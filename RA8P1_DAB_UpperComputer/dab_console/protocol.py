"""Protocol codecs and high-level command planning for the RA8P1 DAB host.

The UI deals in a small set of high-level demo personalities.  This module is
the only place where those intentions are translated to the currently
available UART, TCP/JSONL, or classic-CAN commands.  Keeping that translation
out of the UI makes later firmware protocol changes local and reviewable.
"""

from __future__ import annotations

import json
import re
import struct
import time
from dataclasses import dataclass, field
from typing import Any, Iterable


PERSONALITY_CHARGE = "CV_CHARGE"
PERSONALITY_CP = "CP_ENERGY_ROUTE"
PERSONALITY_MPPT = "MPPT_SOLAR"
PERSONALITY_VOLTAGE = "VOLTAGE_SUPPORT"

PERSONALITIES = (
    PERSONALITY_CHARGE,
    PERSONALITY_CP,
    PERSONALITY_MPPT,
    PERSONALITY_VOLTAGE,
)

MODULATIONS = ("SPS", "EPS", "TPS", "AUTO")
MPPT_ALGORITHMS = ("po", "inc")

# Smart charging is selected by chemistry only.  The lower firmware owns the
# complete profile (CV voltage, CC/precharge/termination currents, state
# transitions and modulation).  In particular, the upper computer must never
# overwrite those values with stale bench-tuning targets.
VALIDATED_CHARGE_PROFILES = ("liion10", "lfp12")

STATE_NAMES = (
    "BOOT",
    "READY",
    "RESET_LOW",
    "LATCH_RELEASE",
    "MAIN_SETTLE",
    "OUTPUT_SETTLE",
    "SOFTSTART",
    "RUN",
    "PAUSED",
    "PATH_DIRECT",
    "TEST_MODE",
    "FAULT",
)

ARCHITECTURE_NAMES = (
    "VOLTAGE_PI_SINGLE_LOOP",
    "OUTPUT_CURRENT_PI",
    "CPU1_ENERGY_SUPERVISOR_CURRENT_PI",
    "CP_POWER_PI_CURRENT_PI_DAMPED",
)

ENERGY_MODE_NAMES = (
    "OFF",
    "MANUAL_CURRENT",
    "MPPT_ONLY",
    "CHARGER_ONLY",
    "SOLAR_CHARGER",
)

BATTERY_PROFILE_NAMES = (
    "CUSTOM",
    "LI_ION_10S_42V",
    "LIFEPO4_12S_43P8V",
    "LEAD_ACID_18S_36V",
)

CHARGE_STAGE_NAMES = (
    "OFF",
    "IDLE",
    "PRECHARGE",
    "CONSTANT_CURRENT",
    "CONSTANT_VOLTAGE",
    "FLOAT",
    "DONE",
    "FAULT",
)

AUTOTUNE_STAGE_NAMES = (
    "IDLE",
    "SETTLE",
    "POSITIVE_STEP",
    "NEGATIVE_STEP",
    "COMPLETE",
    "FAILED",
)

AUTOTUNE_FAILURE_NAMES = (
    "NONE",
    "INSUFFICIENT_RESPONSE",
    "USER_STOP",
    "CONTROL_RESET",
)

MODSWITCH_STAGE_NAMES = (
    "IDLE",
    "TRANSITION",
    "VERIFY",
    "COMPLETE",
    "ROLLBACK",
    "FAILED",
)

MODSWITCH_FAILURE_NAMES = (
    "NONE",
    "METRIC_ABORT",
    "TIMEOUT",
    "CONTROL_RESET",
)


DAB_FIELDS = (
    "seq",
    "vin_v",
    "vout_v",
    "iin_a",
    "iout_a",
    "ipri_a",
    "isec_a",
    "temp_pri_c",
    "temp_sec_c",
    "pin_w",
    "pout_w",
    "vafe_half_v",
    "raw_phase_ns",
    "power_limit_w",
    "voltage_ref_v",
    "state_code",
    "control_enabled",
    "pwm_enabled",
    "fault_mask",
    "valid",
    "modulation_requested_code",
    "modulation_effective_code",
    "effective_phase_ns",
    "command_phase_ns",
    "primary_inner_ns",
    "secondary_inner_ns",
    "inner_manual",
    "kp_ns_per_v",
    "ki_us_per_vs",
    "kd_ns_s_per_v",
    "d_filter_alpha",
    "phase_cap_ns",
    "bench_vin_set_v",
    "load_nominal_ohm",
    "requested_power_layer_w",
    "control_architecture_code",
    "schedule_enabled",
    "table_entry_id",
    "outer_saturated",
    "outer_saturation_updates",
    "feedforward_enabled",
    "feedforward_effective_phase_ns",
)

