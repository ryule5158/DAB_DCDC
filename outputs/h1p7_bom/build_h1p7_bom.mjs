import fs from "node:fs/promises";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const root = String.raw`C:\Users\26488\Desktop\Electric Simulation\Electric Simulation`;
const csvPath = `${root}\\DAB_RA8P1_PCB_Notes\\26_DCDC_BOM_首板硬件定版.csv`;
const outputDir = `${root}\\outputs\\h1p7_bom`;
const outputPath = `${outputDir}\\DAB_RA8P1_首板硬件定版BOM_H1.7.xlsx`;

const csvText = await fs.readFile(csvPath, "utf8");
const bomLastRow = csvText.trimEnd().split(/\r?\n/).length;
const workbook = await Workbook.fromCSV(csvText, { sheetName: "首板BOM_H1.7" });
const bom = workbook.worksheets.getItem("首板BOM_H1.7");
const summary = workbook.worksheets.add("硬件冻结说明");

bom.showGridLines = false;
bom.freezePanes.freezeRows(1);
bom.freezePanes.freezeColumns(2);
const used = bom.getUsedRange();
used.format.font = { name: "Microsoft YaHei", size: 9, color: "#17202A" };
used.format.verticalAlignment = "center";
used.format.wrapText = true;
bom.getRange("A1:J1").format = {
  fill: "#155E75",
  font: { name: "Microsoft YaHei", size: 10, bold: true, color: "#FFFFFF" },
  horizontalAlignment: "center",
  verticalAlignment: "center",
  wrapText: true,
  borders: { preset: "outside", style: "medium", color: "#0E4656" },
};
bom.getRange(`A1:J${bomLastRow}`).format.borders = {
  insideHorizontal: { style: "thin", color: "#D9E2E8" },
  bottom: { style: "thin", color: "#AAB7C2" },
};
bom.getRange(`A1:A${bomLastRow}`).format.columnWidth = 15;
bom.getRange(`B1:B${bomLastRow}`).format.columnWidth = 29;
bom.getRange(`C1:C${bomLastRow}`).format.columnWidth = 8;
bom.getRange(`D1:D${bomLastRow}`).format.columnWidth = 31;
bom.getRange(`E1:E${bomLastRow}`).format.columnWidth = 34;
bom.getRange(`F1:F${bomLastRow}`).format.columnWidth = 18;
bom.getRange(`G1:G${bomLastRow}`).format.columnWidth = 16;
bom.getRange(`H1:H${bomLastRow}`).format.columnWidth = 31;
bom.getRange(`I1:I${bomLastRow}`).format.columnWidth = 52;
bom.getRange(`J1:J${bomLastRow}`).format.columnWidth = 45;
bom.getRange("A1:J1").format.rowHeight = 30;
bom.getRange(`A2:J${bomLastRow}`).format.rowHeight = 44;
bom.getRange(`C2:C${bomLastRow}`).format.horizontalAlignment = "center";
bom.getRange(`G2:G${bomLastRow}`).format.horizontalAlignment = "center";

const table = bom.tables.add(`A1:J${bomLastRow}`, true, "HardwareBOMH17");
table.style = "TableStyleMedium2";
table.showFilterButton = true;
table.showBandedRows = true;

const statusRange = bom.getRange(`G2:G${bomLastRow}`);
statusRange.conditionalFormats.add("containsText", {
  text: "必装",
  format: { fill: "#DCFCE7", font: { color: "#166534", bold: true } },
});
statusRange.conditionalFormats.add("containsText", {
  text: "DNP",
  format: { fill: "#FEF3C7", font: { color: "#92400E" } },
});
statusRange.conditionalFormats.add("containsText", {
  text: "已购锁定",
  format: { fill: "#DBEAFE", font: { color: "#1E40AF", bold: true } },
});

