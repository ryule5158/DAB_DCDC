from __future__ import annotations

import json
import queue
import socket
import threading
import time
import unittest

from dab_console.protocol import DemoConfig, PERSONALITY_CP
from dab_console.transports import EthernetTransport, SimulatorTransport


def wait_event(events: queue.Queue, event_type: str, timeout: float = 2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            event = events.get(timeout=0.05)
        except queue.Empty:
            continue
        if event.get("type") == event_type:
            return event
    raise AssertionError(f"timed out waiting for {event_type}")


class SimulatorTransportTests(unittest.TestCase):
    def test_configure_start_telemetry_stop(self) -> None:
        events: queue.Queue = queue.Queue()
        transport = SimulatorTransport(events)
        transport.connect({})
        config = DemoConfig(
            personality=PERSONALITY_CP,
            target_power_w=100.0,
            power_limit_w=120.0,
        )
        transport.send_action("configure", config)
        transport.send_action("start")
        telemetry = wait_event(events, "telemetry")
        self.assertTrue(telemetry["data"]["simulated"])
        self.assertEqual(telemetry["data"]["state"], "RUN")
        detail = wait_event(events, "algorithm_detail")
        self.assertTrue(detail["data"]["simulated"])
        self.assertIn(detail["family"], ("VAD", "ATI", "MSW", "CPD"))
        transport.send_action("stop")
        transport.disconnect()
        self.assertFalse(transport.connected)


class EthernetTransportTests(unittest.TestCase):
    def test_local_jsonl_server_roundtrip(self) -> None:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        port = server.getsockname()[1]
        received: list[dict] = []

        def serve() -> None:
            client, _address = server.accept()
            with client:
                client.sendall(
                    b'{"type":"hello","protocol":"dab-jsonl/1","telemetry_ms":1000}\n'
                    b'{"type":"telemetry","seq":1,"state":"READY","valid":true,'
                    b'"vin_v":24.0,"vout_v":0.0,"pout_w":0.0}\n'
                )
                buffer = bytearray()
                deadline = time.monotonic() + 1.5
                while time.monotonic() < deadline:
                    client.settimeout(0.1)
                    try:
                        chunk = client.recv(4096)
                    except socket.timeout:
                        continue
                    if not chunk:
                        break
                    buffer.extend(chunk)
                    while b"\n" in buffer:
                        raw, _, rest = buffer.partition(b"\n")
                        buffer[:] = rest
                        message = json.loads(raw)
                        received.append(message)
                        if message.get("cmd") == "get_status":
                            response = {
                                "type": "response",
                                "id": message["id"],
                                "cmd": "get_status",
                                "ok": True,
                                "status": {"state": "READY", "valid": True, "fault_mask": 0},
                            }
                            client.sendall((json.dumps(response) + "\n").encode())
            server.close()

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        events: queue.Queue = queue.Queue()
        transport = EthernetTransport(events)
        transport.connect({"host": "127.0.0.1", "port": port})
        telemetry = wait_event(events, "telemetry")
        self.assertEqual(telemetry["data"]["vin_v"], 24.0)
        wait_event(events, "status")
        self.assertTrue(any(message.get("cmd") == "get_status" for message in received))
        transport.disconnect()
        thread.join(timeout=1.0)
        self.assertFalse(transport.connected)


if __name__ == "__main__":
    unittest.main()
