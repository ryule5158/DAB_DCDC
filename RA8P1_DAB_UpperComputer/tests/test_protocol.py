from __future__ import annotations

import struct
import unittest

from dab_console.protocol import (
    ALG_FIELDS,
    CAN_ID_ACK,
    CAN_ID_MEASUREMENTS,
    CAN_ID_STATUS,
    CAN_OP_SET_CP_TARGET,
    DAB_FIELDS,
    DemoConfig,
    PERSONALITY_CHARGE,
    PERSONALITY_CP,
    PERSONALITY_MPPT,
    can_command_payload,
    can_configuration_plan,
    decode_can_frame,
    ethernet_configuration_plan,
    infer_device_capabilities,
    parse_jsonl_line,
    parse_serial_line,
    serial_configuration_plan,
    serial_engineering_lock_plan,
    serial_engineering_unlock_plan,
    serial_start_plan,
    serial_stop_plan,
)


class SerialProtocolTests(unittest.TestCase):
    def test_dab_csv_schema(self) -> None:
        values = ["1"] * len(DAB_FIELDS)
        event = parse_serial_line("DAB:" + ",".join(values))
        self.assertEqual(event["type"], "telemetry")
        self.assertEqual(event["data"]["state"], "READY")
        self.assertEqual(event["data"]["modulation"], "EPS")
        self.assertEqual(event["data"]["control_architecture"], "OUTPUT_CURRENT_PI")

    def test_alg_csv_schema(self) -> None:
        values = ["1"] * len(ALG_FIELDS)
        event = parse_serial_line("ALG:" + ",".join(values))
        self.assertEqual(event["type"], "telemetry_extension")
        self.assertEqual(event["data"]["energy_mode"], "MANUAL_CURRENT")
        self.assertEqual(event["data"]["charge_stage"], "IDLE")

    def test_text_identity(self) -> None:
        event = parse_serial_line("[DAB][BOOT] 2026-08-17-dualcore-autoschedule-v45; UART")
        self.assertEqual(event["firmware"], "2026-08-17-dualcore-autoschedule-v45")

    def test_version_query_identity(self) -> None:
        event = parse_serial_line("[DAB][VERSION] 2026-08-18-dualcore-uart-exactseed-v47")
        self.assertEqual(event["type"], "identity")
        self.assertEqual(event["firmware"], "2026-08-18-dualcore-uart-exactseed-v47")

    def test_ready_banner_does_not_treat_adc_as_state(self) -> None:
        event = parse_serial_line("[DAB][READY] ADC0+ADC1 DMA=1000 frame/s")
        self.assertEqual(event["data"]["state"], "READY")

    def test_advanced_algorithm_records_are_typed(self) -> None:
        vad = parse_serial_line(
            "VAD:7,1,1,24.125,2,1.02,5.1,0.82,0.6,2.5,70.0,280.0,1,0,0"
        )
        self.assertEqual(vad["type"], "algorithm_detail")
        self.assertTrue(vad["data"]["enabled"])
        self.assertEqual(vad["data"]["band"], "BAND_2")
        ati = parse_serial_line(
            "ATI:8,4,0,12,0,2.1,1.7,0.016,-1,5.0,0.8,1.0,160,1,1,0"
        )
        self.assertEqual(ati["data"]["stage"], "COMPLETE")
        self.assertEqual(ati["data"]["failure"], "NONE")
        msw = parse_serial_line("MSW:9,3,0,2,24,16,100,0.5,180,1,-1,0")
        self.assertEqual(msw["data"]["source_mode"], "SPS")
        self.assertEqual(msw["data"]["target_mode"], "TPS")
        cpd = parse_serial_line(
            "CPD:10,1,100,99,1,0.01,0.002,0.015,44,2.2,0.1,0.2,2.5,0"
        )
        self.assertFalse(cpd["data"]["saturated"])


