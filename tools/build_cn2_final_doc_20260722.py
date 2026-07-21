#!/usr/bin/env python3

from __future__ import annotations

import json
from pathlib import Path

from docx import Document
from docx.enum.section import WD_ORIENT
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Inches, Pt, RGBColor


ROOT = Path(r"C:\Users\LENOVO\Desktop\Electric Simulation")
AUDIT_PATH = ROOT / "_cn2_final_source_20260721" / "cn2_final_audit.json"
OUT_DIR = ROOT / "DAB_RA8P1_PCB_Notes"
DOCX_PATH = OUT_DIR / "38_RA8P1_CN2最终引脚连接表_2026-07-22.docx"
MD_PATH = OUT_DIR / "38_RA8P1_CN2最终引脚连接表_2026-07-22.md"

BLUE = "1F4D78"
HEADER_FILL = "E8EEF5"
TESTED_FILL = "EAF4EA"
NEW_FILL = "FFF4CE"
POWER_FILL = "F2F4F7"
WHITE = "FFFFFF"
MUTED = "555555"


def set_fonts(run, ascii_font="Calibri", east_asia="Microsoft YaHei"):
    run.font.name = ascii_font
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.rFonts
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.insert(0, rfonts)
    rfonts.set(qn("w:ascii"), ascii_font)
    rfonts.set(qn("w:hAnsi"), ascii_font)
    rfonts.set(qn("w:eastAsia"), east_asia)


def shade_cell(cell, fill):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = tc_pr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        tc_pr.append(shd)
    shd.set(qn("w:fill"), fill)