ALG_FIELDS = (
    "seq",
    "control_architecture_code",
    "guard_profile_code",
    "current_reference_command_a",
    "current_reference_active_a",
    "current_error_a",
    "current_kp_ns_per_a",
    "current_ki_us_per_as",
    "current_reference_slew_a_per_s",
    "energy_mode_code",
    "energy_reference_valid",
    "energy_age_ms",
    "energy_current_reference_a",
    "mppt_power_target_w",
    "mppt_voltage_target_v",
    "mppt_limited",
    "battery_profile_code",
    "charge_stage_code",
    "charge_voltage_target_v",
    "charger_limited",
    "ai_hint_current_a",
    "ai_confidence",
    "ai_applied",
    "ai_modulation_hint_code",
    "ai_modulation_confidence",
    "ai_modulation_fresh",
    "ipc_peer_seen",
    "ipc_peer_stale",
    "energy_config_queued",
    "energy_config_dropped",
    "current_control_direction",
)

VAD_FIELDS = (
    "seq",
    "enabled",
    "active",
    "filtered_vin_v",
    "band_code",
    "gain_scale",
    "effective_kp_ns_per_a",
    "effective_ki_us_per_as",
    "feedforward_strength",
    "feedforward_delta_ns",
    "phase_headroom_pct",
    "estimated_max_output_power_w",
    "estimate_valid",
    "actuator_limited",
    "actuator_limit_time_ms",
)

ATI_FIELDS = (
    "seq",
    "stage_code",
    "running",
    "perturbation_ns",
    "injection_ns",
    "positive_current_a",
    "negative_current_a",
    "local_gain_a_per_ns",
    "recommended_direction",
    "recommended_kp_ns_per_a",
    "recommended_ki_us_per_as",
    "recommended_slew_a_per_s",
    "recommended_phase_cap_ns",
    "result_valid",
    "result_applied",
    "failure_code",
)

MSW_FIELDS = (
    "seq",
    "stage_code",
    "source_mode_code",
    "target_mode_code",
    "target_primary_inner_ns",
    "target_secondary_inner_ns",
    "baseline_metric",
    "metric_error_pct",
    "elapsed_ms",
    "source_direction",
    "target_direction",
    "failure_code",
)

CPD_FIELDS = (
    "seq",
    "enabled",
    "target_power_w",
    "active_power_target_w",
    "power_error_w",
    "power_kp_a_per_w",
    "power_ki_a_per_ws",
    "virtual_conductance_a_per_v",
    "nominal_output_v",
    "feedforward_current_a",
    "damping_current_a",
    "integral_current_a",
    "requested_current_a",
    "saturated",
)

ALGORITHM_DETAIL_FIELDS = {
    "VAD": VAD_FIELDS,
    "ATI": ATI_FIELDS,
    "MSW": MSW_FIELDS,
    "CPD": CPD_FIELDS,
}

INT_FIELDS = {
    "seq",
    "raw_phase_ns",
    "state_code",
    "fault_mask",
    "modulation_requested_code",
    "modulation_effective_code",
    "effective_phase_ns",
    "command_phase_ns",
    "primary_inner_ns",
    "secondary_inner_ns",
    "phase_cap_ns",
    "control_architecture_code",
    "table_entry_id",
    "outer_saturation_updates",
    "guard_profile_code",
    "energy_mode_code",
    "energy_age_ms",
    "battery_profile_code",
    "charge_stage_code",
    "ai_modulation_hint_code",
    "energy_config_queued",
    "energy_config_dropped",
    "current_control_direction",
    "band_code",
    "stage_code",
    "recommended_direction",
    "failure_code",
    "source_mode_code",
    "target_mode_code",
    "source_direction",
    "target_direction",
}

