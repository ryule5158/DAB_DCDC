#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path

from docx import Document
from docx.enum.section import WD_ORIENT
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor


NAVY = "17365D"
BLUE = "2F5597"
BLUE_LIGHT = "D9EAF7"
GREEN_LIGHT = "E2F0D9"
YELLOW_LIGHT = "FFF2CC"
GRAY_LIGHT = "EDEDED"
RED_LIGHT = "FCE4D6"
WHITE = "FFFFFF"
MUTED = "5A6573"
INK = "1B1F23"


def set_font(run, *, size: float | None = None, bold: bool | None = None, color: str | None = None) -> None:
    run.font.name = "Calibri"
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.rFonts
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.insert(0, rfonts)
    rfonts.set(qn("w:ascii"), "Calibri")
    rfonts.set(qn("w:hAnsi"), "Calibri")
    rfonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.bold = bold
    if color is not None:
        run.font.color.rgb = RGBColor.from_string(color)


def shade(cell, fill: str) -> None:
    tc_pr = cell._tc.get_or_add_tcPr()
    node = tc_pr.find(qn("w:shd"))
    if node is None:
        node = OxmlElement("w:shd")
        tc_pr.append(node)
    node.set(qn("w:fill"), fill)


def margins(cell, top=35, start=65, bottom=35, end=65) -> None:
    tc_pr = cell._tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for name, value in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{name}"))
        if node is None:
            node = OxmlElement(f"w:{name}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(value))
        node.set(qn("w:type"), "dxa")


def table_geometry(table, widths: list[int]) -> None:
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    tbl_pr = table._tbl.tblPr
    layout = tbl_pr.find(qn("w:tblLayout"))
    if layout is None:
        layout = OxmlElement("w:tblLayout")
        tbl_pr.append(layout)
    layout.set(qn("w:type"), "fixed")
    tbl_w = tbl_pr.find(qn("w:tblW"))
    if tbl_w is None:
        tbl_w = OxmlElement("w:tblW")
        tbl_pr.append(tbl_w)
    tbl_w.set(qn("w:w"), str(sum(widths)))
    tbl_w.set(qn("w:type"), "dxa")
    grid = table._tbl.tblGrid
    for child in list(grid):
        grid.remove(child)
    for width in widths:
        item = OxmlElement("w:gridCol")
        item.set(qn("w:w"), str(width))
        grid.append(item)
    for row in table.rows:
        tr_pr = row._tr.get_or_add_trPr()
        if tr_pr.find(qn("w:cantSplit")) is None:
            tr_pr.append(OxmlElement("w:cantSplit"))
        for index, cell in enumerate(row.cells):
            tc_pr = cell._tc.get_or_add_tcPr()
            tc_w = tc_pr.find(qn("w:tcW"))
            if tc_w is None:
                tc_w = OxmlElement("w:tcW")
                tc_pr.append(tc_w)
            tc_w.set(qn("w:w"), str(widths[index]))
            tc_w.set(qn("w:type"), "dxa")
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            margins(cell)


def repeat_header(row) -> None:
    tr_pr = row._tr.get_or_add_trPr()
    node = OxmlElement("w:tblHeader")
    node.set(qn("w:val"), "true")
    tr_pr.append(node)


def write_cell(cell, text: object, *, size=7.2, bold=False, color=INK, align=WD_ALIGN_PARAGRAPH.LEFT) -> None:
    cell.text = ""
    paragraph = cell.paragraphs[0]
    paragraph.alignment = align
    paragraph.paragraph_format.space_before = Pt(0)
    paragraph.paragraph_format.space_after = Pt(0)
    paragraph.paragraph_format.line_spacing = 1.0
    run = paragraph.add_run(str(text))
    set_font(run, size=size, bold=bold, color=color)


def add_field(paragraph, code: str) -> None:
    run = paragraph.add_run()
    begin = OxmlElement("w:fldChar")
    begin.set(qn("w:fldCharType"), "begin")
    instruction = OxmlElement("w:instrText")
    instruction.set(qn("xml:space"), "preserve")
    instruction.text = code
    separate = OxmlElement("w:fldChar")
    separate.set(qn("w:fldCharType"), "separate")
    end = OxmlElement("w:fldChar")
    end.set(qn("w:fldCharType"), "end")
    run._r.extend([begin, instruction, separate, end])


def setup_styles(doc: Document) -> None:
    normal = doc.styles["Normal"]
    normal.font.name = "Calibri"
    normal.font.size = Pt(9.4)
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    normal.paragraph_format.space_after = Pt(4)
    normal.paragraph_format.line_spacing = 1.12
    for name, size, before, after in (("Heading 1", 14, 13, 7), ("Heading 2", 11.5, 9, 5)):
        style = doc.styles[name]
        style.font.name = "Calibri"
        style.font.size = Pt(size)
        style.font.bold = True
        style.font.color.rgb = RGBColor.from_string(BLUE)
        style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
        style.paragraph_format.space_before = Pt(before)
        style.paragraph_format.space_after = Pt(after)
        style.paragraph_format.keep_with_next = True


def add_bullet(doc: Document, text: str, *, color=INK) -> None:
    paragraph = doc.add_paragraph(style="List Bullet")
    paragraph.paragraph_format.space_after = Pt(2.5)
    run = paragraph.add_run(text)
    set_font(run, size=9.2, color=color)


def add_notice(doc: Document, title: str, body: str, fill: str) -> None:
    table = doc.add_table(rows=1, cols=1)
    table.style = "Table Grid"
    cell = table.cell(0, 0)
    shade(cell, fill)
    cell.text = ""
    p = cell.paragraphs[0]
    p.paragraph_format.space_after = Pt(2)
    r = p.add_run(title)
    set_font(r, size=10, bold=True, color=NAVY)
    p2 = cell.add_paragraph()
    p2.paragraph_format.space_after = Pt(0)
    r2 = p2.add_run(body)
    set_font(r2, size=9, color=INK)
    margins(cell, top=90, start=130, bottom=90, end=130)


def display_net(row: dict) -> str:
    if row["cn2_pin"] == 1:
        return "NC（SCH: NC$14；PCB: 空）"
    if row["cn2_pin"] == 75:
        return "DAB_STATUS_LED\nSCH: N$12；PCB: $1N1440"
    return row["schematic_net"]


def row_fill(row: dict) -> str:
    if row["cn2_pin"] == 1:
        return RED_LIGHT
    if row["schematic_net"] == "GND":
        return GRAY_LIGHT
    if row["schematic_net"] == "3V3_RA":
        return YELLOW_LIGHT
    if row["status"] == "tested":
        return GREEN_LIGHT
    return BLUE_LIGHT


def status_cn(status: str) -> str:
    return {"tested": "保留既有已测端口", "new": "新增／待整机联调", "native": "原生电源/地", "native NC": "原生 NC"}.get(status, status)


def build_doc(audit: dict, output: Path) -> None:
    if not audit.get("all_checks_pass"):
        raise RuntimeError("CN2 audit is not clean")
    doc = Document()
    section = doc.sections[0]
    section.orientation = WD_ORIENT.LANDSCAPE
    section.page_width = Cm(29.7)
    section.page_height = Cm(21.0)
    section.top_margin = Cm(1.05)
    section.bottom_margin = Cm(1.0)
    section.left_margin = Cm(1.05)
    section.right_margin = Cm(1.05)
    section.header_distance = Cm(0.45)
    section.footer_distance = Cm(0.45)
    setup_styles(doc)

    header = section.header.paragraphs[0]
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    r = header.add_run("RA8P1 × CN2 校正接口定义  ·  CURRENT NETLIST  ·  2026-07-22")
    set_font(r, size=7.6, color=MUTED)
    footer = section.footer.paragraphs[0]
    footer.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    r = footer.add_run("ProPrj_瑞萨杯_2026-07-22  |  页 ")
    set_font(r, size=7.6, color=MUTED)
    add_field(footer, " PAGE ")

    title = doc.add_paragraph()
    title.paragraph_format.space_after = Pt(2)
    r = title.add_run("RA8P1—CN2 最终引脚连接表（校正版）")
    set_font(r, size=20, bold=True, color=NAVY)
    subtitle = doc.add_paragraph()
    subtitle.paragraph_format.space_after = Pt(8)
    r = subtitle.add_run("唯一依据：ProPrj_瑞萨杯_2026-07-22.epro2 的原生原理图与 PCB 网表  |  CN2：C5097366 / 120P")
    set_font(r, size=9, color=MUTED)

    add_notice(
        doc,
        "旧版文件作废",
        "此前按 7 月 21 日脚序生成的文档不得再用于接线。本版重新从 7 月 22 日文件逐针提取：69×GND、4×3V3_RA、46×已连接信号、1×NC；总计 120 针。",
        RED_LIGHT,
    )

    doc.add_heading("1. 审计结论", level=1)
    facts = doc.add_table(rows=2, cols=3)
    facts.style = "Table Grid"
    values = [
        ("CN2 覆盖", "120 / 120"),
        ("SCH ↔ PCB1 ↔ PCB3", "0 个不一致"),
        ("开发板引出", "45 个 MCU 信号端口全通过"),
        ("3.3 V / GND / NC", "4 / 69 / 1"),
        ("复用功能核对", "ADC、GPT、POEG、CANFD0、RMII1、IIC2 全通过"),
        ("UART9", "P208/P209 保留，未占用"),
    ]
    for index, (label, value) in enumerate(values):
        cell = facts.cell(index // 3, index % 3)
        cell.text = ""
        p = cell.paragraphs[0]
        p.paragraph_format.space_after = Pt(1)
        r = p.add_run(label + "\n")
        set_font(r, size=7.7, bold=True, color=MUTED)
        r2 = p.add_run(value)
        set_font(r2, size=10, bold=True, color=NAVY)
        shade(cell, "F4F7FA")
        margins(cell, top=75, start=95, bottom=75, end=95)
    table_geometry(facts, [5000, 5000, 5000])

    doc.add_heading("2. 接线时必须遵守", level=1)
    add_bullet(doc, "CN2.1 是原生 NC：原理图为 NC$14，PCB1/PCB3 焊盘无网络；必须悬空。")
    add_bullet(doc, "CN2.75 是 LED10 状态信号：原理图匿名网 N$12，完整 PCB 网 $1N1440；拓扑为 CN2.75 → R49.2 → LED10 → GND，接 P402 / 开发板 P4.1。")
    add_bullet(doc, "CN2.59 RAW_FAULT_N1 与 CN2.61 RAW_FAULT_N2 在转接板 MCU 侧共同接 P401 / P8.8，主板 BAT54 二极管隔离后形成 POEG0 硬件线与；P400、P403仍分别读取一次侧和二次侧过流。")
    add_bullet(doc, "CN2.103、105、118、120 均为 3V3_RA，分别接开发板 P9.1、P9.2、P9.3、P9.4；不得接 5 V。69 个 GND 全接公共地平面，并联接开发板 P10.1～P10.8。")
    add_bullet(doc, "SEC_HW_OK_CTRL 与 PRI_HW_OK_CTRL 来自 ISO7741 的 OUTD，对 MCU 是输入：CN2.49 → PB01/P6.19，CN2.95 → PB06/P6.18。")
    add_bullet(doc, "CN2 编号以 EasyEDA 原生焊盘编号为准；布局布线时请对齐封装 1 脚标记，不要按奇偶脚经验推断。")

    doc.add_page_break()
    doc.add_heading("3. 非 GND 接口表（信号、电源与 NC）", level=1)
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(5)
    r = p.add_run("绿色＝保留此前已测 MCU 端口；蓝色＝新增端口；黄色＝3.3 V；红色＝NC。所有 CN2 脚号均来自当前 7 月 22 日工程。")
    set_font(r, size=8.3, color=MUTED)
    headers = ["CN2", "主板真实网络", "RA8P1", "开发板针脚", "方向 / 硬件功能", "状态"]
    widths = [780, 3150, 1200, 1750, 4800, 2300]
    table = doc.add_table(rows=1, cols=len(headers))
    table.style = "Table Grid"
    for index, label in enumerate(headers):
        write_cell(table.rows[0].cells[index], label, size=7.8, bold=True, color=WHITE, align=WD_ALIGN_PARAGRAPH.CENTER)
        shade(table.rows[0].cells[index], NAVY)
    repeat_header(table.rows[0])
    for row in audit["cn2_rows"]:
        if row["schematic_net"] == "GND":
            continue
        cells = table.add_row().cells
        values = [
            row["cn2_pin"],
            display_net(row),
            row["ra8p1_pin"],
            row["developer_board_header"],
            f"{row['direction']} / {row['role']}",
            status_cn(row["status"]),
        ]
        fill = row_fill(row)
        for index, value in enumerate(values):
            write_cell(cells[index], value, size=7.0, align=WD_ALIGN_PARAGRAPH.CENTER if index in {0, 2, 3, 5} else WD_ALIGN_PARAGRAPH.LEFT)
            shade(cells[index], fill)
    table_geometry(table, widths)

    doc.add_page_break()
    doc.add_heading("4. CN2 120 针逐针核对表", level=1)
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(5)
    r = p.add_run("左半为 1～60，右半为 61～120。GND、3V3、NC 也全部列出，便于布线后逐针复核。")
    set_font(r, size=8.3, color=MUTED)
    headers = ["针", "主板网络", "RA8P1", "开发板", "针", "主板网络", "RA8P1", "开发板"]
    widths = [620, 2900, 1100, 2980, 620, 2900, 1100, 2980]
    full = doc.add_table(rows=1, cols=8)
    full.style = "Table Grid"
    for index, label in enumerate(headers):
        write_cell(full.rows[0].cells[index], label, size=7.6, bold=True, color=WHITE, align=WD_ALIGN_PARAGRAPH.CENTER)
        shade(full.rows[0].cells[index], NAVY)
    repeat_header(full.rows[0])
    by_pin = {int(row["cn2_pin"]): row for row in audit["cn2_rows"]}
    for index in range(1, 61):
        left = by_pin[index]
        right = by_pin[index + 60]
        cells = full.add_row().cells
        for offset, row in ((0, left), (4, right)):
            values = [row["cn2_pin"], display_net(row), row["ra8p1_pin"], row["developer_board_header"]]
            fill = row_fill(row)
            for col, value in enumerate(values):
                write_cell(cells[offset + col], value, size=6.7, align=WD_ALIGN_PARAGRAPH.CENTER if col in {0, 2, 3} else WD_ALIGN_PARAGRAPH.LEFT)
                shade(cells[offset + col], fill)
    table_geometry(full, widths)

    doc.add_heading("5. GND 清单与电源连接", level=1)
    ground_pins = [str(row["cn2_pin"]) for row in audit["cn2_rows"] if row["schematic_net"] == "GND"]
    p = doc.add_paragraph()
    r = p.add_run("69 个 GND：")
    set_font(r, size=8.5, bold=True, color=NAVY)
    r2 = p.add_run(", ".join(ground_pins))
    set_font(r2, size=8.2, color=INK)
    add_bullet(doc, "全部 CN2 GND 焊盘接同一低阻抗 GND 平面；开发板侧 P10.1～P10.8 全部并联到该平面。")
    add_bullet(doc, "四个 3V3_RA 焊盘是同一网络，但建议分别落到 P9.1～P9.4，避免把连接器的并联供电脚遗漏。")

    doc.add_heading("6. e2studio 同步状态", level=1)
    add_bullet(doc, "configuration.xml 中 45 个 MCU 信号端口的 CN2 注释已全部更新为本表脚号。")
    add_bullet(doc, "PB01 / PB06 已按 ISO7741 OUTD 改为 GPIO 输入；生成的 ra_gen/pin_data.c 同步为输入，无输出驱动。")
    add_bullet(doc, "CANFD0、ESWM_RMII1、GPT0/1/5/8、POEG0、ADC0/1 与 IIC2 的复用选择保持不变；UART9 P208/P209 保持不变。")
    add_bullet(doc, "未改业务逻辑；Debug 配置已完整重新链接通过：text 46176 B，data 456 B，bss 24632 B。")

    doc.add_heading("7. 原生数据身份", level=1)
    source_table = doc.add_table(rows=0, cols=2)
    source_table.style = "Table Grid"
    source_rows = [
        ("主工程", audit["source_epro2"]),
        ("主工程 SHA-256", audit["source_epro2_sha256"].upper()),
        ("原生 EPRU SHA-256", audit["source_epru_sha256"].upper()),
        ("原理图 CN2 实例", audit["schematic_cn2_component_id"]),
        ("PCB1 CN2", "UUID 2e443d39c78011f1 / component 31b1d2de8c4407f3"),
        ("PCB3 CN2", "UUID ab46fc1910caa8ca / component 7477aceb4c752f06"),
        ("开发板工程", audit["developer_board_source"]),
        ("开发板 SHA-256", audit["developer_board_source_sha256"].upper()),
        ("自动审计", "all_checks_pass = true；网表不一致 0；开发板引出失败 0；复用失败 0；e2studio 注释/方向失败 0"),
    ]
    for label, value in source_rows:
        cells = source_table.add_row().cells
        write_cell(cells[0], label, size=7.4, bold=True, color=NAVY)
        write_cell(cells[1], value, size=7.2)
        shade(cells[0], BLUE_LIGHT)
    table_geometry(source_table, [2500, 12500])

    end = doc.add_paragraph()
    end.paragraph_format.space_before = Pt(0)
    end.paragraph_format.space_after = Pt(0)
    r = end.add_run(" ")
    set_font(r, size=1)

    doc.core_properties.title = "RA8P1—CN2 最终引脚连接表（校正版）"
    doc.core_properties.subject = "ProPrj_瑞萨杯_2026-07-22 CN2 原生网表与 RA8P1 开发板引出映射"
    doc.core_properties.author = "Codex"
    doc.core_properties.keywords = "RA8P1,CN2,C5097366,DAB,EasyEDA,netlist"
    output.parent.mkdir(parents=True, exist_ok=True)
    doc.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--audit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    audit = json.loads(args.audit.read_text(encoding="utf-8"))
    build_doc(audit, args.output)
    doc = Document(args.output)
    print(json.dumps({
        "output": str(args.output.resolve()),
        "paragraphs": len(doc.paragraphs),
        "tables": len(doc.tables),
        "sections": len(doc.sections),
        "all_checks_pass": audit["all_checks_pass"],
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
