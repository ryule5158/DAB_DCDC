#!/usr/bin/env python3

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path

from easyeda_netlist_extract import attrs_by_parent, load_documents


ROOT = Path(r"C:\Users\LENOVO\Desktop\Electric Simulation")
EPRO2 = Path(r"C:\Users\LENOVO\Desktop\ProPrj_瑞萨杯_2026-07-21.epro2")
EPRU = ROOT / "_cn2_final_source_20260721" / "瑞萨杯.epru"
PINS_CSV = ROOT / "_cn2_final_source_20260721" / "parsed" / "pins.csv"
BOARD_AUDIT = ROOT / "_ra8p1_board_native_20260718" / "board_native_audit.json"
RA_CFG = Path(r"C:\Users\LENOVO\e2_studio\workspace\RA8P1_Project1\ra_cfg.txt")
OUTPUT = ROOT / "_cn2_final_source_20260721" / "cn2_final_audit.json"


ASSIGNMENTS = {
    "SEC_HW_OK_CTRL": ("PB01", "P6.19", "GPIO output", "new"),
    "OUT_PROT_EN": ("P706", "P6.15", "GPIO output", "tested"),
    "ADC_IOUT": ("P008", "P5.8", "ADC input", "tested"),
    "ADC_VOUT": ("P007", "P5.6", "ADC input", "tested"),
    "ADC_TEMP_SEC": ("P004", "P5.11", "ADC input", "tested"),
    "ISEC_OC_N": ("P403", "P7.11", "fault input", "tested"),
    "ADC_ISEC": ("P006", "P5.4", "ADC input", "tested"),
    "PWM_SEC_A_H": ("P915", "P7.19", "GPT output", "tested"),
    "PWM_SEC_A_L": ("P914", "P7.2", "GPT output", "tested"),
    "PWM_SEC_B_H": ("P605", "P6.11", "GPT output", "tested"),
    "PWM_SEC_B_L": ("P604", "P6.9", "GPT output", "tested"),
    "RAW_FAULT_N2": ("P401", "P8.8", "POEG0 fault input (wired-OR)", "tested"),
    "RUN_REQ": ("P707", "P6.17", "GPIO output", "tested"),
    "RAW_FAULT_N1": ("P401", "P8.8", "POEG0 fault input (wired-OR)", "tested"),
    "PROT_RESET_N": ("PB03", "P6.16", "GPIO output", "tested"),
    "2adcaa01:N$131": ("P402", "P4.1", "GPIO output to LED10", "tested"),
    "ADC_IPRI": ("P000", "P5.3", "ADC input", "tested"),
    "IPRI_OC_N": ("P400", "P8.5", "fault input", "tested"),
    "PWM_PRI_A_H": ("P415", "P8.4", "GPT output", "tested"),
    "PWM_PRI_A_L": ("P414", "P8.13", "GPT output", "tested"),
    "PWM_PRI_B_H": ("P105", "P7.1", "GPT output", "tested"),
    "PWM_PRI_B_L": ("P104", "P7.3", "GPT output", "tested"),
    "ADC_TEMP_PRI": ("P009", "P5.10", "ADC input", "tested"),
    "ADC_VAFE_HALF": ("P003", "P5.9", "ADC input", "tested"),
    "ADC_VIN": ("P001", "P5.5", "ADC input", "tested"),
    "ADC_IIN": ("P002", "P5.7", "ADC input", "tested"),
    "PRI_HW_OK_CTRL": ("PB06", "P6.18", "GPIO output", "new"),
    "IN_PRECHARGE_EN": ("PB00", "P6.13", "GPIO output", "tested"),
    "IN_MAIN_EN": ("PB02", "P6.14", "GPIO output", "tested"),
    "CAN_RX_MCU": ("P705", "P6.21", "CANFD0 CRX0 input", "new"),
    "CAN_TX_MCU": ("P704", "P4.2", "CANFD0 CTX0 output", "new"),
    "ETH_TXD0": ("P307", "P6.28", "ESWM_RMII1 TXD0 output", "new"),
    "ETH_TXD1": ("P306", "P4.13", "ESWM_RMII1 TXD1 output", "new"),
    "ETH_TX_EN": ("P310", "P6.35", "ESWM_RMII1 TX_EN output", "new"),
    "ETH_REF50CK": ("P905", "P6.33", "ESWM_RMII1 REF50CK input", "new"),
    "ETH_RXD0": ("P906", "P6.30", "ESWM_RMII1 RXD0 input", "new"),
    "ETH_RXD1": ("P907", "P6.22", "ESWM_RMII1 RXD1 input", "new"),
    "ETH_CRS_DV": ("P206", "P4.11", "ESWM_RMII1 CRS_DV input", "new"),
    "ETH_MDC": ("P806", "P6.34", "ESWM_RMII1 MDC output", "new"),
    "ETH_MDIO": ("P805", "P6.36", "ESWM_RMII1 MDIO bidirectional", "new"),
    "ETH_PHY_RESET_N": ("P513", "P6.32", "GPIO output", "new"),
    "ETH_RX_ER": ("P312", "P6.37", "ESWM_RMII1 RX_ER input", "new"),
    "FAN": ("PB04", "P6.12", "GPIO output", "tested"),
    "TEMP_AMB_OT_N": ("PB05", "P6.20", "fault input", "new"),
    "3V3_RA": ("VCC", "P9.1", "3.3 V power", "power"),
    "I2C_SCL": ("P515", "P4.4", "IIC2 SCL bidirectional", "tested"),
    "I2C_SDA": ("P514", "P4.3", "IIC2 SDA bidirectional", "tested"),
}