class EthernetProtocolTests(unittest.TestCase):
    def test_telemetry_jsonl(self) -> None:
        event = parse_jsonl_line(
            b'{"type":"telemetry","seq":9,"state":"READY","vin_v":24.0,"valid":true}'
        )
        self.assertEqual(event["type"], "telemetry")
        self.assertEqual(event["data"]["seq"], 9)

    def test_hello_is_identity_event(self) -> None:
        event = parse_jsonl_line(b'{"type":"hello","protocol":"dab-jsonl/1"}')
        self.assertEqual(event["type"], "identity")
        self.assertEqual(event["protocol"], "dab-jsonl/1")

class CanProtocolTests(unittest.TestCase):
    def test_cp_encoding(self) -> None:
        payload = can_command_payload(CAN_OP_SET_CP_TARGET, 7, 200.0)
        self.assertEqual(payload[0:3], bytes((1, CAN_OP_SET_CP_TARGET, 7)))
        self.assertEqual(payload[3:5], b"\xd0\x07")

    def test_measurement_decoding(self) -> None:
        event = decode_can_frame(CAN_ID_MEASUREMENTS, struct.pack("<HHhh", 234, 410, -125, 2074))
        self.assertEqual(event["data"]["vin_v"], 23.4)
        self.assertEqual(event["data"]["iout_a"], -1.25)
        self.assertEqual(event["data"]["pout_w"], 207.4)

    def test_status_and_ack_decoding(self) -> None:
        status = decode_can_frame(CAN_ID_STATUS, bytes((1, 7, 3, 2, 0x67, 0, 0, 0)))
        self.assertEqual(status["data"]["state"], "RUN")
        self.assertEqual(status["data"]["modulation"], "TPS")
        self.assertTrue(status["data"]["cp_damping_enabled"])
        ack = decode_can_frame(CAN_ID_ACK, bytes((1, 0x10, 9, 0, 1, 3, 0, 0)))
        self.assertEqual(ack["result"], "ok")
        self.assertEqual(ack["sequence"], 9)


