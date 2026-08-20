"""Communication adapters used by the RA8P1 DAB host application."""

from __future__ import annotations

import json
import math
import queue
import random
import socket
import threading
import time
from abc import ABC, abstractmethod
from typing import Any

from .protocol import (
    ARCHITECTURE_NAMES,
    CAN_ID_COMMAND,
    CAN_OP_GET_STATUS,
    CAN_OP_PING,
    CAN_OP_STOP,
    DemoConfig,
    PERSONALITY_CHARGE,
    PERSONALITY_CP,
    PERSONALITY_MPPT,
    PERSONALITY_VOLTAGE,
    can_command_payload,
    can_configuration_plan,
    decode_can_frame,
    ethernet_configuration_plan,
    jsonl_encode,
    parse_jsonl_line,
    parse_serial_line,
    serial_configuration_plan,
    serial_engineering_lock_plan,
    serial_engineering_unlock_plan,
    serial_start_plan,
    serial_stop_plan,
)


class TransportError(RuntimeError):
    pass


class BaseTransport(ABC):
    key = "base"
    label = "Base"
    capability_summary = ""

    def __init__(self, events: queue.Queue[dict[str, Any]]) -> None:
        self.events = events
        self._connected = False
        self._closing = threading.Event()

    @property
    def connected(self) -> bool:
        return self._connected

    def emit(self, event_type: str, **fields: Any) -> None:
        self.events.put({"type": event_type, "transport": self.key, **fields})

    def emit_log(self, text: str, level: str = "info") -> None:
        self.emit("log", text=text, level=level)

    def _set_connected(self, connected: bool, detail: str = "") -> None:
        self._connected = connected
        self.emit("connection", connected=connected, detail=detail)

    @abstractmethod
    def connect(self, settings: dict[str, Any]) -> None:
        raise NotImplementedError

    @abstractmethod
    def disconnect(self) -> None:
        raise NotImplementedError

    @abstractmethod
    def send_action(self, action: str, payload: Any = None) -> None:
        raise NotImplementedError


