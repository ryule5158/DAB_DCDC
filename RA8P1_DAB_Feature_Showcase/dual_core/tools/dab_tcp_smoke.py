#!/usr/bin/env python3
"""Safe communication smoke test for the RA8P1 DAB JSON Lines server.

The default test never sends ``start`` or changes a power-control parameter.
It validates a split frame, two coalesced frames, telemetry, heartbeat, and a
clean disconnect/reconnect.  Use --exercise-start-stop only during an
explicitly authorized powered commissioning run.
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from typing import Any


class JsonLinesClient:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: socket.socket | None = None
        self.buffer = bytearray()

    def connect(self, attempts: int, delay: float) -> None:
        self.close()
        last_error: OSError | None = None
        for attempt in range(1, attempts + 1):
            try:
                sock = socket.create_connection((self.host, self.port), self.timeout)
                sock.settimeout(self.timeout)
                self.sock = sock
                self.buffer.clear()
                print(f"[LINK] connected to {self.host}:{self.port} (attempt {attempt})")
                return
            except OSError as exc:
                last_error = exc
                print(f"[LINK] attempt {attempt}/{attempts} failed: {exc}")
                if attempt < attempts:
                    time.sleep(delay)
        raise ConnectionError(f"cannot connect after {attempts} attempts: {last_error}")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.sock.close()
            self.sock = None
        self.buffer.clear()

    @staticmethod
    def encode(message: dict[str, Any]) -> bytes:
        return (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")

    def send_raw(self, payload: bytes, label: str) -> None:
        if self.sock is None:
            raise ConnectionError("not connected")
        print(f"[TX:{label}] {payload!r}")
        self.sock.sendall(payload)

    def send(self, message: dict[str, Any]) -> None:
        payload = self.encode(message)
        self.send_raw(payload, "jsonl")

    def receive(self) -> dict[str, Any]:
        if self.sock is None:
            raise ConnectionError("not connected")
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                raw = bytes(self.buffer[:newline])
                del self.buffer[: newline + 1]
                if not raw.strip():
                    continue
                message = json.loads(raw.decode("utf-8"))
                print(f"[RX] {json.dumps(message, ensure_ascii=False, sort_keys=True)}")
                return message
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("device closed the TCP connection")
            self.buffer.extend(chunk)

    def wait_for_response(self, request_id: int) -> dict[str, Any]:
        while True:
            message = self.receive()
            if message.get("type") == "response" and message.get("id") == request_id:
                return message

    def request(self, request_id: int, command: str, **fields: Any) -> dict[str, Any]:
        message: dict[str, Any] = {"id": request_id, "cmd": command}
        message.update(fields)
        self.send(message)
        return self.wait_for_response(request_id)


def require_ok(message: dict[str, Any], context: str) -> None:
    if message.get("ok") is not True:
        raise RuntimeError(f"{context} failed: {message}")


def wait_for_type(client: JsonLinesClient, message_type: str) -> dict[str, Any]:
    while True:
        message = client.receive()
        if message.get("type") == message_type:
            return message


def run_safe_protocol_checks(client: JsonLinesClient) -> None:
    hello = wait_for_type(client, "hello")
    if hello.get("protocol") != "dab-jsonl/1":
        raise RuntimeError(f"unexpected hello: {hello}")

    split_ping = client.encode({"id": 101, "cmd": "ping"})
    cuts = (3, max(4, len(split_ping) // 2))
    client.send_raw(split_ping[: cuts[0]], "split-1/3")
    time.sleep(0.03)
    client.send_raw(split_ping[cuts[0] : cuts[1]], "split-2/3")
    time.sleep(0.03)
    client.send_raw(split_ping[cuts[1] :], "split-3/3")
    require_ok(client.wait_for_response(101), "split ping")

    sticky = (
        client.encode({"id": 102, "cmd": "get_status"})
        + client.encode({"id": 103, "cmd": "get_parameter", "name": "load_mode"})
    )
    client.send_raw(sticky, "sticky-2-lines")
    pending = {102, 103}
    while pending:
        message = client.receive()
        response_id = message.get("id")
        if message.get("type") == "response" and response_id in pending:
            require_ok(message, f"sticky request {response_id}")
            pending.remove(response_id)

    require_ok(
        client.request(
            104,
            "set_parameter",
            name="load_mode",
            value="resistive_load",
        ),
        "safe load-mode metadata write",
    )
    require_ok(client.request(105, "ping"), "heartbeat ping")
    telemetry = wait_for_type(client, "telemetry")
    if "state" not in telemetry or "seq" not in telemetry:
        raise RuntimeError(f"incomplete telemetry: {telemetry}")


def exercise_start_stop(client: JsonLinesClient) -> None:
    print("[POWER] explicit --exercise-start-stop selected; START may energize the converter")
    try:
        require_ok(client.request(201, "start"), "start")
        time.sleep(0.2)
    finally:
        require_ok(client.request(202, "stop"), "stop")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.10.100")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--reconnect-attempts", type=int, default=5)
    parser.add_argument("--reconnect-delay", type=float, default=1.0)
    parser.add_argument(
        "--exercise-start-stop",
        action="store_true",
        help="explicitly send START then STOP; this can energize the power stage",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    client = JsonLinesClient(args.host, args.port, args.timeout)
    try:
        client.connect(args.reconnect_attempts, args.reconnect_delay)
        run_safe_protocol_checks(client)
        if args.exercise_start_stop:
            exercise_start_stop(client)

        print("[LINK] closing once to validate server-side disconnect cleanup")
        client.close()
        time.sleep(0.2)
        client.connect(args.reconnect_attempts, args.reconnect_delay)
        wait_for_type(client, "hello")
        require_ok(client.request(301, "ping"), "post-reconnect ping")
        print("[PASS] JSONL split/sticky/telemetry/heartbeat/reconnect checks passed")
        return 0
    except (ConnectionError, OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
