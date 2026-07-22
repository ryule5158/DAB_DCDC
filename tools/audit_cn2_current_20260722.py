#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from typing import Any

from easyeda_netlist_extract import attrs_by_parent, load_documents, meta_for


ASSIGNMENTS: dict[str, dict[str, str]] = {
    "ETH_RXD0": {"ra_pin": "P906", "header": "P6.30", "direction": "MCU input", "role": "RMII1 RXD0", "status": "new", "mux": "ESWM_RMII1: RMII1_RXD0"},
    "ETH_RX_ER": {"ra_pin": "P312", "header": "P6.37", "direction": "MCU input", "role": "RMII1 RX_ER", "status": "new", "mux": "ESWM_RMII1: RMII1_RX_ER"},
    "ETH_CRS_DV": {"ra_pin": "P206", "header": "P4.11", "direction": "MCU input", "role": "RMII1 CRS_DV", "status": "new", "mux": "ESWM_RMII1: RMII1_CRS_DV"},
    "ETH_RXD1": {"ra_pin": "P907", "header": "P6.22", "direction": "MCU input", "role": "RMII1 RXD1", "status": "new", "mux": "ESWM_RMII1: RMII1_RXD1"},
    "ETH_MDIO": {"ra_pin": "P805", "header": "P6.36", "direction": "bidirectional", "role": "RMII1 MDIO", "status": "new", "mux": "ESWM_RMII1: ET1_MDIO"},
    "ETH_REF50CK": {"ra_pin": "P905", "header": "P6.33", "direction": "MCU input", "role": "RMII1 50 MHz reference clock", "status": "new", "mux": "ESWM_RMII1: RMII1_REF50CK"},
    "ETH_MDC": {"ra_pin": "P806", "header": "P6.34", "direction": "MCU output", "role": "RMII1 MDC", "status": "new", "mux": "ESWM_RMII1: ET1_MDC"},
    "ETH_PHY_RESET_N": {"ra_pin": "P513", "header": "P6.32", "direction": "MCU output", "role": "PHY reset, active low", "status": "new", "mux": ""},
    "ETH_TX_EN": {"ra_pin": "P310", "header": "P6.35", "direction": "MCU output", "role": "RMII1 TX_EN", "status": "new", "mux": "ESWM_RMII1: RMII1_TX_EN"},
    "ETH_TXD0": {"ra_pin": "P307", "header": "P6.28", "direction": "MCU output", "role": "RMII1 TXD0", "status": "new", "mux": "ESWM_RMII1: RMII1_TXD0"},
    "ETH_TXD1": {"ra_pin": "P306", "header": "P4.13", "direction": "MCU output", "role": "RMII1 TXD1", "status": "new", "mux": "ESWM_RMII1: RMII1_TXD1"},
    "ADC_TEMP_SEC": {"ra_pin": "P004", "header": "P5.11", "direction": "MCU analog input", "role": "secondary temperature ADC", "status": "tested", "mux": "ADC0: AN004"},
    "PWM_SEC_A_H": {"ra_pin": "P915", "header": "P7.19", "direction": "MCU output", "role": "GPT5 GTIOC5A", "status": "tested", "mux": "GPT5: GTIOC5A"},
    "PWM_SEC_A_L": {"ra_pin": "P914", "header": "P7.2", "direction": "MCU output", "role": "GPT5 GTIOC5B", "status": "tested", "mux": "GPT5: GTIOC5B"},
    "PWM_SEC_B_H": {"ra_pin": "P605", "header": "P6.11", "direction": "MCU output", "role": "GPT8 GTIOC8A", "status": "tested", "mux": "GPT8: GTIOC8A"},
    "PWM_SEC_B_L": {"ra_pin": "P604", "header": "P6.9", "direction": "MCU output", "role": "GPT8 GTIOC8B", "status": "tested", "mux": "GPT8: GTIOC8B"},
    "OUT_PROT_EN": {"ra_pin": "P706", "header": "P6.15", "direction": "MCU output", "role": "secondary protection enable", "status": "tested", "mux": ""},
    "PROT_RESET_N": {"ra_pin": "PB03", "header": "P6.16", "direction": "MCU output", "role": "protection reset, active low", "status": "tested", "mux": ""},
    "SEC_HW_OK_CTRL": {"ra_pin": "PB01", "header": "P6.19", "direction": "MCU input", "role": "isolated secondary HW_OK feedback", "status": "new", "mux": "IRQ: IRQ12"},
    "ADC_VOUT": {"ra_pin": "P007", "header": "P5.6", "direction": "MCU analog input", "role": "output voltage ADC", "status": "tested", "mux": "ADC1: AN007"},
    "ISEC_OC_N": {"ra_pin": "P403", "header": "P7.11", "direction": "MCU input", "role": "secondary overcurrent, active low", "status": "tested", "mux": "IRQ: IRQ14-DS"},
    "RUN_REQ": {"ra_pin": "P707", "header": "P6.17", "direction": "MCU output", "role": "gate run request", "status": "tested", "mux": ""},
    "RAW_FAULT_N1": {"ra_pin": "P401", "header": "P8.8", "direction": "MCU input", "role": "primary fault into POEG0 wired-OR", "status": "tested", "mux": "GPT_POEGA: GTETRGA", "shared": "RAW_FAULT_WIRED_OR"},
    "RAW_FAULT_N2": {"ra_pin": "P401", "header": "P8.8", "direction": "MCU input", "role": "secondary fault into POEG0 wired-OR", "status": "tested", "mux": "GPT_POEGA: GTETRGA", "shared": "RAW_FAULT_WIRED_OR"},
    "ADC_IIN": {"ra_pin": "P002", "header": "P5.7", "direction": "MCU analog input", "role": "input current ADC", "status": "tested", "mux": "ADC0: AN002"},
    "ADC_IOUT": {"ra_pin": "P008", "header": "P5.8", "direction": "MCU analog input", "role": "output current ADC", "status": "tested", "mux": "ADC1: AN008"},
    "ADC_VAFE_HALF": {"ra_pin": "P003", "header": "P5.9", "direction": "MCU analog input", "role": "analog front-end midpoint ADC", "status": "tested", "mux": "ADC0: AN003"},
    "ADC_IPRI": {"ra_pin": "P000", "header": "P5.3", "direction": "MCU analog input", "role": "primary current ADC", "status": "tested", "mux": "ADC0: AN000"},
    "ADC_ISEC": {"ra_pin": "P006", "header": "P5.4", "direction": "MCU analog input", "role": "secondary current ADC", "status": "tested", "mux": "ADC1: AN006"},
    "IPRI_OC_N": {"ra_pin": "P400", "header": "P8.5", "direction": "MCU input", "role": "primary overcurrent, active low", "status": "tested", "mux": "IRQ: IRQ0"},
    "PWM_PRI_A_L": {"ra_pin": "P414", "header": "P8.13", "direction": "MCU output", "role": "GPT0 GTIOC0B", "status": "tested", "mux": "GPT0: GTIOC0B"},
    "PWM_PRI_A_H": {"ra_pin": "P415", "header": "P8.4", "direction": "MCU output", "role": "GPT0 GTIOC0A", "status": "tested", "mux": "GPT0: GTIOC0A"},
    "PWM_PRI_B_L": {"ra_pin": "P104", "header": "P7.3", "direction": "MCU output", "role": "GPT1 GTIOC1B", "status": "tested", "mux": "GPT1: GTIOC1B"},
    "PWM_PRI_B_H": {"ra_pin": "P105", "header": "P7.1", "direction": "MCU output", "role": "GPT1 GTIOC1A", "status": "tested", "mux": "GPT1: GTIOC1A"},
    "ADC_TEMP_PRI": {"ra_pin": "P009", "header": "P5.10", "direction": "MCU analog input", "role": "primary temperature ADC", "status": "tested", "mux": "ADC1: AN009"},
    "PRI_HW_OK_CTRL": {"ra_pin": "PB06", "header": "P6.18", "direction": "MCU input", "role": "isolated primary HW_OK feedback", "status": "new", "mux": "IRQ: IRQ0"},
    "IN_PRECHARGE_EN": {"ra_pin": "PB00", "header": "P6.13", "direction": "MCU output", "role": "input precharge enable", "status": "tested", "mux": ""},
    "IN_MAIN_EN": {"ra_pin": "PB02", "header": "P6.14", "direction": "MCU output", "role": "input main relay enable", "status": "tested", "mux": ""},
    "ADC_VIN": {"ra_pin": "P001", "header": "P5.5", "direction": "MCU analog input", "role": "input voltage ADC", "status": "tested", "mux": "ADC0: AN001"},
    "CAN_RX_MCU": {"ra_pin": "P705", "header": "P6.21", "direction": "MCU input", "role": "CANFD0 CRX0", "status": "new", "mux": "CANFD0: CRX0"},
    "CAN_TX_MCU": {"ra_pin": "P704", "header": "P4.2", "direction": "MCU output", "role": "CANFD0 CTX0", "status": "new", "mux": "CANFD0: CTX0"},
    "FAN": {"ra_pin": "PB04", "header": "P6.12", "direction": "MCU output", "role": "fan control", "status": "tested", "mux": ""},
    "TEMP_AMB_OT_N": {"ra_pin": "PB05", "header": "P6.20", "direction": "MCU input", "role": "ambient overtemperature alert, active low", "status": "new", "mux": "IRQ: IRQ15"},
    "I2C_SDA": {"ra_pin": "P514", "header": "P4.3", "direction": "bidirectional", "role": "IIC2 SDA2", "status": "tested", "mux": "IIC2: SDA2"},
    "I2C_SCL": {"ra_pin": "P515", "header": "P4.4", "direction": "bidirectional", "role": "IIC2 SCL2", "status": "tested", "mux": "IIC2: SCL2"},
}