class SimulatorTransport(BaseTransport):
    key = "simulator"
    label = "离线演示"
    capability_summary = "全界面演示 · 明确标记模拟数据 · 不访问任何硬件"

    def __init__(self, events: queue.Queue[dict[str, Any]]) -> None:
        super().__init__(events)
        self._thread: threading.Thread | None = None
        self._config = DemoConfig()
        self._running = False
        self._started_at = 0.0
        self._seq = 0
        self._temperature_pri = 29.0
        self._temperature_sec = 28.0

    def connect(self, settings: dict[str, Any]) -> None:
        if self.connected:
            return
        self._closing.clear()
        self._set_connected(True, "离线模拟器")
        self.emit("identity", firmware="DEMO-SIMULATOR", protocol="local-simulation/1")
        self.emit_log("离线演示已连接：所有数值均为模拟数据。", "warning")
        self._thread = threading.Thread(target=self._run, name="dab-simulator", daemon=True)
        self._thread.start()

    def disconnect(self) -> None:
        self._closing.set()
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.0)
        if self.connected:
            self._set_connected(False, "已断开")

    def send_action(self, action: str, payload: Any = None) -> None:
        if not self.connected:
            raise TransportError("离线演示尚未连接")
        if action == "configure":
            if not isinstance(payload, DemoConfig):
                raise TransportError("无效的演示配置")
            payload.validate()
            self._config = payload
            self.emit_log(
                f"模拟配置已应用：{payload.personality} / {payload.modulation}", "info"
            )
            self._emit_status()
        elif action == "start":
            self._running = True
            self._started_at = time.monotonic()
            self.emit_log("模拟功率传输已启动。", "warning")
        elif action == "stop":
            self._running = False
            self.emit_log("模拟功率传输已停止，PWM=0。", "warning")
            self._emit_status()
        elif action in ("status", "ping"):
            self._emit_status()
        elif action == "raw":
            self.emit_log(f"模拟器收到原始命令：{payload}", "debug")
        else:
            raise TransportError(f"模拟器不支持动作 {action}")

    def _emit_status(self) -> None:
        architecture = {
            PERSONALITY_CHARGE: ARCHITECTURE_NAMES[2],
            PERSONALITY_CP: ARCHITECTURE_NAMES[3],
            PERSONALITY_MPPT: ARCHITECTURE_NAMES[2],
            PERSONALITY_VOLTAGE: ARCHITECTURE_NAMES[0],
        }[self._config.personality]
        self.emit(
            "status",
            source="simulator",
            data={
                "state": "RUN" if self._running else "READY",
                "control_enabled": self._running,
                "pwm_enabled": self._running,
                "fault_mask": 0,
                "valid": True,
                "modulation": self._config.modulation,
                "control_architecture": architecture,
                "simulated": True,
                "received_at": time.time(),
            },
        )

    def _run(self) -> None:
        phase = random.random() * math.tau
        while not self._closing.wait(0.1):
            self._seq += 1
            cfg = self._config
            elapsed = max(0.0, time.monotonic() - self._started_at)
            modulation = cfg.modulation if cfg.modulation != "AUTO" else "TPS"
            efficiency = {"SPS": 0.915, "EPS": 0.938, "TPS": 0.952}.get(
                modulation, 0.92
            )
            ripple = math.sin(elapsed * 2.2 + phase)
            if not self._running:
                pout = 0.0
                iout = 0.0
                vout = 0.0
                state = "READY"
            elif cfg.personality == PERSONALITY_CP:
                pout = max(0.0, cfg.target_power_w * (1.0 + 0.008 * ripple))
                vout = cfg.target_voltage_v * (1.0 + 0.002 * ripple)
                iout = pout / max(vout, 1.0)
                state = "RUN"
            elif cfg.personality == PERSONALITY_MPPT:
                tracking = 1.0 - 0.12 * math.exp(-elapsed / 2.5)
                pout = cfg.target_power_w * tracking * (1.0 + 0.012 * ripple)
                vout = cfg.target_voltage_v * (1.0 + 0.003 * ripple)
                iout = pout / max(vout, 1.0)
                state = "RUN"
            else:
                vout = cfg.target_voltage_v * (1.0 - 0.08 * math.exp(-elapsed / 0.8))
                iout = cfg.target_current_a * (1.0 + 0.006 * ripple)
                pout = vout * iout
                state = "RUN"
            vin = cfg.vin_set_v * (1.0 - (0.004 if self._running else 0.0))
            pin = pout / max(efficiency, 0.5)
            iin = pin / max(vin, 1.0)
            heat_target = 29.0 + (pout / 400.0) * 26.0
            self._temperature_pri += (heat_target - self._temperature_pri) * 0.007
            self._temperature_sec += ((heat_target - 2.0) - self._temperature_sec) * 0.006
            charge_stage = "OFF"
            energy_mode = "OFF"
            if cfg.personality == PERSONALITY_CHARGE:
                energy_mode = "SOLAR_CHARGER"
                charge_stage = "CONSTANT_CURRENT" if elapsed < 12.0 else "CONSTANT_VOLTAGE"
            elif cfg.personality == PERSONALITY_MPPT:
                energy_mode = "SOLAR_CHARGER" if cfg.solar_with_charger else "MPPT_ONLY"
                charge_stage = "CONSTANT_CURRENT" if cfg.solar_with_charger else "OFF"
            telemetry = {
                "seq": self._seq,
                "state": state,
                "valid": True,
                "control_enabled": self._running,
                "pwm_enabled": self._running,
                "fault_mask": 0,
                "vin_v": vin + random.uniform(-0.025, 0.025),
                "vout_v": vout + random.uniform(-0.035, 0.035),
                "iin_a": iin + random.uniform(-0.015, 0.015),
                "iout_a": iout + random.uniform(-0.01, 0.01),
                "pin_w": pin,
                "pout_w": pout,
                "temp_pri_c": self._temperature_pri,
                "temp_sec_c": self._temperature_sec,
                "raw_phase_ns": 0 if not self._running else 80 + 8 * ripple,
                "effective_phase_ns": 0 if not self._running else 80 + 8 * ripple,
                "phase_cap_ns": 160,
                "power_limit_w": cfg.power_limit_w,
                "voltage_ref_v": cfg.target_voltage_v,
                "modulation": modulation,
                "modulation_requested": cfg.modulation,
                "control_architecture": {
                    PERSONALITY_CHARGE: ARCHITECTURE_NAMES[2],
                    PERSONALITY_CP: ARCHITECTURE_NAMES[3],
                    PERSONALITY_MPPT: ARCHITECTURE_NAMES[2],
                    PERSONALITY_VOLTAGE: ARCHITECTURE_NAMES[0],
                }[cfg.personality],
                "energy_mode": energy_mode,
                "charge_stage": charge_stage,
                "battery_profile": cfg.battery_profile.upper(),
                "mppt_power_target_w": cfg.target_power_w
                if (cfg.personality == PERSONALITY_MPPT or
                    (cfg.personality == PERSONALITY_CHARGE and
                     charge_stage == "CONSTANT_CURRENT"))
                else 0.0,
                "ai_applied": cfg.modulation == "AUTO",
                "ipc_peer_seen": True,
                "ipc_peer_stale": False,
                "board_efficiency_pct": efficiency * 100.0,
                "simulated": True,
                "received_at": time.time(),
            }
            self.emit("telemetry", source="simulator", data=telemetry)
            if self._seq % 5 == 0:
                self._emit_algorithm_details(telemetry, ripple)

    def _emit_algorithm_details(self, telemetry: dict[str, Any], ripple: float) -> None:
        """Populate the advanced-algorithm page with clearly simulated data."""

        cfg = self._config
        vin = float(telemetry["vin_v"])
        pout = float(telemetry["pout_w"])
        vad = {
            "seq": self._seq,
            "enabled": True,
            "active": self._running,
            "filtered_vin_v": vin,
            "band_code": 1,
            "band": "BAND_1",
            "gain_scale": 1.0 + 0.01 * ripple,
            "effective_kp_ns_per_a": 5.0,
            "effective_ki_us_per_as": 0.8,
            "feedforward_strength": 0.65,
            "feedforward_delta_ns": 2.0 * ripple,
            "phase_headroom_pct": 72.0,
            "estimated_max_output_power_w": cfg.power_limit_w,
            "estimate_valid": True,
            "actuator_limited": False,
            "actuator_limit_time_ms": 0.0,
            "simulated": True,
            "received_at": time.time(),
        }
        ati = {
            "seq": self._seq,
            "stage": "COMPLETE" if self._running else "IDLE",
            "stage_code": 4 if self._running else 0,
            "running": False,
            "perturbation_ns": 12.0,
            "injection_ns": 0.0,
            "local_gain_a_per_ns": 0.018,
            "recommended_kp_ns_per_a": 5.1,
            "recommended_ki_us_per_as": 0.82,
            "recommended_phase_cap_ns": 160.0,
            "result_valid": self._running,
            "result_applied": False,
            "failure": "NONE",
            "failure_code": 0,
            "simulated": True,
            "received_at": time.time(),
        }
        source_mode = "SPS"
        target_mode = telemetry.get("modulation", cfg.modulation)
        msw = {
            "seq": self._seq,
            "stage": "COMPLETE" if self._running and target_mode != source_mode else "IDLE",
            "source_mode": source_mode,
            "target_mode": target_mode,
            "target_primary_inner_ns": 24.0 if target_mode in ("EPS", "TPS") else 0.0,
            "target_secondary_inner_ns": 16.0 if target_mode == "TPS" else 0.0,
            "baseline_metric": pout,
            "metric_error_pct": 0.6 if self._running else 0.0,
            "elapsed_ms": 180.0 if self._running else 0.0,
            "failure": "NONE",
            "failure_code": 0,
            "simulated": True,
            "received_at": time.time(),
        }
        cpd = {
            "seq": self._seq,
            "enabled": cfg.cp_damping and cfg.personality == PERSONALITY_CP,
            "target_power_w": cfg.target_power_w,
            "active_power_target_w": cfg.target_power_w if self._running else 0.0,
            "power_error_w": cfg.target_power_w - pout if self._running else 0.0,
            "power_kp_a_per_w": 0.012,
            "power_ki_a_per_ws": 0.004,
            "virtual_conductance_a_per_v": 0.015,
            "nominal_output_v": cfg.target_voltage_v,
            "feedforward_current_a": pout / max(cfg.target_voltage_v, 1.0),
            "damping_current_a": 0.02 * ripple if self._running else 0.0,
            "integral_current_a": 0.0,
            "requested_current_a": float(telemetry["iout_a"]),
            "saturated": False,
            "simulated": True,
            "received_at": time.time(),
        }
        for family, data in (("VAD", vad), ("ATI", ati), ("MSW", msw), ("CPD", cpd)):
            self.emit("algorithm_detail", source="simulator", family=family, data=data)


