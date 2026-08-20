#!/usr/bin/env python3
"""Extract CAN connectivity evidence from a native EasyEDA .epro2 project.

The parser reads the embedded .epru record stream and derives connectivity
from symbol pins, component transforms, WIRE/LINE geometry, and NET labels.
It never uses schematic screenshots.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import zipfile
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


EPS = 1.0e-6


def parse_record(line: str) -> tuple[dict[str, Any], dict[str, Any]]:
    left, separator, right = line.rstrip("\r\n").partition("||")
    if not separator:
        raise ValueError("EasyEDA record delimiter not found")
    return json.loads(left), json.loads(right.rstrip("|")) if right.rstrip("|") else {}


def load_documents(project: Path) -> tuple[str, list[dict[str, Any]]]:
    with zipfile.ZipFile(project) as archive:
        entries = [name for name in archive.namelist() if name.lower().endswith(".epru")]
        if len(entries) != 1:
            raise RuntimeError(f"expected one .epru entry, found {len(entries)}")
        documents: list[dict[str, Any]] = []
        current: dict[str, Any] | None = None
        failures: list[str] = []
        with archive.open(entries[0]) as raw:
            with io.TextIOWrapper(raw, encoding="utf-8", errors="replace") as stream:
                for line_number, line in enumerate(stream, 1):
                    try:
                        head, payload = parse_record(line)
                    except Exception as exc:
                        failures.append(f"line {line_number}: {exc}")
                        continue
                    record = {
                        "type": head.get("type"),
                        "id": str(head.get("id") or ""),
                        "data": payload,
                    }
                    if record["type"] == "DOCHEAD":
                        current = {
                            "doc_type": payload.get("docType"),
                            "uuid": str(payload.get("uuid") or ""),
                            "records": [],
                        }
                        documents.append(current)
                    elif current is not None:
                        current["records"].append(record)
        if failures:
            raise RuntimeError(
                f"failed to parse {len(failures)} records: " + "; ".join(failures[:5])
            )
        return entries[0], documents


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def meta_for(document: dict[str, Any]) -> dict[str, Any]:
    return next(
        (record["data"] for record in document["records"] if record["type"] == "META"),
        {},
    )


def attributes_by_parent(records: Iterable[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    attributes: dict[str, dict[str, Any]] = defaultdict(dict)
    for record in records:
        if record["type"] != "ATTR":
            continue
        data = record["data"]
        parent = data.get("parentId")
        key = data.get("key")
        if parent is not None and key is not None:
            attributes[str(parent)][str(key)] = data.get("value")
    return attributes


@dataclass(frozen=True)
class SymbolPin:
    number: str
    name: str
    x: float
    y: float


def build_symbol_index(
    documents: list[dict[str, Any]],
) -> tuple[dict[tuple[str, str], list[SymbolPin]], dict[str, list[tuple[str, str]]]]:
    exact: dict[tuple[str, str], list[SymbolPin]] = defaultdict(list)
    by_part: dict[str, list[tuple[str, str]]] = defaultdict(list)
    for document in documents:
        if document["doc_type"] != "SYMBOL":
            continue
        attrs = attributes_by_parent(document["records"])
        canvas = next(
            (record["data"] for record in document["records"] if record["type"] == "CANVAS"),
            {},
        )
        origin_x = float(canvas.get("originX") or 0.0)
        origin_y = float(canvas.get("originY") or 0.0)
        part_ids = [
            record["id"] for record in document["records"] if record["type"] == "PART"
        ]
        for part_id in part_ids:
            key = (document["uuid"], part_id)
            if key not in by_part[part_id]:
                by_part[part_id].append(key)
        for record in document["records"]:
            if record["type"] != "PIN":
                continue
            data = record["data"]
            part_id = str(data.get("partId") or "")
            pin_attrs = attrs.get(record["id"], {})
            exact[(document["uuid"], part_id)].append(
                SymbolPin(
                    number=str(pin_attrs.get("Pin Number") or ""),
                    name=str(pin_attrs.get("Pin Name") or ""),
                    x=float(data.get("x") or 0.0) - origin_x,
                    y=float(data.get("y") or 0.0) - origin_y,
                )
            )
    return dict(exact), dict(by_part)


def build_device_index(documents: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    devices: dict[str, dict[str, Any]] = {}
    for document in documents:
        if document["doc_type"] != "DEVICE":
            continue
        meta = meta_for(document)
        attrs = meta.get("attributes") or {}
        devices[document["uuid"]] = {
            "title": str(meta.get("title") or ""),
            "manufacturer_part": str(attrs.get("Manufacturer Part") or ""),
        }
    return devices


def component_pins(
    component: dict[str, Any],
    attrs: dict[str, Any],
    exact: dict[tuple[str, str], list[SymbolPin]],
    by_part: dict[str, list[tuple[str, str]]],
) -> list[SymbolPin]:
    part_id = str(component["data"].get("partId") or "")
    symbol_uuid = str(attrs.get("Symbol") or "")
    pins = exact.get((symbol_uuid, part_id))
    if pins is not None:
        return pins
    candidates = by_part.get(part_id, [])
    return exact.get(candidates[0], []) if len(candidates) == 1 else []


def transform_pin(
    pin: SymbolPin,
    component: dict[str, Any],
    rotation_sign: int,
    mirror_after_rotation: bool,
) -> tuple[float, float]:
    data = component["data"]
    x, y = pin.x, pin.y
    mirrored = bool(data.get("isMirror"))
    if mirrored and not mirror_after_rotation:
        x = -x
    angle = math.radians(rotation_sign * float(data.get("rotation") or 0.0))
    rotated_x = x * math.cos(angle) - y * math.sin(angle)
    rotated_y = x * math.sin(angle) + y * math.cos(angle)
    if mirrored and mirror_after_rotation:
        rotated_x = -rotated_x
    return (
        round(float(data.get("x") or 0.0) + rotated_x, 6),
        round(float(data.get("y") or 0.0) + rotated_y, 6),
    )


def point_on_segment(
    point: tuple[float, float], segment: tuple[float, float, float, float]
) -> bool:
    px, py = point
    x1, y1, x2, y2 = segment
    cross = (px - x1) * (y2 - y1) - (py - y1) * (x2 - x1)
    scale = max(1.0, abs(x2 - x1), abs(y2 - y1))
    return (
        abs(cross) <= EPS * scale
        and min(x1, x2) - EPS <= px <= max(x1, x2) + EPS
        and min(y1, y2) - EPS <= py <= max(y1, y2) + EPS
    )


class UnionFind:
    def __init__(self, items: Iterable[str]) -> None:
        self.parent = {item: item for item in items}

    def find(self, item: str) -> str:
        parent = self.parent[item]
        if parent != item:
            self.parent[item] = self.find(parent)
        return self.parent[item]

    def union(self, first: str, second: str) -> None:
        first_root, second_root = self.find(first), self.find(second)
        if first_root != second_root:
            self.parent[second_root] = first_root


def page_geometry(
    page: dict[str, Any],
) -> tuple[
    list[dict[str, Any]],
    dict[str, dict[str, Any]],
    dict[str, list[tuple[float, float, float, float]]],
]:
    attrs = attributes_by_parent(page["records"])
    components = [record for record in page["records"] if record["type"] == "COMPONENT"]
    wire_ids = {record["id"] for record in page["records"] if record["type"] == "WIRE"}
    segments: dict[str, list[tuple[float, float, float, float]]] = defaultdict(list)
    for record in page["records"]:
        if record["type"] != "LINE":
            continue
        data = record["data"]
        group = str(data.get("lineGroup") or "")
        if group not in wire_ids:
            continue
        keys = ("startX", "startY", "endX", "endY")
        if all(data.get(key) is not None for key in keys):
            segments[group].append(tuple(float(data[key]) for key in keys))
    return components, attrs, dict(segments)


def choose_transform(
    pages: list[dict[str, Any]],
    exact: dict[tuple[str, str], list[SymbolPin]],
    by_part: dict[str, list[tuple[str, str]]],
) -> tuple[int, bool, dict[str, int]]:
    scores: dict[str, int] = {}
    options = [(sign, mirror_after) for sign in (1, -1) for mirror_after in (False, True)]
    for sign, mirror_after in options:
        score = 0
        for page in pages:
            components, attrs, segments_by_wire = page_geometry(page)
            segments = [segment for group in segments_by_wire.values() for segment in group]
            for component in components:
                component_attrs = attrs.get(component["id"], {})
                for pin in component_pins(component, component_attrs, exact, by_part):
                    point = transform_pin(pin, component, sign, mirror_after)
                    if any(point_on_segment(point, segment) for segment in segments):
                        score += 1
        scores[f"sign={sign},mirror_after={str(mirror_after).lower()}"] = score
    best = max(
        options,
        key=lambda option: scores[
            f"sign={option[0]},mirror_after={str(option[1]).lower()}"
        ],
    )
    return best[0], best[1], scores


def extract_page(
    page: dict[str, Any],
    exact: dict[tuple[str, str], list[SymbolPin]],
    by_part: dict[str, list[tuple[str, str]]],
    devices: dict[str, dict[str, Any]],
    rotation_sign: int,
    mirror_after_rotation: bool,
) -> list[dict[str, Any]]:
    components, attrs, segments_by_wire = page_geometry(page)
    wire_ids = sorted(segments_by_wire)
    union = UnionFind(wire_ids)
    all_segments = [
        (wire_id, segment)
        for wire_id, segments in segments_by_wire.items()
        for segment in segments
    ]
    endpoints: dict[tuple[float, float], set[str]] = defaultdict(set)
    for wire_id, segment in all_segments:
        endpoints[(segment[0], segment[1])].add(wire_id)
        endpoints[(segment[2], segment[3])].add(wire_id)
    for point, groups in endpoints.items():
        touching = set(groups)
        touching.update(
            wire_id for wire_id, segment in all_segments if point_on_segment(point, segment)
        )
        touching_list = sorted(touching)
        for other in touching_list[1:]:
            union.union(touching_list[0], other)

    labels_by_wire: dict[str, set[str]] = defaultdict(set)
    first_wire_by_label: dict[str, str] = {}
    for wire_id in wire_ids:
        for key in ("NET", "Net", "Net Name", "Global Net Name"):
            value = attrs.get(wire_id, {}).get(key)
            if value in (None, ""):
                continue
            label = str(value)
            labels_by_wire[wire_id].add(label)
            if label in first_wire_by_label:
                union.union(wire_id, first_wire_by_label[label])
            else:
                first_wire_by_label[label] = wire_id

    # Multiple wire groups may deliberately terminate at the same component
    # pin.  Merge those groups before resolving root labels.
    for component in components:
        component_attrs = attrs.get(component["id"], {})
        for pin in component_pins(component, component_attrs, exact, by_part):
            point = transform_pin(pin, component, rotation_sign, mirror_after_rotation)
            hits = sorted(
                {wire_id for wire_id, segment in all_segments if point_on_segment(point, segment)}
            )
            for other in hits[1:]:
                union.union(hits[0], other)

    labels_by_root: dict[str, set[str]] = defaultdict(set)
    for wire_id, labels in labels_by_wire.items():
        labels_by_root[union.find(wire_id)].update(labels)

    rows: list[dict[str, Any]] = []
    for component in components:
        component_attrs = attrs.get(component["id"], {})
        device_uuid = str(component_attrs.get("Device") or "")
        device = devices.get(device_uuid, {})
        component_info = {
            "page_uuid": page["uuid"],
            "page_title": str(meta_for(page).get("title") or ""),
            "component_id": component["id"],
            "designator": str(component_attrs.get("Designator") or ""),
            "value": str(component_attrs.get("Value") or component_attrs.get("Name") or ""),
            "device_title": str(device.get("title") or ""),
            "manufacturer_part": str(device.get("manufacturer_part") or ""),
        }
        for pin in component_pins(component, component_attrs, exact, by_part):
            point = transform_pin(pin, component, rotation_sign, mirror_after_rotation)
            hits = sorted(
                {wire_id for wire_id, segment in all_segments if point_on_segment(point, segment)}
            )
            labels = sorted(labels_by_root.get(union.find(hits[0]), set())) if hits else []
            rows.append(
                {
                    **component_info,
                    "pin_number": pin.number,
                    "pin_name": pin.name,
                    "net": "/".join(labels) if labels else (f"ANON:{hits[0]}" if hits else "NC"),
                    "connected": bool(hits),
                }
            )
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    entry, documents = load_documents(args.project)
    exact, by_part = build_symbol_index(documents)
    devices = build_device_index(documents)
    pages = [document for document in documents if document["doc_type"] == "SCH_PAGE"]
    sign, mirror_after, scores = choose_transform(pages, exact, by_part)
    pin_rows = [
        row
        for page in pages
        for row in extract_page(page, exact, by_part, devices, sign, mirror_after)
    ]

    transceiver_rows = [
        row
        for row in pin_rows
        if "TCAN3413" in (row["device_title"] + row["manufacturer_part"]).upper()
    ]
    relevant_nets = {
        row["net"]
        for row in transceiver_rows
        if row["net"] != "NC"
    }
    relevant_nets.update(
        row["net"]
        for row in pin_rows
        if any(token in row["net"].upper() for token in ("CAN", "P704", "P705"))
    )
    neighbor_rows = [row for row in pin_rows if row["net"] in relevant_nets]
    relevant_components = {
        (row["page_uuid"], row["component_id"])
        for row in neighbor_rows
    }
    component_rows = [
        row
        for row in pin_rows
        if (row["page_uuid"], row["component_id"]) in relevant_components
    ]
    can_page_uuids = {row["page_uuid"] for row in transceiver_rows}
    component_nets = {row["net"] for row in component_rows if row["net"] != "NC"}
    can_page_net_rows = [
        row
        for row in pin_rows
        if row["page_uuid"] in can_page_uuids and row["net"] in component_nets
    ]

    def has_pin(designator: str, pin: str, net: str) -> bool:
        return any(
            row["designator"] == designator
            and row["pin_number"] == pin
            and row["net"] == net
            for row in pin_rows
        )

    def net_for(designator: str, pin: str) -> str:
        return next(
            (row["net"] for row in component_rows
             if row["designator"] == designator and row["pin_number"] == pin),
            "NC",
        )

    def net_has_ground(net: str) -> bool:
        return any(
            row["net"] == net and row["device_title"] == "Ground-GND"
            for row in can_page_net_rows
        )

    checks = {
        "cn2_109_can_tx_mcu": has_pin("CN2", "109", "CAN_TX_MCU"),
        "cn2_107_can_rx_mcu": has_pin("CN2", "107", "CAN_RX_MCU"),
        "tcan_canh": has_pin("U4", "7", "CANH_IC"),
        "tcan_canl": has_pin("U4", "6", "CANL_IC"),
        "db9_7_canh": has_pin("J1", "7", "CANH_EXT"),
        "db9_2_canl": has_pin("J1", "2", "CANL_EXT"),
        "db9_3_ground": net_has_ground(net_for("J1", "3")),
        "stb_pulldown": (
            net_for("U4", "8") == net_for("R12", "1")
            and net_has_ground(net_for("R12", "2"))
        ),
        "split_termination": (
            has_pin("R105", "1", "CANH_EXT")
            and has_pin("R105", "2", "CAN_TERM_MID")
            and has_pin("R106", "1", "CANL_EXT")
            and has_pin("R106", "2", "CAN_TERM_MID")
            and has_pin("C67", "1", "CAN_TERM_MID")
            and net_has_ground(net_for("C67", "2"))
        ),
    }
    result = {
        "project": str(args.project.resolve()),
        "project_sha256": file_sha256(args.project),
        "epru_entry": entry,
        "document_counts": {
            document_type: sum(1 for document in documents if document["doc_type"] == document_type)
            for document_type in sorted({str(document["doc_type"]) for document in documents})
        },
        "transform": {
            "rotation_sign": sign,
            "mirror_after_rotation": mirror_after,
            "scores": scores,
        },
        "transceiver_pins": transceiver_rows,
        "relevant_net_neighbors": neighbor_rows,
        "relevant_component_pins": component_rows,
        "can_page_component_net_neighbors": can_page_net_rows,
        "checks": checks,
        "all_checks_passed": all(checks.values()),
    }
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    print(encoded)
    if not result["all_checks_passed"]:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