summary.showGridLines = false;
summary.freezePanes.freezeRows(3);
summary.getRange("A1:F1").merge();
summary.getRange("A1").values = [["DAB RA8P1 首板硬件冻结说明 H1.7"]];
summary.getRange("A1:F1").format = {
  fill: "#155E75",
  font: { name: "Microsoft YaHei", size: 16, bold: true, color: "#FFFFFF" },
  horizontalAlignment: "left",
  verticalAlignment: "center",
};
summary.getRange("A2:F2").merge();
summary.getRange("A2").values = [["日期：2026-07-13。此表用于画原理图、PCB、采购和送板前审查；控制算法未决项不等于硬件缺件。"]];
summary.getRange("A2:F2").format = {
  fill: "#E8F1F5",
  font: { name: "Microsoft YaHei", size: 10, color: "#24404C" },
  wrapText: true,
};

summary.getRange("A4:C4").values = [["冻结项目", "H1.7 结论", "执行状态"]];
const rows = [
  ["唯一仿真模型", "DAB_RA8P1_Final_Physical_MOSFET.slx", "已冻结"],
  ["功率拓扑", "双有源全桥 DAB；原副边各 4×AONS66917", "已冻结"],
  ["变压器", "PQ4040/PC95；3:6 匝；1:2；Lm=57.5/230uH", "已购锁定"],
  ["传输漏感", "T1 自身副边折算 0.50uH±20%；无外接电感和跳线位", "已冻结"],
  ["输入共模电感", "每绕组 2mH/30A；不设旁路铜桥", "已购锁定"],
  ["输出共模电感", "不安装且不预留", "已删除"],
  ["输出滤波电感", "SER2915H-222KL；2.2uH±10%；DCR≤2.05mΩ", "已冻结"],
  ["门极驱动", "UCC21520；0/10.18V；4.7Ω开通；2.2Ω关断；120ns", "已冻结"],
  ["隔离门极电源", "每半桥 UCC33421-Q1 + TLV61046A；满功率保持环境≤85°C", "有条件冻结"],
  ["快速过流", "TMCS1133B7A；原边约65A；副边约40A；直达POEG/锁存", "已冻结"],
  ["输出电容", "8×1000uF/80V + 8×10uF/100V；桥前另有94uF+HF bank", "已冻结"],
  ["采样侧电源", "5V_CTRL经磁珠形成5V_SENSE，四颗AMC直接由5V_SENSE供电；TLV773只带3V3_AFE且设计负载<100mA", "已冻结"],
  ["母线寄生目标", "每个桥的正负母线换流总环路约≤10nH；Vds重复峰值目标<80V", "PCB强制项"],
  ["吸收网络", "首轮低压双脉冲装10Ω+470pF/每管；实测后才允许改值", "调试冻结"],
  ["BOM 行数", "见右侧公式", "自动统计"],
];
const summaryLastRow = 4 + rows.length;
summary.getRange(`A5:C${summaryLastRow}`).values = rows;
summary.getRange(`B${summaryLastRow}`).formulas = [[`=COUNTA('首板BOM_H1.7'!B2:B${bomLastRow})`]];

summary.getRange("E4:F4").values = [["Gerber 前必须关闭", "判据"]];
summary.getRange("E5:F9").values = [
  ["RA8P1 针脚映射", "GPT/ADC/POEG 与真实开发板连接器逐针核对"],
  ["机械封装", "T1/LCM1/LOUT/TMCS/保险座/连接器按实物尺寸冻结"],
  ["变压器验收", "Lm、0.40–0.60uH 漏感、DCR、耐压、匝间电容"],
  ["原理图 ERC", "三地隔离、八路Gate/Kelvin、故障链逐网通过"],
  ["PCB 人工审查", "四个换流环路、热过孔、爬电槽、无细颈"],
];
summary.getRange("E11:F11").values = [["首板装配后", "必须完成"]];
summary.getRange("E12:F16").values = [
  ["双脉冲", "Vgs、Miller抬升、死区、Vds峰值、吸收电阻温升"],
  ["短路保护", "TMCS OC→RAW_FAULT_N→POEG/DIS/锁存的真实时延"],
  ["300W连续", "MOS/T1/LCM1/LOUT/COUT热像与纹波"],
  ["500W短测", "从1s开始逐级放行；不得直接长期运行"],
  ["软件待办", "18V升压侧相移方向与EPS/TPS/调频标定"],
];