def set_cell_margins(cell, top=55, start=80, bottom=55, end=80):
    tc = cell._tc
    tc_pr = tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for margin, value in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{margin}"))
        if node is None:
            node = OxmlElement(f"w:{margin}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(value))
        node.set(qn("w:type"), "dxa")


def set_table_geometry(table, widths_dxa):
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    tbl_pr = table._tbl.tblPr
    tbl_layout = tbl_pr.find(qn("w:tblLayout"))
    if tbl_layout is None:
        tbl_layout = OxmlElement("w:tblLayout")
        tbl_pr.append(tbl_layout)
    tbl_layout.set(qn("w:type"), "fixed")
    tbl_w = tbl_pr.find(qn("w:tblW"))
    if tbl_w is None:
        tbl_w = OxmlElement("w:tblW")
        tbl_pr.append(tbl_w)
    tbl_w.set(qn("w:w"), str(sum(widths_dxa)))
    tbl_w.set(qn("w:type"), "dxa")
    tbl_ind = tbl_pr.find(qn("w:tblInd"))
    if tbl_ind is None:
        tbl_ind = OxmlElement("w:tblInd")
        tbl_pr.append(tbl_ind)
    tbl_ind.set(qn("w:w"), "80")
    tbl_ind.set(qn("w:type"), "dxa")

    grid = table._tbl.tblGrid
    for child in list(grid):
        grid.remove(child)
    for width in widths_dxa:
        col = OxmlElement("w:gridCol")
        col.set(qn("w:w"), str(width))
        grid.append(col)

    for row in table.rows:
        tr_pr = row._tr.get_or_add_trPr()
        cant_split = OxmlElement("w:cantSplit")
        tr_pr.append(cant_split)
        for index, cell in enumerate(row.cells):
            tc_pr = cell._tc.get_or_add_tcPr()
            tc_w = tc_pr.find(qn("w:tcW"))
            if tc_w is None:
                tc_w = OxmlElement("w:tcW")
                tc_pr.append(tc_w)
            tc_w.set(qn("w:w"), str(widths_dxa[index]))
            tc_w.set(qn("w:type"), "dxa")
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            set_cell_margins(cell)


def repeat_header(row):
    tr_pr = row._tr.get_or_add_trPr()
    tbl_header = OxmlElement("w:tblHeader")
    tbl_header.set(qn("w:val"), "true")
    tr_pr.append(tbl_header)


def write_cell(cell, text, bold=False, size=7.4, color="000000", align=WD_ALIGN_PARAGRAPH.LEFT):
    cell.text = ""
    paragraph = cell.paragraphs[0]
    paragraph.alignment = align
    paragraph.paragraph_format.space_before = Pt(0)
    paragraph.paragraph_format.space_after = Pt(0)
    paragraph.paragraph_format.line_spacing = 1.0
    run = paragraph.add_run(str(text))
    run.bold = bold
    run.font.size = Pt(size)
    run.font.color.rgb = RGBColor.from_string(color)
    set_fonts(run)


def add_field(paragraph, field_code):
    run = paragraph.add_run()
    begin = OxmlElement("w:fldChar")
    begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = field_code
    separate = OxmlElement("w:fldChar")
    separate.set(qn("w:fldCharType"), "separate")
    end = OxmlElement("w:fldChar")
    end.set(qn("w:fldCharType"), "end")
    run._r.extend([begin, instr, separate, end])


def setup_styles(doc):
    normal = doc.styles["Normal"]
    normal.font.name = "Calibri"
    normal.font.size = Pt(10.5)
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    normal.paragraph_format.space_before = Pt(0)
    normal.paragraph_format.space_after = Pt(6)
    normal.paragraph_format.line_spacing = 1.25

    for style_name, size, before, after, color in (
        ("Heading 1", 16, 18, 10, "2E74B5"),
        ("Heading 2", 13, 14, 7, "2E74B5"),
        ("Heading 3", 12, 10, 5, BLUE),
    ):
        style = doc.styles[style_name]
        style.font.name = "Calibri"
        style.font.size = Pt(size)
        style.font.bold = True
        style.font.color.rgb = RGBColor.from_string(color)
        style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
        style.paragraph_format.space_before = Pt(before)
        style.paragraph_format.space_after = Pt(after)
        style.paragraph_format.keep_with_next = True


def add_bullet(doc, text):
    p = doc.add_paragraph(style="List Bullet")
    p.paragraph_format.left_indent = Inches(0.375)
    p.paragraph_format.first_line_indent = Inches(-0.188)
    p.paragraph_format.space_after = Pt(4)
    p.paragraph_format.line_spacing = 1.25
    r = p.add_run(text)
    r.font.size = Pt(10)
    set_fonts(r)
    return p


def display_net(row):
    if row["cn2_pin"] == 51:
        return "DAB_STATUS_LED\n(原网名 N$131)"
    return row["schematic_net"]


def status_text(status):
    return {
        "tested": "保留·已实测",
        "new": "新增·三重核验",
        "power": "电源",
    }.get(status, status)


def build_docx(audit):
    doc = Document()
    section = doc.sections[0]
    section.orientation = WD_ORIENT.LANDSCAPE
    section.page_width = Cm(29.7)
    section.page_height = Cm(21.0)
    section.top_margin = Cm(1.15)
    section.bottom_margin = Cm(1.15)
    section.left_margin = Cm(1.15)
    section.right_margin = Cm(1.15)
    section.header_distance = Cm(0.55)
    section.footer_distance = Cm(0.55)
    setup_styles(doc)

    header = section.header.paragraphs[0]
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    hr = header.add_run("RA8P1 ↔ CN2 最终接口定义 · 2026-07-22")
    hr.font.size = Pt(8)
    hr.font.color.rgb = RGBColor.from_string(MUTED)
    set_fonts(hr)

    footer = section.footer.paragraphs[0]
    footer.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    fr = footer.add_run("瑞萨杯 DAB 接口文档  |  第 ")
    fr.font.size = Pt(8)
    fr.font.color.rgb = RGBColor.from_string(MUTED)
    set_fonts(fr)
    add_field(footer, " PAGE ")
    fr2 = footer.add_run(" 页")
    fr2.font.size = Pt(8)
    fr2.font.color.rgb = RGBColor.from_string(MUTED)
    set_fonts(fr2)

    title = doc.add_paragraph()
    title.alignment = WD_ALIGN_PARAGRAPH.LEFT
    title.paragraph_format.space_after = Pt(3)
    tr = title.add_run("RA8P1 ↔ CN2 最终引脚连接表")
    tr.bold = True
    tr.font.size = Pt(20)
    tr.font.color.rgb = RGBColor.from_string(BLUE)
    set_fonts(tr)
    sub = doc.add_paragraph()
    sub.paragraph_format.space_after = Pt(9)
    sr = sub.add_run("适用工程：ProPrj_瑞萨杯_2026-07-21.epro2  |  CN2：C5097366 / 120P")
    sr.font.size = Pt(9)
    sr.font.color.rgb = RGBColor.from_string(MUTED)
    set_fonts(sr)

    doc.add_heading("1. 最终结论", level=1)
    add_bullet(doc, "CN2 的 120 个焊盘已全部覆盖：46 个信号、1 个 3.3 V 电源、73 个 GND；没有遗漏网络。")
    add_bullet(doc, "原有 30 个已实测信号保持原 RA8P1 引脚不变；新增 16 个信号仅使用开发板原理图和 PCB 网表均确认已引出的空闲引脚。")
    add_bullet(doc, "45 个有名称的 CN2 网络在原理图与 PCB 焊盘网表中逐针一致。CN2.51 原理图未命名，但原理图与 PCB 均确认其拓扑为 CN2.51 → R49.2 → LED10 → GND。")
    add_bullet(doc, "以太网使用 RA8P1 的 ESWM_RMII1。RMII0 的 TXD0/TXD1 会占用已实测的 PB00/PB02，因此不得改用 RMII0。")

    doc.add_heading("2. CN2 非 GND 引脚总表", level=1)
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(5)
    rr = p.add_run("“新增·三重核验”表示：主板原理图网表、主板 PCB 焊盘网表、开发板原理图/PCB 网表与 RA8P1 复用功能表均已核对。")
    rr.font.size = Pt(8.5)
    rr.font.color.rgb = RGBColor.from_string(MUTED)
    set_fonts(rr)

    headers = ["CN2", "主板网络", "RA8P1", "开发板排针", "RA8P1 功能 / 方向", "状态与说明"]
    widths = [650, 1850, 900, 950, 2750, 2450]
    table = doc.add_table(rows=1, cols=len(headers))
    table.style = "Table Grid"
    for index, heading in enumerate(headers):
        write_cell(table.rows[0].cells[index], heading, bold=True, size=8.0, color=WHITE, align=WD_ALIGN_PARAGRAPH.CENTER)
        shade_cell(table.rows[0].cells[index], BLUE)
    repeat_header(table.rows[0])

    for row in audit["cn2_rows"]:
        if row["schematic_net"] == "GND":
            continue
        cells = table.add_row().cells
        values = [
            row["cn2_pin"],
            display_net(row),
            row["assigned_ra_pin"],
            row["header"],
            row["function"],
            status_text(row["status"]),
        ]
        fill = TESTED_FILL if row["status"] == "tested" else NEW_FILL if row["status"] == "new" else POWER_FILL
        for index, value in enumerate(values):
            align = WD_ALIGN_PARAGRAPH.CENTER if index in {0, 2, 3, 5} else WD_ALIGN_PARAGRAPH.LEFT
            write_cell(cells[index], value, size=7.2, align=align)
            shade_cell(cells[index], fill)
    set_table_geometry(table, widths)

    doc.add_heading("3. GND 与电源连接", level=1)
    ground_pins = [str(row["cn2_pin"]) for row in audit["cn2_rows"] if row["schematic_net"] == "GND"]
    p = doc.add_paragraph()
    r = p.add_run("CN2 的 73 个 GND 焊盘：")
    r.bold = True
    set_fonts(r)
    r2 = p.add_run(", ".join(ground_pins) + "。")
    set_fonts(r2)
    add_bullet(doc, "上述全部 GND 在转接板上接同一 GND 平面，再接开发板 P10.1（P10 的其他 GND 针可并联加强回流）。它们不是 MCU GPIO。")
    add_bullet(doc, "CN2.111（3V3_RA）接开发板 P9.1 的 VCC/3.3 V；P9.2～P9.6 同为 VCC，可并联，但不得接 5 V。")

    doc.add_heading("4. 新增引脚选择说明", level=1)
    new_rows = [row for row in audit["cn2_rows"] if row["status"] == "new"]
    new_table = doc.add_table(rows=1, cols=4)
    new_table.style = "Table Grid"
    new_headers = ["主板网络", "RA8P1 / 排针", "选择的硬件功能", "选择理由"]
    for index, heading in enumerate(new_headers):
        write_cell(new_table.rows[0].cells[index], heading, bold=True, size=8, color=WHITE, align=WD_ALIGN_PARAGRAPH.CENTER)
        shade_cell(new_table.rows[0].cells[index], BLUE)
    repeat_header(new_table.rows[0])
    for row in new_rows:
        if row["schematic_net"].startswith("ETH_"):
            reason = "RMII1 固定复用；避开 PB00/PB02，并已在开发板 PCB 焊盘找到"
        elif row["schematic_net"].startswith("CAN_"):
            reason = "CANFD0 原生 CTX0/CRX0；两针均已引出且不占已测信号"
        elif row["schematic_net"] == "ETH_PHY_RESET_N":
            reason = "普通 GPIO 即可；P6.32 靠近 RMII1 排针组，便于布线"
        else:
            reason = "空闲 GPIO/IRQ；开发板原理图与 PCB 网表均已确认"
        values = [row["schematic_net"], f"{row['assigned_ra_pin']} / {row['header']}", row["function"], reason]
        cells = new_table.add_row().cells
        for index, value in enumerate(values):
            write_cell(cells[index], value, size=7.4, align=WD_ALIGN_PARAGRAPH.CENTER if index == 1 else WD_ALIGN_PARAGRAPH.LEFT)
            shade_cell(cells[index], NEW_FILL)
    set_table_geometry(new_table, [2050, 1650, 2550, 3250])

    doc.add_heading("5. 必须遵守的特殊连接", level=1)
    add_bullet(doc, "RAW_FAULT_N1（CN2.33）与 RAW_FAULT_N2（CN2.29）在转接板上共同接 P401/P8.8。主板两路均由 BAT54HT1G 隔离并各自 4.7 kΩ 上拉，适合在 MCU 侧线与；P401 是 GTETRGA/POEG0 硬件关断输入。P400 与 P403 仍分别读取一次侧/二次侧故障来源。")
    add_bullet(doc, "CN2.51 接 P402/P4.1，作为 DAB_STATUS_LED 输出。主板该网尚未命名，建议在转接板网名中使用 DAB_STATUS_LED，避免沿用自动网名。")
    add_bullet(doc, "CAN 使用 CANFD0：CN2.79 → P705/CRX0，CN2.81 → P704/CTX0。网络名中的 RX/TX 是以 MCU 方向命名。")
    add_bullet(doc, "RMII 使用通道 1：TXD0=P307、TXD1=P306、TX_EN=P310、REF50CK=P905、RXD0=P906、RXD1=P907、CRS_DV=P206、MDC=P806、MDIO=P805、RX_ER=P312；PHY_RESET_N=P513。")

    doc.add_heading("6. 软件配置边界", level=1)
    add_bullet(doc, "当前 e² studio 工程中的新引脚仍处于 Disabled/未配置状态；本文件只确定硬件连接。后续启用 CANFD0、ESWM_RMII1 和新增 GPIO 时必须按本表配置。")
    add_bullet(doc, "UART9 保持 P208(RXD9)/P209(TXD9)，115200-8N1；该串口不在 CN2 上，本次没有移动或复用。")
    add_bullet(doc, "不要把 RMII1 改为 RMII0；否则会与 IN_PRECHARGE_EN(PB00) 和 IN_MAIN_EN(PB02) 冲突。")

    doc.add_heading("7. 核验依据", level=1)
    source_table = doc.add_table(rows=0, cols=2)
    source_table.style = "Table Grid"
    source_rows = [
        ("DAB 工程", audit["source_epro2"]),
        ("DAB 工程 SHA-256", audit["source_epro2_sha256"].upper()),
        ("主板原理图页", f"UUID {audit['schematic_page_uuid']}"),
        ("主板 PCB", f"UUID {audit['pcb_uuid']}"),
        ("CN2 器件", "C5097366 / HSEC8-160-01-L-DV-A-K-TR / 120P"),
        ("开发板工程", audit["developer_board_source"]),
        ("开发板 SHA-256", audit["developer_board_sha256"].upper()),
        ("校验结果", "缺失分配 0；有名网络 PCB 不一致 0；开发板引出不一致 0；CAN/RMII 复用不一致 0"),
    ]
    for label, value in source_rows:
        cells = source_table.add_row().cells
        write_cell(cells[0], label, bold=True, size=7.5)
        write_cell(cells[1], value, size=7.5)
        shade_cell(cells[0], HEADER_FILL)
    set_table_geometry(source_table, [1800, 7700])
    for row in source_table.rows:
        for cell in row.cells:
            set_cell_margins(cell, top=20, start=80, bottom=20, end=80)
    # Word requires an end paragraph after a final table.  Make it explicit
    # and tiny so the document does not gain a footer-only blank page.
    end_paragraph = doc.add_paragraph()
    end_paragraph.paragraph_format.space_before = Pt(0)
    end_paragraph.paragraph_format.space_after = Pt(0)
    end_paragraph.paragraph_format.line_spacing = 1.0
    end_run = end_paragraph.add_run(" ")
    end_run.font.size = Pt(1)
    set_fonts(end_run)

    doc.core_properties.title = "RA8P1 ↔ CN2 最终引脚连接表"
    doc.core_properties.subject = "瑞萨杯 DAB 主板 CN2 与 CPKHMI-RA8P1 开发板接口"
    doc.core_properties.author = "Codex"
    doc.core_properties.keywords = "RA8P1,CN2,C5097366,DAB,RMII,CANFD"
    doc.save(DOCX_PATH)


def build_markdown(audit):
    lines = [
        "# RA8P1 ↔ CN2 最终引脚连接表（2026-07-22）",
        "",
        "依据 `ProPrj_瑞萨杯_2026-07-21.epro2` 内部原生原理图/PCB 网表生成。CN2 为 C5097366，120P。",
        "",
        "结论：120 个焊盘全部覆盖；46 个信号、1 个 3.3 V、73 个 GND。原有 30 个已实测信号保持不变，新增 16 个信号均通过开发板原理图、开发板 PCB 与 RA8P1 复用功能核验。",
        "",
        "| CN2 | 主板网络 | RA8P1 | 开发板排针 | 功能 | 状态 |",
        "|---:|---|---|---|---|---|",
    ]
    for row in audit["cn2_rows"]:
        if row["schematic_net"] == "GND":
            continue
        net = "DAB_STATUS_LED（原网名 N$131）" if row["cn2_pin"] == 51 else row["schematic_net"]
        lines.append(f"| {row['cn2_pin']} | {net} | {row['assigned_ra_pin']} | {row['header']} | {row['function']} | {status_text(row['status'])} |")
    ground_pins = ", ".join(str(row["cn2_pin"]) for row in audit["cn2_rows"] if row["schematic_net"] == "GND")
    lines.extend([
        "",
        f"GND 焊盘（73 个）：{ground_pins}。全部接公共 GND，再接开发板 P10.1。",
        "",
        "特殊规则：CN2.29 与 CN2.33 共同接 P401/P8.8（BAT54 隔离后的硬件线与）；以太网必须使用 RMII1，不能使用会占用 PB00/PB02 的 RMII0；CN2.51 接 P402/P4.1 驱动 LED10。",
        "",
        f"DAB 工程 SHA-256：`{audit['source_epro2_sha256'].upper()}`",
        "",
        f"开发板工程 SHA-256：`{audit['developer_board_sha256'].upper()}`",
        "",
    ])
    MD_PATH.write_text("\n".join(lines), encoding="utf-8")


def audit_docx_geometry():
    doc = Document(DOCX_PATH)
    section = doc.sections[0]
    tables = []
    for index, table in enumerate(doc.tables, 1):
        tbl_w = table._tbl.tblPr.find(qn("w:tblW"))
        grids = [int(col.get(qn("w:w"))) for col in table._tbl.tblGrid]
        tables.append({"index": index, "tblW": int(tbl_w.get(qn("w:w"))), "gridSum": sum(grids), "columns": grids})
    return {
        "page_width_cm": round(section.page_width.cm, 2),
        "page_height_cm": round(section.page_height.cm, 2),
        "margins_cm": [round(section.top_margin.cm, 2), round(section.right_margin.cm, 2), round(section.bottom_margin.cm, 2), round(section.left_margin.cm, 2)],
        "normal_font": doc.styles["Normal"].font.name,
        "normal_size_pt": doc.styles["Normal"].font.size.pt,
        "tables": tables,
    }


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    audit = json.loads(AUDIT_PATH.read_text(encoding="utf-8"))
    if audit["missing_assignments"] or audit["named_pcb_mismatches"]:
        raise RuntimeError("Source audit is not clean")
    if not audit["all_breakouts_valid"] or not audit["all_mux_functions_valid"] or not audit["pin51_topology_valid"]:
        raise RuntimeError("Hardware validation is not clean")
    build_docx(audit)
    build_markdown(audit)
    print(json.dumps({"docx": str(DOCX_PATH), "markdown": str(MD_PATH), "geometry": audit_docx_geometry()}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