class SerialTransport(BaseTransport):
    key = "serial"
    label = "串口"
    capability_summary = "当前功能最完整 · 文本命令 + DAB/ALG 遥测 · 需要 pyserial"

    def __init__(self, events: queue.Queue[dict[str, Any]]) -> None:
        super().__init__(events)
        self._serial: Any = None
        self._thread: threading.Thread | None = None
        self._write_lock = threading.Lock()
        self._config = DemoConfig()

    def connect(self, settings: dict[str, Any]) -> None:
        if self.connected:
            return
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise TransportError("串口功能需要安装 pyserial：python -m pip install pyserial") from exc
        port = str(settings.get("port", "COM4"))
        baudrate = int(settings.get("baudrate", 115200))
        try:
            self._serial = serial.Serial(
                port=port,
                baudrate=baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.2,
                write_timeout=1.0,
            )
        except Exception as exc:
            raise TransportError(f"无法打开 {port}: {exc}") from exc
        self._closing.clear()
        self._set_connected(True, f"{port} @ {baudrate} 8N1")
        self._thread = threading.Thread(target=self._reader, name="dab-uart-reader", daemon=True)
        self._thread.start()
        self.emit_log("串口已连接；尚未启动功率。正在请求 save 遥测和状态。")
        self._send_line("version")
        self._send_line("alglog on")
        self._send_line("mode save")
        self._send_line("show")

    def disconnect(self) -> None:
        self._closing.set()
        serial_port = self._serial
        self._serial = None
        if serial_port is not None:
            try:
                serial_port.close()
            except Exception:
                pass
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=0.6)
        if self.connected:
            self._set_connected(False, "已断开")

    def _send_line(self, line: str) -> None:
        if not self.connected or self._serial is None:
            raise TransportError("串口未连接")
        payload = (line.strip() + "\r\n").encode("ascii", errors="strict")
        try:
            with self._write_lock:
                self._serial.write(payload)
                self._serial.flush()
            self.emit("tx", text=line.strip())
        except Exception as exc:
            raise TransportError(f"串口发送失败: {exc}") from exc

    def _send_lines(self, commands: list[str], delay_s: float = 0.025) -> None:
        """Pace command bursts so the board's UART shell can process each line."""

        for index, command in enumerate(commands):
            self._send_line(command)
            if delay_s > 0.0 and index + 1 < len(commands):
                time.sleep(delay_s)

    def send_action(self, action: str, payload: Any = None) -> None:
        if action == "configure":
            if not isinstance(payload, DemoConfig):
                raise TransportError("无效的演示配置")
            self._config = payload
            plan = serial_configuration_plan(payload)
            for warning in plan.warnings:
                self.emit_log(warning, "warning")
            self._send_lines(plan.commands)
        elif action == "start":
            self._send_lines(serial_start_plan(self._config).commands, delay_s=0.15)
        elif action == "stop":
            self._send_lines(serial_stop_plan(self._config).commands)
        elif action == "engineer_unlock":
            self._send_lines(serial_engineering_unlock_plan(float(payload or 400.0)).commands)
        elif action == "engineer_lock":
            self._send_lines(serial_engineering_lock_plan().commands)
        elif action == "status":
            self._send_line("show")
        elif action == "ping":
            self._send_line("show")
        elif action == "raw":
            if not isinstance(payload, str) or not payload.strip():
                raise TransportError("原始串口命令为空")
            self._send_line(payload)
        else:
            raise TransportError(f"串口不支持动作 {action}")

    def _reader(self) -> None:
        while not self._closing.is_set() and self._serial is not None:
            try:
                raw = self._serial.readline()
                if self._closing.is_set():
                    break
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace")
                event = parse_serial_line(text)
                self.events.put({"transport": self.key, **event})
                self.emit("rx", text=text.rstrip())
            except Exception as exc:
                if not self._closing.is_set():
                    self.emit_log(f"串口接收失败: {exc}", "error")
                    self._set_connected(False, str(exc))
                break