BOOL_FIELDS = {
    "control_enabled",
    "pwm_enabled",
    "valid",
    "inner_manual",
    "schedule_enabled",
    "outer_saturated",
    "feedforward_enabled",
    "energy_reference_valid",
    "mppt_limited",
    "charger_limited",
    "ai_applied",
    "ai_modulation_fresh",
    "ipc_peer_seen",
    "ipc_peer_stale",
    "enabled",
    "active",
    "estimate_valid",
    "actuator_limited",
    "running",
    "result_valid",
    "result_applied",
    "saturated",
}


def _name(items: tuple[str, ...], value: Any, fallback: str = "?") -> str:
    try:
        index = int(value)
    except (TypeError, ValueError):
        return fallback
    return items[index] if 0 <= index < len(items) else fallback


def _coerce(field_name: str, value: str) -> Any:
    value = value.strip()
    if field_name in BOOL_FIELDS:
        return value not in ("0", "0.0", "false", "False", "")
    if field_name in INT_FIELDS:
        return int(float(value))
    return float(value)


def _decode_csv_payload(prefix: str, line: str, fields: tuple[str, ...]) -> dict[str, Any]:
    raw_values = line[len(prefix) :].strip().split(",")
    if len(raw_values) < len(fields):
        raise ValueError(
            f"{prefix[:-1]} field count {len(raw_values)} is smaller than expected {len(fields)}"
        )
    decoded = {name: _coerce(name, raw_values[index]) for index, name in enumerate(fields)}
    if len(raw_values) > len(fields):
        decoded["extra_fields"] = raw_values[len(fields) :]
    return decoded


def normalize_telemetry(payload: dict[str, Any]) -> dict[str, Any]:
    """Normalize names and derived labels without erasing source fields."""

    data = dict(payload)
    if "measurements_valid" in data and "valid" not in data:
        data["valid"] = bool(data["measurements_valid"])
    if "state_code" in data and "state" not in data:
        data["state"] = _name(STATE_NAMES, data["state_code"])
    if "control_architecture_code" in data and "control_architecture" not in data:
        data["control_architecture"] = _name(
            ARCHITECTURE_NAMES, data["control_architecture_code"]
        )
    if "modulation_effective_code" in data and "modulation" not in data:
        data["modulation"] = _name(MODULATIONS, data["modulation_effective_code"])
    if "energy_mode_code" in data:
        data["energy_mode"] = _name(ENERGY_MODE_NAMES, data["energy_mode_code"])
    if "battery_profile_code" in data:
        data["battery_profile"] = _name(
            BATTERY_PROFILE_NAMES, data["battery_profile_code"]
        )
    if "charge_stage_code" in data:
        data["charge_stage"] = _name(CHARGE_STAGE_NAMES, data["charge_stage_code"])
    if "ai_modulation_hint_code" in data:
        data["ai_modulation_hint"] = _name(
            MODULATIONS, data["ai_modulation_hint_code"]
        )
    pin = data.get("pin_w")
    pout = data.get("pout_w")
    try:
        if pin is not None and pout is not None and float(pin) > 1.0:
            data["board_efficiency_pct"] = 100.0 * float(pout) / float(pin)
    except (TypeError, ValueError, ZeroDivisionError):
        pass
    data.setdefault("received_at", time.time())
    return data


def normalize_algorithm_detail(family: str, payload: dict[str, Any]) -> dict[str, Any]:
    """Attach readable labels to one VAD/ATI/MSW/CPD telemetry record."""

    data = dict(payload)
    if family == "ATI":
        data["stage"] = _name(AUTOTUNE_STAGE_NAMES, data.get("stage_code"))
        data["failure"] = _name(AUTOTUNE_FAILURE_NAMES, data.get("failure_code"))
    elif family == "MSW":
        data["stage"] = _name(MODSWITCH_STAGE_NAMES, data.get("stage_code"))
        data["failure"] = _name(MODSWITCH_FAILURE_NAMES, data.get("failure_code"))
        data["source_mode"] = _name(MODULATIONS, data.get("source_mode_code"))
        data["target_mode"] = _name(MODULATIONS, data.get("target_mode_code"))
    elif family == "VAD":
        data["band"] = f"BAND_{data.get('band_code', '?')}"
    data.setdefault("received_at", time.time())
    return data


_STATE_RE = re.compile(r"\[DAB\]\[STATE\]\s*([A-Z_]+)")
_READY_RE = re.compile(r"\[DAB\]\[READY\]")
_IDENTITY_RE = re.compile(r"\[DAB\]\[(?:BOOT|VERSION)\]\s*([^;\r\n]+)")


