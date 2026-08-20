from __future__ import annotations

import unittest

from dab_console.ui import (
    BATTERY_LABELS,
    BATTERY_STAGE_HINTS,
    DEFAULT_PERSONALITY_LABEL,
    MPPT_LABELS,
    PERSONALITY_LABELS,
    SERIAL_TRANSPORT_LABEL,
)


class PlannedInterfaceScopeTests(unittest.TestCase):
    def test_only_delivered_function_is_exposed(self) -> None:
        self.assertEqual(
            tuple(PERSONALITY_LABELS),
            ("智能充电 + MPPT",),
        )

    def test_fixed_power_is_not_exposed_as_mppt(self) -> None:
        self.assertEqual(tuple(MPPT_LABELS), ("P&O 扰动观察", "增量电导"))

    def test_fused_charge_mppt_is_the_valid_default(self) -> None:
        self.assertEqual(DEFAULT_PERSONALITY_LABEL, "智能充电 + MPPT")
        self.assertIn(DEFAULT_PERSONALITY_LABEL, PERSONALITY_LABELS)

    def test_only_two_tested_battery_profiles_are_exposed(self) -> None:
        self.assertEqual(
            tuple(BATTERY_LABELS),
            ("10S 锂离子（已实测）", "12S 磷酸铁锂（已实测）"),
        )

    def test_each_battery_profile_has_a_stage_hint(self) -> None:
        self.assertEqual(set(BATTERY_STAGE_HINTS), set(BATTERY_LABELS.values()))

    def test_field_interface_is_serial_only(self) -> None:
        self.assertEqual(SERIAL_TRANSPORT_LABEL, "串口 UART")


if __name__ == "__main__":
    unittest.main()