class EthernetTransport(BaseTransport):
    key = "ethernet"
    label = "以太网"
    capability_summary = "TCP 5000 + JSONL · 实时全量遥测 · 当前 v1 仅完整控制电压模式"

    def __init__(self, events: queue.Queue[dict[str, Any]]) -> None:
        super().__init__(events)
        self._socket: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._send_lock = threading.Lock()
        self._next_id = 1
        self._config = DemoConfig()
        self._configuration_complete = False
        self._host = "192.168.10.100"
        self._port = 5000

    def connect(self, settings: dict[str, Any]) -> None:
        if self.connected or (self._thread and self._thread.is_alive()):
            return
        self._host = str(settings.get("host", "192.168.10.100"))
        self._port = int(settings.get("port", 5000))
        self._closing.clear()
        self.emit("connection", connected=False, connecting=True, detail=f"{self._host}:{self._port}")
        self._thread = threading.Thread(target=self._connect_and_read, name="dab-tcp-reader", daemon=True)
        self._thread.start()

    def disconnect(self) -> None:
        self._closing.set()
        sock = self._socket
        self._socket = None
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=0.8)
        if self.connected:
            self._set_connected(False, "已断开")

    def _connect_and_read(self) -> None:
        try:
            sock = socket.create_connection((self._host, self._port), timeout=3.0)
            sock.settimeout(0.25)
            self._socket = sock
            self._set_connected(True, f"{self._host}:{self._port}")
            self.emit_log("TCP 已连接；等待板端 hello/telemetry。")
            self._send_request({"cmd": "get_status"})
            buffer = bytearray()
            last_ping = time.monotonic()
            while not self._closing.is_set():
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        raise ConnectionError("板端关闭 TCP 连接")
                    buffer.extend(chunk)
                    while b"\n" in buffer:
                        raw, _, rest = buffer.partition(b"\n")
                        buffer[:] = rest
                        if not raw.strip():
                            continue
                        self.emit("rx", text=raw.decode("utf-8", errors="replace"))
                        try:
                            event = parse_jsonl_line(raw)
                            self.events.put({"transport": self.key, **event})
                        except Exception as exc:
                            self.emit_log(f"JSONL 解析失败: {exc}; raw={raw!r}", "warning")
                except socket.timeout:
                    pass
                if time.monotonic() - last_ping >= 5.0:
                    self._send_request({"cmd": "ping"})
                    last_ping = time.monotonic()
        except Exception as exc:
            if not self._closing.is_set():
                self.emit_log(f"以太网连接失败: {exc}", "error")
                self._set_connected(False, str(exc))
        finally:
            sock = self._socket
            self._socket = None
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
            if self.connected:
                self._set_connected(False, "连接结束")

    def _send_request(self, command: dict[str, Any]) -> int:
        sock = self._socket
        if not self.connected or sock is None:
            raise TransportError("以太网未连接")
        request_id = self._next_id
        self._next_id += 1
        payload = jsonl_encode(request_id, command)
        try:
            with self._send_lock:
                sock.sendall(payload)
        except OSError as exc:
            raise TransportError(f"TCP 发送失败: {exc}") from exc
        self.emit("tx", text=payload.decode("utf-8").rstrip())
        return request_id

    def send_action(self, action: str, payload: Any = None) -> None:
        if action == "configure":
            if not isinstance(payload, DemoConfig):
                raise TransportError("无效的演示配置")
            self._config = payload
            plan = ethernet_configuration_plan(payload)
            self._configuration_complete = plan.supported
            for warning in plan.warnings:
                self.emit_log(warning, "warning")
            for command in plan.commands:
                self._send_request(command)
        elif action == "start":
            if not self._configuration_complete:
                raise TransportError("当前 Ethernet v1 不能完整配置所选运行模式，已阻止启动；请改用串口或稳压模式。")
            self._send_request({"cmd": "start"})
        elif action == "stop":
            self._send_request({"cmd": "stop"})
        elif action == "status":
            self._send_request({"cmd": "get_status"})
        elif action == "ping":
            self._send_request({"cmd": "ping"})
        elif action == "raw":
            if isinstance(payload, str):
                payload = json.loads(payload)
            if not isinstance(payload, dict):
                raise TransportError("Ethernet 原始命令必须是 JSON 对象")
            self._send_request(payload)
        else:
            raise TransportError(f"以太网不支持动作 {action}")