EXPECTED_MUX = {
    "P705": "CANFD0: CRX0",
    "P704": "CANFD0: CTX0",
    "P307": "ESWM_RMII1: RMII1_TXD0",
    "P306": "ESWM_RMII1: RMII1_TXD1",
    "P310": "ESWM_RMII1: RMII1_TX_EN",
    "P905": "ESWM_RMII1: RMII1_REF50CK",
    "P906": "ESWM_RMII1: RMII1_RXD0",
    "P907": "ESWM_RMII1: RMII1_RXD1",
    "P206": "ESWM_RMII1: RMII1_CRS_DV",
    "P806": "ESWM_RMII1: ET1_MDC",
    "P805": "ESWM_RMII1: ET1_MDIO",
    "P312": "ESWM_RMII1: RMII1_RX_ER",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_cn2_schematic() -> dict[int, dict[str, str]]:
    result = {}
    with PINS_CSV.open("r", encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["designator"] == "CN2":
                result[int(row["pin_number"])] = row
    if sorted(result) != list(range(1, 121)):
        raise RuntimeError(f"CN2 schematic pins incomplete: {len(result)}")
    return result


def load_cn2_pcb() -> list[dict[str, object]]:
    hits = []
    for doc in load_documents(EPRU):
        if doc["doc_type"] != "PCB":
            continue
        attrs = attrs_by_parent(doc["records"])
        designator_by_component = {}
        for record in doc["records"]:
            if record["type"] != "COMPONENT":
                continue
            merged = {**(record["data"].get("attrs") or {}), **attrs.get(str(record["id"]), {})}
            designator = str(merged.get("Designator") or "")
            if designator:
                designator_by_component[str(record["id"])] = designator
        all_pads = []
        for record in doc["records"]:
            if record["type"] != "PAD_NET":
                continue
            try:
                key = json.loads(str(record["id"]))
            except json.JSONDecodeError:
                continue
            if len(key) >= 3:
                try:
                    pad_number = int(key[2])
                except (TypeError, ValueError):
                    continue
                all_pads.append((str(key[1]), pad_number, str(record["data"].get("padNet") or "")))
        component_ids = [component_id for component_id, designator in designator_by_component.items() if designator == "CN2"]
        for component_id in component_ids:
            pads = {number: net for cid, number, net in all_pads if cid == component_id}
            cn2_nets = set(pads.values())
            net_members = {}
            for cid, number, net in all_pads:
                if net in cn2_nets:
                    designator = designator_by_component.get(cid, cid)
                    net_members.setdefault(net, []).append(f"{designator}.{number}")
            for members in net_members.values():
                members.sort()
            hits.append({"pcb_uuid": doc["uuid"], "component_id": component_id, "pads": pads, "net_members": net_members})
    return hits


def validate_board_breakout() -> list[dict[str, object]]:
    board = json.loads(BOARD_AUDIT.read_text(encoding="utf-8"))
    pin_map = board["pinMap"]
    pcb_map = board["pcbPinMap"]
    result = []
    for net, (ra_pin, header, function, status) in ASSIGNMENTS.items():
        header_ref, header_pin = header.split(".")
        sch_net = str(pin_map.get(header_ref, {}).get(header_pin, {}).get("net") or "")
        pcb_net = str(pcb_map.get(header_ref, {}).get(header_pin) or "")
        expected = "VCC" if ra_pin == "VCC" else ra_pin
        result.append({
            "network": net,
            "ra_pin": ra_pin,
            "header": header,
            "function": function,
            "status": status,
            "devboard_schematic_net": sch_net,
            "devboard_pcb_net": pcb_net,
            "schematic_match": sch_net == expected,
            "pcb_match": pcb_net == expected,
        })
    return result


def main() -> None:
    schematic = load_cn2_schematic()
    pcb_hits = load_cn2_pcb()
    if len(pcb_hits) != 1:
        raise RuntimeError(f"Expected one PCB CN2, found {len(pcb_hits)}")
    pcb = pcb_hits[0]
    if sorted(pcb["pads"]) != list(range(1, 121)):
        raise RuntimeError(f"CN2 PCB pads incomplete: {len(pcb['pads'])}")

    named_nets = {row["net"] for row in schematic.values() if row["net"] not in {"GND", "3V3_RA"}}
    missing_assignments = sorted(named_nets - set(ASSIGNMENTS))
    unused_assignments = sorted(set(ASSIGNMENTS) - named_nets - {"3V3_RA"})
    board_rows = validate_board_breakout()
    cfg_lines = RA_CFG.read_text(encoding="utf-8", errors="replace").splitlines()
    mux_checks = []
    for pin, function in EXPECTED_MUX.items():
        line = next((item.strip() for item in cfg_lines if item.lstrip().startswith(pin + " ")), "")
        mux_checks.append({"pin": pin, "expected_function": function, "found": function in line, "cfg_line": line})

    rows = []
    for number, row in sorted(schematic.items()):
        net = row["net"]
        assignment = ASSIGNMENTS.get(net)
        rows.append({
            "cn2_pin": number,
            "schematic_net": net,
            "pcb_net": pcb["pads"].get(number, ""),
            "assigned_ra_pin": assignment[0] if assignment else ("GND" if net == "GND" else ""),
            "header": assignment[1] if assignment else ("P10.1 (common ground)" if net == "GND" else ""),
            "function": assignment[2] if assignment else ("ground" if net == "GND" else ""),
            "status": assignment[3] if assignment else ("power" if net == "GND" else ""),
        })

    named_pcb_mismatches = [
        {"cn2_pin": row["cn2_pin"], "schematic_net": row["schematic_net"], "pcb_net": row["pcb_net"]}
        for row in rows
        if not row["schematic_net"].startswith("2adcaa01:N$") and row["schematic_net"] != row["pcb_net"]
    ]
    pin51_members = pcb["net_members"].get(pcb["pads"][51], [])

    result = {
        "generated_at": "2026-07-22",
        "source_epro2": str(EPRO2),
        "source_epro2_sha256": sha256(EPRO2),
        "source_epru_sha256": sha256(EPRU),
        "developer_board_source": r"C:\Users\LENOVO\Desktop\瑞萨CPKHMI-RA8P1扩展板 (1).eprj2",
        "developer_board_sha256": json.loads(BOARD_AUDIT.read_text(encoding="utf-8"))["inputSha256"],
        "schematic_page_uuid": "2adcaa011103195a",
        "pcb_uuid": pcb["pcb_uuid"],
        "cn2_component_id": pcb["component_id"],
        "counts": {
            "cn2_pins": len(rows),
            "ground_pins": sum(row["schematic_net"] == "GND" for row in rows),
            "power_pins": sum(row["schematic_net"] == "3V3_RA" for row in rows),
            "signal_pins": sum(row["schematic_net"] not in {"GND", "3V3_RA"} for row in rows),
        },
        "missing_assignments": missing_assignments,
        "unused_assignments": unused_assignments,
        "named_pcb_mismatches": named_pcb_mismatches,
        "pin51_pcb_net_members": pin51_members,
        "pin51_topology_valid": "CN2.51" in pin51_members and "R49.2" in pin51_members,
        "developer_board_breakout": board_rows,
        "all_breakouts_valid": all(row["schematic_match"] and row["pcb_match"] for row in board_rows),
        "mux_checks": mux_checks,
        "all_mux_functions_valid": all(row["found"] for row in mux_checks),
        "cn2_rows": rows,
    }
    OUTPUT.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "output": str(OUTPUT),
        "counts": result["counts"],
        "missing_assignments": missing_assignments,
        "unused_assignments": unused_assignments,
        "named_pcb_mismatches": named_pcb_mismatches,
        "pin51_topology_valid": result["pin51_topology_valid"],
        "all_breakouts_valid": result["all_breakouts_valid"],
        "all_mux_functions_valid": result["all_mux_functions_valid"],
        "pcb_uuid": pcb["pcb_uuid"],
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