PIN_OVERRIDES = {
    75: {"logical_net": "DAB_STATUS_LED", "ra_pin": "P402", "header": "P4.1", "direction": "MCU output", "role": "LED10 status output, active high through R49", "status": "tested", "mux": ""},
}

POWER_HEADERS = {103: "P9.1", 105: "P9.2", 118: "P9.3", 120: "P9.4"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_cn2_schematic(pins_csv: Path) -> dict[int, dict[str, str]]:
    rows: list[dict[str, str]] = []
    with pins_csv.open("r", encoding="utf-8-sig", newline="") as handle:
        rows = [row for row in csv.DictReader(handle) if row["designator"] == "CN2"]
    component_ids = {row["component_id"] for row in rows}
    if len(component_ids) != 1:
        raise RuntimeError(f"Expected exactly one schematic CN2 instance, got {component_ids}")
    result = {int(row["pin_number"]): row for row in rows}
    if len(result) != 120 or sorted(result) != list(range(1, 121)):
        raise RuntimeError(f"Schematic CN2 pin set is not 1..120: {len(result)} unique pins")
    return result


def load_pcb_data(epru: Path) -> tuple[list[dict[str, Any]], dict[str, dict[str, set[str]]]]:
    cn2_hits: list[dict[str, Any]] = []
    embedded_headers: dict[str, dict[str, set[str]]] = {}
    for doc in load_documents(epru):
        if doc["doc_type"] != "PCB":
            continue
        meta = meta_for(doc)
        title = str(meta.get("title") or "")
        attrs = attrs_by_parent(doc["records"])
        designator_by_component: dict[str, str] = {}
        for record in doc["records"]:
            if record["type"] != "COMPONENT":
                continue
            merged = {**(record["data"].get("attrs") or {}), **attrs.get(str(record["id"]), {})}
            designator_by_component[str(record["id"])] = str(merged.get("Designator") or "")

        pad_rows: list[tuple[str, str, str]] = []
        for record in doc["records"]:
            if record["type"] != "PAD_NET":
                continue
            try:
                key = json.loads(str(record["id"]))
            except json.JSONDecodeError:
                continue
            if len(key) >= 3:
                pad_rows.append((str(key[1]), str(key[2]), str(record["data"].get("padNet") or "")))

        for component_id, designator in designator_by_component.items():
            if designator != "CN2":
                continue
            component_pads: dict[int, str] = {}
            for cid, number_text, net in pad_rows:
                if cid != component_id:
                    continue
                number = int(number_text)
                if number in component_pads and component_pads[number] != net:
                    raise RuntimeError(f"Conflicting PCB net at {title} CN2.{number}")
                component_pads[number] = net
            net_members: dict[str, list[str]] = defaultdict(list)
            cn2_nets = {net for net in component_pads.values() if net}
            for cid, number, net in pad_rows:
                if net in cn2_nets:
                    designator = designator_by_component.get(cid, cid)
                    member = f"{designator}.{int(number)}"
                    if member not in net_members[net]:
                        net_members[net].append(member)
            for members in net_members.values():
                members.sort()
            cn2_hits.append({
                "pcb_uuid": doc["uuid"],
                "pcb_title": title,
                "component_id": component_id,
                "pads": component_pads,
                "net_members": dict(net_members),
            })

        if "RA8P1" in title:
            header_map: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
            for cid, number, net in pad_rows:
                designator = designator_by_component.get(cid, "")
                if designator in {"P4", "P5", "P6", "P7", "P8", "P9", "P10"}:
                    header_map[designator][str(int(number))].add(net)
            embedded_headers = {ref: dict(pins) for ref, pins in header_map.items()}
    return cn2_hits, embedded_headers


def cfg_lines_by_pin(ra_cfg: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in ra_cfg.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = line.strip().split(maxsplit=1)
        if fields and fields[0].startswith("P"):
            result[fields[0]] = line.strip()
    return result


def assignment_for(pin: int, schematic_net: str) -> dict[str, str]:
    if pin == 1:
        return {"logical_net": "NC", "ra_pin": "NC", "header": "do not connect", "direction": "—", "role": "reserved/no-connect", "status": "native NC", "mux": ""}
    if schematic_net == "GND":
        return {"logical_net": "GND", "ra_pin": "GND", "header": "P10.1-P10.8 / GND plane", "direction": "power return", "role": "ground", "status": "native", "mux": ""}
    if schematic_net == "3V3_RA":
        return {"logical_net": "3V3_RA", "ra_pin": "VCC", "header": POWER_HEADERS[pin], "direction": "power", "role": "3.3 V supply", "status": "native", "mux": ""}
    if pin in PIN_OVERRIDES:
        return dict(PIN_OVERRIDES[pin])
    if schematic_net not in ASSIGNMENTS:
        raise RuntimeError(f"No RA8P1 assignment for CN2.{pin} net {schematic_net}")
    result = dict(ASSIGNMENTS[schematic_net])
    result["logical_net"] = schematic_net
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--epro2", type=Path, required=True)
    parser.add_argument("--epru", type=Path, required=True)
    parser.add_argument("--pins-csv", type=Path, required=True)
    parser.add_argument("--board-source", type=Path, required=True)
    parser.add_argument("--board-audit", type=Path, required=True)
    parser.add_argument("--ra-cfg", type=Path, required=True)
    parser.add_argument("--configuration-xml", type=Path, required=True)
    parser.add_argument("--pin-data", type=Path, required=True)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-csv", type=Path, required=True)
    args = parser.parse_args()

    schematic = load_cn2_schematic(args.pins_csv)
    pcb_hits, embedded_headers = load_pcb_data(args.epru)
    if len(pcb_hits) != 2:
        raise RuntimeError(f"Expected two native PCB CN2 implementations, got {len(pcb_hits)}")
    for hit in pcb_hits:
        if sorted(hit["pads"]) != list(range(1, 121)):
            raise RuntimeError(f"{hit['pcb_title']} CN2 pad set is not 1..120")

    board = json.loads(args.board_audit.read_text(encoding="utf-8"))
    board_hash = sha256(args.board_source)
    if board_hash.lower() != str(board["inputSha256"]).lower():
        raise RuntimeError("Developer-board source hash does not match its native audit")
    cfg = cfg_lines_by_pin(args.ra_cfg)

    rows: list[dict[str, Any]] = []
    named_mismatches: list[dict[str, Any]] = []
    topology_checks: list[dict[str, Any]] = []
    breakout_checks: list[dict[str, Any]] = []
    mux_checks: list[dict[str, Any]] = []
    active_ports: dict[str, list[tuple[int, str, str]]] = defaultdict(list)

    for pin, sch_row in sorted(schematic.items()):
        sch_net = sch_row["net"]
        assignment = assignment_for(pin, sch_net)
        pcb_nets = {hit["pcb_title"]: hit["pads"][pin] for hit in pcb_hits}
        if pin == 1:
            pcb_match = all(not net for net in pcb_nets.values()) and sch_row["connected"] == "False"
            match_method = "schematic NC and PCB pad has no net"
        elif pin == 75:
            checks = []
            primary_net = next((hit["pads"][pin] for hit in pcb_hits if hit["pcb_title"] == "PCB1"), "")
            for hit in pcb_hits:
                net = hit["pads"][pin]
                members = hit["net_members"].get(net, [])
                if hit["pcb_title"] == "PCB1":
                    ok = "CN2.75" in members and "R49.2" in members
                    method = "full PCB topology CN2.75-R49.2"
                else:
                    ok = net == primary_net and "CN2.75" in members
                    method = "connector-only PCB copy uses the same native PCB net as PCB1"
                checks.append(ok)
                topology_checks.append({"pcb": hit["pcb_title"], "pin": 75, "pcb_net": net, "members": members, "method": method, "ok": ok})
            pcb_match = all(checks)
            match_method = "anonymous net matched by CN2.75-R49.2 topology"
        else:
            pcb_match = all(net == sch_net for net in pcb_nets.values())
            match_method = "exact native net-name equality"
        if not pcb_match:
            named_mismatches.append({"pin": pin, "schematic_net": sch_net, "pcb_nets": pcb_nets})

        ra_pin = assignment["ra_pin"]
        header = assignment["header"]
        if ra_pin not in {"NC", "GND", "VCC"}:
            active_ports[ra_pin].append((pin, assignment["logical_net"], assignment.get("shared", "")))
            header_ref, header_pin = header.split(".")
            sch_dev_net = str(board["pinMap"][header_ref][header_pin]["net"])
            pcb_dev_net = str(board["pcbPinMap"][header_ref][header_pin])
            embedded_nets = sorted(embedded_headers.get(header_ref, {}).get(header_pin, set()))
            breakout_checks.append({
                "cn2_pin": pin,
                "logical_net": assignment["logical_net"],
                "ra_pin": ra_pin,
                "header": header,
                "standalone_schematic_net": sch_dev_net,
                "standalone_pcb_net": pcb_dev_net,
                "embedded_pcb_nets": embedded_nets,
                "ok": sch_dev_net == ra_pin and pcb_dev_net == ra_pin and embedded_nets == [ra_pin],
            })
            line = cfg.get(ra_pin, "")
            required = assignment.get("mux", "")
            mux_checks.append({
                "cn2_pin": pin,
                "logical_net": assignment["logical_net"],
                "ra_pin": ra_pin,
                "required": required or "GPIO capability",
                "cfg_line": line,
                "ok": bool(line) and (not required or required in line),
            })

        rows.append({
            "cn2_pin": pin,
            "schematic_net": sch_net,
            "pcb1_net": pcb_nets.get("PCB1", ""),
            "pcb3_net": pcb_nets.get("PCB3", ""),
            "pcb_match": pcb_match,
            "match_method": match_method,
            "logical_net": assignment["logical_net"],
            "ra8p1_pin": ra_pin,
            "developer_board_header": header,
            "direction": assignment["direction"],
            "role": assignment["role"],
            "status": assignment["status"],
        })

    for pin, header in POWER_HEADERS.items():
        header_ref, header_pin = header.split(".")
        sch_dev_net = str(board["pinMap"][header_ref][header_pin]["net"])
        pcb_dev_net = str(board["pcbPinMap"][header_ref][header_pin])
        embedded_nets = sorted(embedded_headers.get(header_ref, {}).get(header_pin, set()))
        breakout_checks.append({
            "cn2_pin": pin,
            "logical_net": "3V3_RA",
            "ra_pin": "VCC",
            "header": header,
            "standalone_schematic_net": sch_dev_net,
            "standalone_pcb_net": pcb_dev_net,
            "embedded_pcb_nets": embedded_nets,
            "ok": sch_dev_net == "VCC" and pcb_dev_net == "VCC" and embedded_nets == ["VCC"],
        })
    for header_pin in range(1, 9):
        key = str(header_pin)
        sch_dev_net = str(board["pinMap"]["P10"][key]["net"])
        pcb_dev_net = str(board["pcbPinMap"]["P10"][key])
        embedded_nets = sorted(embedded_headers.get("P10", {}).get(key, set()))
        breakout_checks.append({
            "cn2_pin": "all GND",
            "logical_net": "GND",
            "ra_pin": "GND",
            "header": f"P10.{header_pin}",
            "standalone_schematic_net": sch_dev_net,
            "standalone_pcb_net": pcb_dev_net,
            "embedded_pcb_nets": embedded_nets,
            "ok": sch_dev_net == "GND" and pcb_dev_net == "GND" and embedded_nets == ["GND"],
        })

    duplicate_errors = []
    for ra_pin, uses in active_ports.items():
        if len(uses) <= 1:
            continue
        shared_groups = {shared for _, _, shared in uses}
        if shared_groups != {"RAW_FAULT_WIRED_OR"}:
            duplicate_errors.append({"ra_pin": ra_pin, "uses": uses})

    xml_root = ET.parse(args.configuration_xml).getroot()
    comments = {str(node.attrib.get("propertyId") or ""): str(node.attrib.get("value") or "") for node in xml_root.iter("comment")}
    alt_ids = [str(node.attrib.get("altId") or "") for node in xml_root.iter("configSetting")]
    e2_comment_checks = []
    for ra_pin, uses in sorted(active_ports.items()):
        comment = comments.get(f"{ra_pin.lower()}.comment", "")
        pin_numbers = sorted(pin for pin, _, _ in uses)
        logical_nets = [net for _, net, _ in uses]
        if ra_pin == "P401":
            expected_pin_text = "CN2.59+61"
            ok = expected_pin_text in comment and "RAW_FAULT_N1/N2" in comment
        else:
            expected_pin_text = f"CN2.{pin_numbers[0]}"
            ok = expected_pin_text in comment and logical_nets[0] in comment
        e2_comment_checks.append({"ra_pin": ra_pin, "expected_cn2": expected_pin_text, "logical_nets": logical_nets, "comment": comment, "ok": ok})
    e2_direction_checks = []
    pin_data = args.pin_data.read_text(encoding="utf-8", errors="replace")
    for ra_pin, c_pin in (("PB01", "BSP_IO_PORT_11_PIN_01"), ("PB06", "BSP_IO_PORT_11_PIN_06")):
        match = re.search(rf"\{{ \.pin = {c_pin}, \.pin_cfg = \(\(uint32_t\) ([^}}]+)\}}", pin_data)
        c_block = match.group(0) if match else ""
        lower = ra_pin.lower()
        ok = (
            f"{lower}.input" in alt_ids
            and f"{lower}.gpio_mode.gpio_mode_in" in alt_ids
            and not any(item.startswith(f"{lower}.output") for item in alt_ids)
            and "IOPORT_CFG_PORT_DIRECTION_INPUT" in c_block
            and "IOPORT_CFG_PORT_DIRECTION_OUTPUT" not in c_block
        )
        e2_direction_checks.append({"ra_pin": ra_pin, "configuration_input": f"{lower}.input" in alt_ids, "pin_data_block": c_block, "ok": ok})

    counts = {
        "cn2_pins": len(rows),
        "ground_pins": sum(row["schematic_net"] == "GND" for row in rows),
        "power_3v3_pins": sum(row["schematic_net"] == "3V3_RA" for row in rows),
        "connected_signal_pins": sum(row["schematic_net"] not in {"GND", "3V3_RA"} and row["cn2_pin"] != 1 for row in rows),
        "no_connect_pins": sum(row["cn2_pin"] == 1 for row in rows),
        "unique_ra_signal_pins": len(active_ports),
    }
    expected_counts = {"cn2_pins": 120, "ground_pins": 69, "power_3v3_pins": 4, "connected_signal_pins": 46, "no_connect_pins": 1, "unique_ra_signal_pins": 45}
    all_checks_pass = (
        counts == expected_counts
        and not named_mismatches
        and all(item["ok"] for item in breakout_checks)
        and all(item["ok"] for item in mux_checks)
        and not duplicate_errors
        and all(item["ok"] for item in e2_comment_checks)
        and all(item["ok"] for item in e2_direction_checks)
        and "P208" not in active_ports
        and "P209" not in active_ports
    )

    result = {
        "generated_at": "2026-07-22",
        "source_epro2": str(args.epro2.resolve()),
        "source_epro2_sha256": sha256(args.epro2),
        "source_epru": str(args.epru.resolve()),
        "source_epru_sha256": sha256(args.epru),
        "developer_board_source": str(args.board_source.resolve()),
        "developer_board_source_sha256": board_hash,
        "ra_cfg": str(args.ra_cfg.resolve()),
        "configuration_xml": str(args.configuration_xml.resolve()),
        "pin_data": str(args.pin_data.resolve()),
        "schematic_cn2_component_id": next(iter({row["component_id"] for row in schematic.values()})),
        "pcb_cn2_instances": [{key: value for key, value in hit.items() if key not in {"pads", "net_members"}} for hit in pcb_hits],
        "counts": counts,
        "expected_counts": expected_counts,
        "named_or_topology_mismatches": named_mismatches,
        "anonymous_topology_checks": topology_checks,
        "developer_board_breakout_checks": breakout_checks,
        "mux_checks": mux_checks,
        "duplicate_port_errors": duplicate_errors,
        "e2studio_comment_checks": e2_comment_checks,
        "e2studio_direction_checks": e2_direction_checks,
        "uart9_preserved": "P208" not in active_ports and "P209" not in active_ports,
        "intentional_shared_port": {"P401": ["CN2.59 RAW_FAULT_N1", "CN2.61 RAW_FAULT_N2"], "reason": "main-board BAT54 diode isolation forms POEG0 wired-OR"},
        "all_checks_pass": all_checks_pass,
        "cn2_rows": rows,
    }
    if not all_checks_pass:
        raise RuntimeError(json.dumps({
            "counts": counts,
            "named_mismatches": named_mismatches,
            "breakout_failures": [item for item in breakout_checks if not item["ok"]],
            "mux_failures": [item for item in mux_checks if not item["ok"]],
            "duplicate_errors": duplicate_errors,
            "e2_comment_failures": [item for item in e2_comment_checks if not item["ok"]],
            "e2_direction_failures": [item for item in e2_direction_checks if not item["ok"]],
        }, ensure_ascii=False, indent=2))

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    fieldnames = list(rows[0])
    with args.output_csv.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(json.dumps({
        "output_json": str(args.output_json.resolve()),
        "output_csv": str(args.output_csv.resolve()),
        "counts": counts,
        "pcb_cn2_instances": [{"title": hit["pcb_title"], "uuid": hit["pcb_uuid"], "component_id": hit["component_id"]} for hit in pcb_hits],
        "all_checks_pass": all_checks_pass,
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