summary.getRange("A4:C4").format = summary.getRange("E4:F4").format = summary.getRange("E11:F11").format = {
  fill: "#334155",
  font: { name: "Microsoft YaHei", size: 10, bold: true, color: "#FFFFFF" },
  horizontalAlignment: "center",
  verticalAlignment: "center",
};
summary.getRange(`A5:C${summaryLastRow}`).format = {
  font: { name: "Microsoft YaHei", size: 10, color: "#17202A" },
  wrapText: true,
  verticalAlignment: "center",
  borders: { preset: "inside", style: "thin", color: "#D7E0E7" },
};
summary.getRange("E5:F9").format = summary.getRange("E12:F16").format = {
  font: { name: "Microsoft YaHei", size: 10, color: "#17202A" },
  wrapText: true,
  verticalAlignment: "center",
  borders: { preset: "inside", style: "thin", color: "#D7E0E7" },
};
summary.getRange(`C5:C${summaryLastRow}`).format.horizontalAlignment = "center";
summary.getRange(`C5:C${summaryLastRow}`).conditionalFormats.add("containsText", {
  text: "冻结",
  format: { fill: "#DCFCE7", font: { color: "#166534", bold: true } },
});
summary.getRange(`C5:C${summaryLastRow}`).conditionalFormats.add("containsText", {
  text: "强制",
  format: { fill: "#FEE2E2", font: { color: "#991B1B", bold: true } },
});
summary.getRange(`A1:A${summaryLastRow}`).format.columnWidth = 22;
summary.getRange(`B1:B${summaryLastRow}`).format.columnWidth = 62;
summary.getRange(`C1:C${summaryLastRow}`).format.columnWidth = 18;
summary.getRange(`D1:D${summaryLastRow}`).format.columnWidth = 4;
summary.getRange(`E1:E${summaryLastRow}`).format.columnWidth = 23;
summary.getRange(`F1:F${summaryLastRow}`).format.columnWidth = 55;
summary.getRange("A1:F1").format.rowHeight = 34;
summary.getRange("A2:F2").format.rowHeight = 36;
summary.getRange(`A4:F${summaryLastRow}`).format.rowHeight = 38;

await fs.mkdir(outputDir, { recursive: true });
const inspect = await workbook.inspect({
  kind: "table",
  range: `硬件冻结说明!A1:F${summaryLastRow}`,
  include: "values,formulas",
  tableMaxRows: 20,
  tableMaxCols: 8,
});
console.log(inspect.ndjson);
const errors = await workbook.inspect({
  kind: "match",
  searchTerm: "#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A",
  options: { useRegex: true, maxResults: 50 },
  summary: "formula error scan",
});
console.log(errors.ndjson);

for (const [name, range, file] of [
  ["硬件冻结说明", `A1:F${summaryLastRow}`, "preview_summary.png"],
  ["首板BOM_H1.7", "A1:J28", "preview_bom_head.png"],
  ["首板BOM_H1.7", `A${Math.max(2, bomLastRow - 27)}:J${bomLastRow}`, "preview_bom_tail.png"],
]) {
  const preview = await workbook.render({ sheetName: name, range, scale: 1, format: "png" });
  await fs.writeFile(`${outputDir}\\${file}`, new Uint8Array(await preview.arrayBuffer()));
}

const output = await SpreadsheetFile.exportXlsx(workbook);
await output.save(outputPath);
console.log(`OUTPUT=${outputPath}`);
