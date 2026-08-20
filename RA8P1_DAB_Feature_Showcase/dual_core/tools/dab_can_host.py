#!/usr/bin/env python3
"""Minimal PC-side client for the DAB board's safe classic-CAN protocol.

Requires python-can only when accessing an adapter.  ``--self-test`` validates
the codec without hardware or third-party packages.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from typing import Any

PROTOCOL_VERSION = 1
ID_STATUS = 0x180
ID_MEASUREMENTS = 0x181
ID_ACK = 0x182
ID_COMMAND = 0x200

OP_PING = 0x01
OP_GET_STATUS = 0x02
OP_STOP = 0x10
OP_SET_CP_TARGET = 0x20

RESULT_NAMES = {
    0: "ok",
    1: "bad_version",
    2: "bad_opcode",
    3: "bad_dlc",
    4: "bad_state",
    5: "out_of_range",
    6: "queue_full",
    7: "transport_error",
    8: "no_validated_seed",
}


def command_payload(opcode: int, sequence: int, cp_target_w: float | None = None) -> bytes:
    data = bytearray(8)
    data[0:3] = bytes((PROTOCOL_VERSION, opcode & 0xFF, sequence & 0xFF))
    if opcode == OP_SET_CP_TARGET:
        if cp_target_w is None or not 0.0 <= cp_target_w <= 6553.5:
            raise ValueError("CP target must be within the CAN encoding range 0..6553.5 W")
        data[3:5] = struct.pack("<H", round(cp_target_w * 10.0))
    return bytes(data)


def decode_frame(arbitration_id: int, data: bytes) -> dict[str, Any]:
    if len(data) != 8:
        return {"id": hex(arbitration_id), "error": "expected_dlc_8", "raw": data.hex()}
    if arbitration_id == ID_ACK:
        version, opcode, sequence, result, state, architecture, rx_drop, errors = data
        return {
            "type": "ack",
            "version": version,
            "opcode": hex(opcode),
            "sequence": sequence,
            "result": RESULT_NAMES.get(result, f"unknown_{result}"),
            "state": state,
            "architecture": architecture,
            "rx_dropped_low8": rx_drop,
            "error_events_low8": errors,
        }
    if arbitration_id == ID_STATUS:
        faults = data[5] | (data[6] << 8) | (data[7] << 16)
        return {
            "type": "status",
            "version": data[0],
            "state": data[1],
            "architecture": data[2],
            "modulation": data[3],
            "flags": hex(data[4]),
            "fault_mask_low24": hex(faults),
        }
    if arbitration_id == ID_MEASUREMENTS:
        vin_dv, vout_dv, iout_ca, pout_dw = struct.unpack("<HHhh", data)
        return {
            "type": "measurements",
            "vin_v": vin_dv / 10.0,
            "vout_v": vout_dv / 10.0,
            "iout_a": iout_ca / 100.0,
            "pout_w": pout_dw / 10.0,
        }
    return {"id": hex(arbitration_id), "raw": data.hex()}


def self_test() -> None:
    assert command_payload(OP_SET_CP_TARGET, 7, 200.0)[3:5] == b"\xd0\x07"
    measurement = decode_frame(ID_MEASUREMENTS, struct.pack("<HHhh", 234, 410, -125, 2074))
    assert measurement["vin_v"] == 23.4
    assert measurement["iout_a"] == -1.25
    assert measurement["pout_w"] == 207.4
    ack = decode_frame(ID_ACK, bytes((1, OP_STOP, 9, 0, 1, 3, 0, 0)))
    assert ack["result"] == "ok" and ack["sequence"] == 9


def execute(args: argparse.Namespace) -> int:
    try:
        import can  # type: ignore
    except ImportError:
        print("python-can is required for adapter access: python -m pip install python-can", file=sys.stderr)
        return 2

    opcode = {
        "ping": OP_PING,
        "status": OP_GET_STATUS,
        "stop": OP_STOP,
        "set-cp": OP_SET_CP_TARGET,
    }[args.command]
    sequence = int(time.monotonic_ns() >> 20) & 0xFF
    payload = command_payload(opcode, sequence, args.watts)

    with can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate) as bus:
        bus.send(can.Message(arbitration_id=ID_COMMAND, is_extended_id=False, data=payload),
                 timeout=args.timeout)
        deadline = time.monotonic() + args.timeout
        ack_ok = False
        while time.monotonic() < deadline:
            message = bus.recv(timeout=max(0.0, deadline - time.monotonic()))
            if message is None:
                break
            decoded = decode_frame(message.arbitration_id, bytes(message.data))
            print(json.dumps(decoded, ensure_ascii=False, sort_keys=True))
            if (message.arbitration_id == ID_ACK and
                    decoded.get("sequence") == sequence and
                    decoded.get("opcode") == hex(opcode)):
                ack_ok = decoded.get("result") == "ok"
                if args.command not in ("status", "stop", "set-cp"):
                    break
        return 0 if ack_ok else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--interface", default="slcan", help="python-can interface, e.g. slcan or vector")
    parser.add_argument("--channel", default="COM3", help="adapter channel, e.g. COM3")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("command", choices=("ping", "status", "stop", "set-cp"), nargs="?")
    parser.add_argument("watts", type=float, nargs="?", help="target watts for set-cp")
    args = parser.parse_args()
    if not args.self_test and args.command is None:
        parser.error("command is required unless --self-test is used")
    if args.command == "set-cp" and args.watts is None:
        parser.error("set-cp requires watts")
    return args


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        print("dab_can_host codec self-test passed")
        return 0
    return execute(args)


if __name__ == "__main__":
    raise SystemExit(main())