def parse_serial_line(line: str) -> dict[str, Any]:
    """Parse one UART line into a transport-neutral event."""

    clean = line.strip("\r\n\x00")
    if not clean:
        return {"type": "log", "level": "debug", "text": ""}
    try:
        if clean.startswith("DAB:"):
            return {
                "type": "telemetry",
                "source": "serial",
                "data": normalize_telemetry(_decode_csv_payload("DAB:", clean, DAB_FIELDS)),
            }
        if clean.startswith("ALG:"):
            return {
                "type": "telemetry_extension",
                "source": "serial",
                "data": normalize_telemetry(_decode_csv_payload("ALG:", clean, ALG_FIELDS)),
            }
        if clean.startswith(("VAD:", "ATI:", "MSW:", "CPD:")):
            family, payload = clean.split(":", 1)
            values = _decode_csv_payload(
                f"{family}:", clean, ALGORITHM_DETAIL_FIELDS[family]
            )
            return {
                "type": "algorithm_detail",
                "source": "serial",
                "family": family,
                "data": normalize_algorithm_detail(family, values),
                "values": payload.split(","),
                "raw": clean,
            }
    except (ValueError, IndexError) as exc:
        return {"type": "log", "level": "warning", "text": f"解析失败: {clean} ({exc})"}

    identity = _IDENTITY_RE.search(clean)
    if identity:
        return {
            "type": "identity",
            "source": "serial",
            "firmware": identity.group(1).strip(),
        }
    state = _STATE_RE.search(clean)
    if state:
        return {
            "type": "status",
            "source": "serial",
            "data": {"state": state.group(1), "received_at": time.time()},
        }
    if _READY_RE.search(clean):
        return {
            "type": "status",
            "source": "serial",
            "data": {"state": "READY", "received_at": time.time()},
        }
    level = "error" if ("[FAULT" in clean or "[BLOCK" in clean or "ERROR" in clean) else "info"
    return {"type": "log", "level": level, "text": clean}


def parse_jsonl_line(line: bytes | str) -> dict[str, Any]:
    if isinstance(line, bytes):
        line = line.decode("utf-8")
    message = json.loads(line)
    message_type = message.get("type")
    if message_type == "telemetry":
        return {
            "type": "telemetry",
            "source": "ethernet",
            "data": normalize_telemetry(message),
        }
    if message_type == "response" and isinstance(message.get("status"), dict):
        status = normalize_telemetry(message["status"])
        return {
            "type": "status",
            "source": "ethernet",
            "data": status,
            "response": message,
        }
    if message_type == "hello":
        return {**message, "type": "identity", "source": "ethernet"}
    return {"type": "protocol", "source": "ethernet", "message": message}


_FIRMWARE_TIER_RE = re.compile(r"(?:^|[-_])v(\d+)([a-z]?)", re.IGNORECASE)


def infer_device_capabilities(
    source: str,
    *,
    firmware: str | None = None,
    protocol: str | None = None,
    can_version: int | None = None,
) -> dict[str, Any]:
    """Describe capabilities proven by the observed identity/protocol.

    This is deliberately conservative: it reports protocol-level support and
    version-derived feature tiers, but does not claim that a physical link or
    power stage has been validated.
    """

    firmware_text = (firmware or "").strip()
    protocol_text = (protocol or "").strip()
    features: list[str]
    compatible = True

    if source == "simulator":
        identity = firmware_text or "DEMO-SIMULATOR"
        features = ["五种运行模式", "高级算法模拟状态", "本地趋势与导出"]
    elif source == "serial":
        identity = firmware_text or "串口设备（版本待识别）"
        features = ["文本命令", "DAB/ALG 遥测", "模式配置与显式启停"]
        match = _FIRMWARE_TIER_RE.search(firmware_text)
        if match:
            tier = int(match.group(1))
            if tier >= 44:
                features.append("VAD/ATI/MSW/CPD")
            if tier >= 45:
                features.append("参数调度表")
            if tier >= 46:
                features.append("CAN/Ethernet 固件族")
    elif source == "ethernet":
        identity = firmware_text or protocol_text or "Ethernet 设备（版本待识别）"
        features = ["TCP JSONL 遥测", "状态/心跳", "电压模式参数与显式启停"]
        if protocol_text and protocol_text != "dab-jsonl/1":
            compatible = False
    elif source == "can":
        version = can_version if can_version is not None else "?"
        identity = f"Classic CAN protocol v{version}"
        features = ["状态/测量", "CP 目标", "ACK/STOP", "无 START"]
        if can_version is not None and can_version != CAN_PROTOCOL_VERSION:
            compatible = False
    else:
        identity = firmware_text or protocol_text or source or "未知设备"
        features = ["能力未知"]

    return {
        "source": source,
        "identity": identity,
        "firmware": firmware_text,
        "protocol": protocol_text,
        "features": features,
        "summary": " · ".join(features),
        "compatible": compatible,
    }