class CommandPlanTests(unittest.TestCase):
    def test_serial_charge_plan_arms_schedule_without_starting(self) -> None:
        plan = serial_configuration_plan(DemoConfig())
        self.assertEqual(plan.commands[0], "ai off")
        self.assertIn("bat profile liion10", plan.commands)
        self.assertIn("mppt po", plan.commands)
        self.assertFalse(any(command.startswith("energy ") for command in plan.commands))
        self.assertFalse(any(command.startswith("mppt vmin ") for command in plan.commands))
        self.assertFalse(any(command.startswith("mppt vmax ") for command in plan.commands))
        self.assertFalse(any(command.startswith("mppt step ") for command in plan.commands))
        self.assertFalse(any(command.startswith("mppt interval ") for command in plan.commands))
        self.assertFalse(any(command.startswith("mppt pmax ") for command in plan.commands))
        self.assertFalse(any(command.startswith("bat vcv ") for command in plan.commands))
        self.assertFalse(any(command.startswith("bat imax ") for command in plan.commands))
        self.assertNotIn("bat start", plan.commands)
        self.assertNotIn("on", plan.commands)
        forbidden_prefixes = ("mod ", "inner ", "cap ", "ikp ", "iki ",
                              "islew ", "arch ", "sched ", "p ", "vinset ")
        self.assertFalse(any(command.startswith(forbidden_prefixes)
                             for command in plan.commands))

    def test_serial_charge_operator_values_cannot_override_validated_bundle(self) -> None:
        plan = serial_configuration_plan(
            DemoConfig(
                battery_profile="lfp12",
                target_voltage_v=47.0,
                target_current_a=9.0,
            )
        )
        self.assertIn("bat profile lfp12", plan.commands)
        self.assertFalse(any(command.startswith("energy ") for command in plan.commands))
        self.assertFalse(any(command.startswith("bat vcv ") for command in plan.commands))
        self.assertFalse(any(command.startswith("bat imax ") for command in plan.commands))

    def test_serial_mppt_plan_leaves_search_parameters_local(self) -> None:
        config = DemoConfig(
            personality=PERSONALITY_MPPT,
            modulation="SPS",
            target_power_w=40.0,
            power_limit_w=40.0,
            mppt_algorithm="inc",
            solar_with_charger=False,
        )
        plan = serial_configuration_plan(config)
        for command in (
            "ai off",
            "arch energy",
            "energy mppt",
            "mppt inc",
            "sched auto",
        ):
            self.assertIn(command, plan.commands)
        forbidden_prefixes = ("mod ", "p ", "mppt vmin ", "mppt vmax ",
                              "mppt step ", "mppt interval ", "mppt pmax ")
        self.assertFalse(any(command.startswith(forbidden_prefixes)
                             for command in plan.commands))
        self.assertFalse(any(command.startswith("energy solar") for command in plan.commands))

    def test_serial_charge_start_stop_are_single_high_level_commands(self) -> None:
        config = DemoConfig()
        self.assertEqual(serial_start_plan(config).commands, ["bat start"])
        self.assertEqual(serial_stop_plan(config).commands, ["bat stop"])

    def test_serial_charge_rejects_fixed_power_mppt(self) -> None:
        with self.assertRaisesRegex(ValueError, "P&O"):
            serial_configuration_plan(
                DemoConfig(
                    personality=PERSONALITY_CHARGE,
                    battery_profile="lfp12",
                    mppt_algorithm="fixed",
                )
            )

    def test_serial_cp_plan_never_starts(self) -> None:
        config = DemoConfig(
            personality=PERSONALITY_CP,
            modulation="TPS",
            target_power_w=200.0,
            power_limit_w=220.0,
        )
        plan = serial_configuration_plan(config)
        self.assertIn("arch cp", plan.commands)
        self.assertIn("cpdamp vnom 42.00", plan.commands)
        self.assertIn("cpdamp target 200.0", plan.commands)
        self.assertFalse(any(command.startswith("mod ") for command in plan.commands))
        self.assertNotIn("sched auto", plan.commands)
        self.assertNotIn("on", plan.commands)
        self.assertNotIn("unlock 400", plan.commands)
        self.assertFalse(any("不会自动解锁" in warning for warning in plan.warnings))

    def test_engineering_unlock_is_separate_explicit_plan(self) -> None:
        self.assertEqual(serial_engineering_unlock_plan().commands, ["unlock 400"])
        self.assertEqual(serial_engineering_lock_plan().commands, ["lock"])
        with self.assertRaises(ValueError):
            serial_engineering_unlock_plan(200.0)

    def test_bench_metadata_ranges_are_validated(self) -> None:
        with self.assertRaisesRegex(ValueError, "标称输入电压"):
            DemoConfig(vin_set_v=17.9).validate()
        with self.assertRaisesRegex(ValueError, "标称负载"):
            DemoConfig(load_ohm=100.1).validate()

    def test_can_only_configures_cp(self) -> None:
        config = DemoConfig(personality=PERSONALITY_CP, target_power_w=100.0)
        plan = can_configuration_plan(config, 3)
        self.assertTrue(plan.supported)
        self.assertEqual(len(plan.commands), 1)
        self.assertEqual(plan.commands[0][1][2], 3)


class CapabilityTests(unittest.TestCase):
    def test_current_serial_firmware_feature_tier(self) -> None:
        profile = infer_device_capabilities(
            "serial", firmware="2026-08-18-dualcore-cp-can-ethdiag-v46a"
        )
        self.assertTrue(profile["compatible"])
        self.assertIn("VAD/ATI/MSW/CPD", profile["features"])
        self.assertIn("参数调度表", profile["features"])

    def test_protocol_mismatch_is_visible(self) -> None:
        ethernet = infer_device_capabilities("ethernet", protocol="dab-jsonl/2")
        can = infer_device_capabilities("can", can_version=2)
        self.assertFalse(ethernet["compatible"])
        self.assertFalse(can["compatible"])


if __name__ == "__main__":
    unittest.main()