class CanTransport(BaseTransport):
    key = "can"
    label = "CAN"
    capability_summary = "Classic CAN 500 kbit/s · 状态/测量/STOP/CP目标 · v1 不含 START"

    def __init__(self, events: queue.Queue[dict[str, Any]]) -> None:
        super().__init__(events)
        self._bus: Any = None
        self._can_module: Any = None
        self._thread: threading.Thread | None = None
        self._send_lock = threading.Lock()
        self._sequence = 1
        self._config = DemoConfig(personality=PERSONALITY_CP)

    def _next_sequence(self) -> int:
        value = self._sequence & 0xFF
        self._sequence = (self._sequence + 1) & 0xFF
        return value

    def connect(self, settings: dict[str, Any]) -> None:
        if self.connected:
            return
        try:
            import can  # type: ignore
        except ImportError as exc:
            raise TransportError("CAN 功能需要安装 python-can：python -m pip install python-can") from exc
        interface = str(settings.get("interface", "slcan"))
        channel = str(settings.get("channel", "COM3"))
        bitrate = int(settings.get("bitrate", 500_000))
        try:
            self._bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate)
        except Exception as exc:
            raise TransportError(f"无法打开 CAN 适配器 {interface}/{channel}: {exc}") from exc
        self._can_module = can
        self._closing.clear()
        self._set_connected(True, f"{interface} / {channel} / {bitrate} bit/s")
        self._thread = threading.Thread(target=self._reader, name="dab-can-reader", daemon=True)
        self._thread.start()
        self.emit_log("CAN 已连接；协议 v1 不提供 START，软件不会通过 CAN 启动功率。")
        self._send_opcode(CAN_OP_PING)
        self._send_opcode(CAN_OP_GET_STATUS)

    def disconnect(self) -> None:
        self._closing.set()
        bus = self._bus
        self._bus = None
        if bus is not None:
            try:
                bus.shutdown()
            except Exception:
                pass
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=0.8)
        if self.connected:
            self._set_connected(False, "已断开")

    def _send_frame(self, arbitration_id: int, data: bytes) -> None:
        if not self.connected or self._bus is None or self._can_module is None:
            raise TransportError("CAN 未连接")
        message = self._can_module.Message(
            arbitration_id=arbitration_id,
            is_extended_id=False,
            data=data,
        )
        try:
            with self._send_lock:
                self._bus.send(message, timeout=1.0)
        except Exception as exc:
            raise TransportError(f"CAN 发送失败: {exc}") from exc
        self.emit("tx", text=f"{arbitration_id:#05x} {data.hex(' ')}")

    def _send_opcode(self, opcode: int) -> None:
        sequence = self._next_sequence()
        self._send_frame(CAN_ID_COMMAND, can_command_payload(opcode, sequence))

    def send_action(self, action: str, payload: Any = None) -> None:
        if action == "configure":
            if not isinstance(payload, DemoConfig):
                raise TransportError("无效的演示配置")
            self._config = payload
            plan = can_configuration_plan(payload, self._next_sequence())
            for warning in plan.warnings:
                self.emit_log(warning, "warning")
            if not plan.commands:
                raise TransportError(plan.warnings[0] if plan.warnings else "CAN v1 不支持该配置")
            for arbitration_id, data in plan.commands:
                self._send_frame(arbitration_id, data)
        elif action == "start":
            raise TransportError("CAN v1 故意没有 START；请沿用现场显式启动流程。")
        elif action == "stop":
            self._send_opcode(CAN_OP_STOP)
        elif action == "status":
            self._send_opcode(CAN_OP_GET_STATUS)
        elif action == "ping":
            self._send_opcode(CAN_OP_PING)
        else:
            raise TransportError(f"CAN 不支持动作 {action}")

    def _reader(self) -> None:
        last_status = 0.0
        while not self._closing.is_set() and self._bus is not None:
            try:
                message = self._bus.recv(timeout=0.2)
                if message is not None:
                    raw = bytes(message.data)
                    self.emit("rx", text=f"{message.arbitration_id:#05x} {raw.hex(' ')}")
                    event = decode_can_frame(message.arbitration_id, raw)
                    self.events.put({"transport": self.key, **event})
                if time.monotonic() - last_status >= 1.0:
                    self._send_opcode(CAN_OP_GET_STATUS)
                    last_status = time.monotonic()
            except Exception as exc:
                if not self._closing.is_set():
                    self.emit_log(f"CAN 接收失败: {exc}", "error")
                    self._set_connected(False, str(exc))
                break


TRANSPORT_CLASSES = {
    "simulator": SimulatorTransport,
    "serial": SerialTransport,
    "can": CanTransport,
    "ethernet": EthernetTransport,
}