CAN_PROTOCOL_VERSION = 1
CAN_ID_STATUS = 0x180
CAN_ID_MEASUREMENTS = 0x181
CAN_ID_ACK = 0x182
CAN_ID_COMMAND = 0x200
CAN_OP_PING = 0x01
CAN_OP_GET_STATUS = 0x02
CAN_OP_STOP = 0x10
CAN_OP_SET_CP_TARGET = 0x20

CAN_RESULT_NAMES = {
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


def can_command_payload(opcode: int, sequence: int, cp_target_w: float | None = None) -> bytes:
    data = bytearray(8)
    data[0:3] = bytes((CAN_PROTOCOL_VERSION, opcode & 0xFF, sequence & 0xFF))
    if opcode == CAN_OP_SET_CP_TARGET:
        if cp_target_w is None or not 0.0 <= cp_target_w <= 6553.5:
            raise ValueError("CP target must fit the CAN 0.1 W uint16 encoding")
        data[3:5] = struct.pack("<H", round(cp_target_w * 10.0))
    return bytes(data)


def decode_can_frame(arbitration_id: int, data: bytes) -> dict[str, Any]:
    if len(data) != 8:
        return {
            "type": "protocol",
            "source": "can",
            "error": "expected_dlc_8",
            "id": hex(arbitration_id),
            "raw": data.hex(),
        }
    if arbitration_id == CAN_ID_ACK:
        version, opcode, sequence, result, state, architecture, rx_drop, errors = data
        return {
            "type": "ack",
            "source": "can",
            "version": version,
            "opcode": opcode,
            "sequence": sequence,
            "result": CAN_RESULT_NAMES.get(result, f"unknown_{result}"),
            "state": _name(STATE_NAMES, state),
            "control_architecture": _name(ARCHITECTURE_NAMES, architecture),
            "rx_dropped_low8": rx_drop,
            "error_events_low8": errors,
        }
    if arbitration_id == CAN_ID_STATUS:
        faults = data[5] | (data[6] << 8) | (data[7] << 16)
        flags = data[4]
        status = {
            "state_code": data[1],
            "control_architecture_code": data[2],
            "modulation_effective_code": data[3],
            "valid": bool(flags & 0x01),
            "control_enabled": bool(flags & 0x02),
            "pwm_enabled": bool(flags & 0x04),
            "test_mode": bool(flags & 0x08),
            "can_bus_off": bool(flags & 0x10),
            "cp_damping_enabled": bool(flags & 0x20),
            "seed_loaded": bool(flags & 0x40),
            "software_auto_trip_enabled": bool(flags & 0x80),
            "fault_mask": faults,
        }
        return {"type": "status", "source": "can", "data": normalize_telemetry(status)}
    if arbitration_id == CAN_ID_MEASUREMENTS:
        vin_dv, vout_dv, iout_ca, pout_dw = struct.unpack("<HHhh", data)
        measurements = {
            "vin_v": vin_dv / 10.0,
            "vout_v": vout_dv / 10.0,
            "iout_a": iout_ca / 100.0,
            "pout_w": pout_dw / 10.0,
            "valid": True,
        }
        return {
            "type": "telemetry",
            "source": "can",
            "data": normalize_telemetry(measurements),
        }
    return {
        "type": "protocol",
        "source": "can",
        "id": hex(arbitration_id),
        "raw": data.hex(),
    }


@dataclass(slots=True)
class DemoConfig:
    personality: str = PERSONALITY_CHARGE
    modulation: str = "SPS"
    target_voltage_v: float = 42.0
    target_current_a: float = 2.0
    target_power_w: float = 100.0
    power_limit_w: float = 100.0
    battery_profile: str = "liion10"
    mppt_algorithm: str = "po"
    solar_with_charger: bool = True
    cp_damping: bool = True
    vin_set_v: float = 24.0
    load_ohm: float = 18.0
    extras: dict[str, Any] = field(default_factory=dict)

    def validate(self) -> None:
        if self.personality not in PERSONALITIES:
            raise ValueError(f"未知运行模式: {self.personality}")
        if self.modulation.upper() not in MODULATIONS:
            raise ValueError(f"unknown modulation: {self.modulation}")
        if self.mppt_algorithm not in MPPT_ALGORITHMS:
            raise ValueError("MPPT算法只允许P&O或增量电导")
        if not 36.0 <= self.target_voltage_v <= 48.0:
            raise ValueError("目标电压必须在 36..48 V")
        if not 0.0 <= self.target_current_a <= 18.0:
            raise ValueError("目标电流必须在 0..18 A")
        if not 20.0 <= self.target_power_w <= 360.0:
            raise ValueError("目标功率必须在 20..360 W")
        if not 20.0 <= self.power_limit_w <= 360.0:
            raise ValueError("功率上限必须在 20..360 W")
        if self.target_power_w > self.power_limit_w:
            raise ValueError("目标功率不能高于功率上限")
        if not 18.0 <= self.vin_set_v <= 24.0:
            raise ValueError("标称输入电压必须在 18..24 V")
        if not 1.0 <= self.load_ohm <= 100.0:
            raise ValueError("标称负载必须在 1..100 Ω")


@dataclass(slots=True)
class CommandPlan:
    commands: list[Any]
    warnings: list[str] = field(default_factory=list)
    supported: bool = True


def serial_configuration_plan(config: DemoConfig) -> CommandPlan:
    """Translate one high-level configuration to existing UART commands.

    The plan never contains a power-start command.  For smart charging the
    upper computer sends only a battery profile and the P&O/INC selection;
    voltage/current targets, MPPT range/search parameters, architecture,
    modulation, partitions, limits and transitions remain lower-firmware
    responsibilities.
    """

    config.validate()
    commands: list[str] = ["ai off"]
    warnings: list[str] = []

    if (config.personality not in (PERSONALITY_CHARGE, PERSONALITY_CP) and
            config.power_limit_w > 100.0):
        warnings.append(
            "功率上限高于 100 W；普通模式应用不会自动解锁。若板端处于 100 W 锁定状态，请单独执行工程解锁。"
        )
    if config.personality == PERSONALITY_CHARGE:
        if config.battery_profile not in VALIDATED_CHARGE_PROFILES:
            raise ValueError(f"未经实测的电池类型: {config.battery_profile}")
        commands.extend(
            [
                f"bat profile {config.battery_profile}",
                f"mppt {config.mppt_algorithm}",
            ]
        )
    elif config.personality == PERSONALITY_CP:
        commands.extend(
            [
                "arch cp",
                f"p {config.power_limit_w:.1f}",
                f"vref {config.target_voltage_v:.2f}",
                f"cpdamp vnom {config.target_voltage_v:.2f}",
                f"cpdamp target {config.target_power_w:.1f}",
                f"cpdamp {'on' if config.cp_damping else 'off'}",
            ]
        )
    elif config.personality == PERSONALITY_MPPT:
        commands.extend(
            [
                "arch energy",
                "energy mppt",
                f"mppt {config.mppt_algorithm}",
                "sched auto",
            ]
        )
    elif config.personality == PERSONALITY_VOLTAGE:
        mod = config.modulation.lower()
        commands.extend(
            [
                f"mod {mod}",
                f"p {config.power_limit_w:.1f}",
                "arch voltage",
                f"vref {config.target_voltage_v:.2f}",
                f"rload {config.load_ohm:.2f}",
            ]
        )
    commands.extend(["alglog on", "mode save", "show"])
    return CommandPlan(commands, warnings)


def serial_engineering_unlock_plan(limit_w: float = 400.0) -> CommandPlan:
    """Build the explicit UART high-power unlock action."""

    if abs(float(limit_w) - 400.0) > 0.01:
        raise ValueError("当前固件只支持显式 unlock 400")
    return CommandPlan(["unlock 400"])


def serial_engineering_lock_plan() -> CommandPlan:
    """Restore the firmware's normal 100 W / 320 ns command ceiling."""

    return CommandPlan(["lock"])


def serial_start_plan(config: DemoConfig) -> CommandPlan:
    """Return the explicit start sequence for an already applied mode."""

    if config.personality == PERSONALITY_CHARGE:
        return CommandPlan(["bat start"])
    return CommandPlan(["on"])


def serial_stop_plan(config: DemoConfig | None = None) -> CommandPlan:
    if config and config.personality == PERSONALITY_CHARGE:
        return CommandPlan(["bat stop"])
    return CommandPlan(["off"])


def ethernet_configuration_plan(config: DemoConfig) -> CommandPlan:
    """Use only fields actually implemented by the current TCP protocol."""

    config.validate()
    commands: list[dict[str, Any]] = [
        {"cmd": "set_parameter", "name": "modulation", "value": config.modulation.lower()},
        {"cmd": "set_parameter", "name": "power_limit_w", "value": config.power_limit_w},
        {"cmd": "set_parameter", "name": "voltage_ref_v", "value": config.target_voltage_v},
        {"cmd": "set_parameter", "name": "bench_vin_set_v", "value": config.vin_set_v},
    ]
    warnings: list[str] = []
    supported = True

    if config.personality == PERSONALITY_CHARGE:
        commands.append(
            {"cmd": "set_parameter", "name": "load_mode", "value": "battery_load"}
        )
        warnings.append("当前 Ethernet v1 尚未暴露电池 profile/energy 模式；已配置电池标签与公共目标。")
        supported = False
    elif config.personality == PERSONALITY_CP:
        commands.append(
            {"cmd": "set_parameter", "name": "requested_power_layer_w", "value": config.target_power_w}
        )
        warnings.append("当前 Ethernet v1 尚未开放 CP 架构/CP 目标；requested_power_layer 仅是工况标签。")
        supported = False
    elif config.personality == PERSONALITY_MPPT:
        warnings.append("当前 Ethernet v1 尚未开放 MPPT/energy 命令；只配置公共目标并显示遥测。")
        supported = False
    elif config.personality == PERSONALITY_VOLTAGE:
        commands.extend(
            [
                {"cmd": "set_parameter", "name": "control_architecture", "value": "single"},
                {"cmd": "set_parameter", "name": "load_mode", "value": "resistive_load"},
                {"cmd": "set_parameter", "name": "load_nominal_ohm", "value": config.load_ohm},
            ]
        )
    commands.append({"cmd": "get_status"})
    return CommandPlan(commands, warnings, supported)


def can_configuration_plan(config: DemoConfig, sequence: int) -> CommandPlan:
    config.validate()
    if config.personality != PERSONALITY_CP:
        return CommandPlan(
            [],
            ["CAN v1 只支持设置 CP 目标；其他运行模式可查看状态并执行 STOP。"],
            False,
        )
    if config.modulation.upper() != "SPS":
        warning = "CAN v1 不支持改变调制方式；板端将保留当前有效调制。"
    else:
        warning = ""
    warnings = [warning] if warning else []
    if config.target_power_w > 100.0:
        warnings.append("CAN 目标高于 100 W；只有板端现有高功率解锁已生效时才会被接受。")
    payload = can_command_payload(CAN_OP_SET_CP_TARGET, sequence, config.target_power_w)
    return CommandPlan([(CAN_ID_COMMAND, payload)], warnings, True)


def jsonl_encode(request_id: int, command: dict[str, Any]) -> bytes:
    message = {"id": request_id, **command}
    return (json.dumps(message, separators=(",", ":"), ensure_ascii=False) + "\n").encode(
        "utf-8"
    )


def merge_telemetry(base: dict[str, Any], update: dict[str, Any]) -> dict[str, Any]:
    merged = dict(base)
    merged.update(update)
    return normalize_telemetry(merged)


def iter_command_labels(commands: Iterable[Any]) -> Iterable[str]:
    for command in commands:
        if isinstance(command, str):
            yield command
        elif isinstance(command, dict):
            yield json.dumps(command, ensure_ascii=False, separators=(",", ":"))
        elif isinstance(command, tuple) and len(command) == 2:
            arbitration_id, data = command
            yield f"CAN {arbitration_id:#05x} {bytes(data).hex(' ')}"
        else:
            yield repr(command)
