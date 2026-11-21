"""Tkinter user interface for the RA8P1 DAB demonstration host."""

from __future__ import annotations

import csv
import json
import math
import queue
import time
import tkinter as tk
from collections import deque
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Any

from .protocol import (
    DemoConfig,
    PERSONALITY_CHARGE,
    PERSONALITY_CP,
    infer_device_capabilities,
    merge_telemetry,
)
from .transports import TRANSPORT_CLASSES, BaseTransport, TransportError


COLORS = {
    "bg": "#09111f",
    "panel": "#111c2f",
    "panel_2": "#16243a",
    "panel_3": "#1b2b45",
    "line": "#263854",
    "text": "#eef4ff",
    "muted": "#93a7c3",
    "cyan": "#27d3c2",
    "blue": "#4d8dff",
    "purple": "#a47cff",
    "green": "#45d483",
    "yellow": "#f4c95d",
    "orange": "#ff9a5c",
    "red": "#ff5f6d",
    "white": "#ffffff",
}

FONT = "Microsoft YaHei UI"

SERIAL_TRANSPORT_LABEL = "串口 UART"

PERSONALITY_LABELS = {
    "智能充电 + MPPT": PERSONALITY_CHARGE,
}
DEFAULT_PERSONALITY_LABEL = "智能充电 + MPPT"

BATTERY_LABELS = {
    "10S 锂离子（已实测）": "liion10",
    "12S 磷酸铁锂（已实测）": "lfp12",
}
BATTERY_STAGE_HINTS = {
    "liion10": "<30.0 V预充；越过30.0 V后CC+MPPT；接近42.0 V转CV减流",
    "lfp12": "<32.4 V预充；越过32.4 V后CC+MPPT；接近43.8 V转CV减流",
}
BATTERY_PLACEHOLDER = "请选择电池类型"

MPPT_LABELS = {
    "P&O 扰动观察": "po",
    "增量电导": "inc",
}


def _safe_float(value: Any, default: float = 0.0) -> float:
    try:
        result = float(value)
        return result if math.isfinite(result) else default
    except (TypeError, ValueError):
        return default


class MetricCard(tk.Frame):
    def __init__(self, master: tk.Misc, title: str, unit: str, color: str) -> None:
        super().__init__(master, bg=COLORS["panel_2"], highlightthickness=1, highlightbackground=COLORS["line"])
        self.color = color
        tk.Frame(self, bg=color, height=3).pack(fill="x")
        tk.Label(self, text=title, fg=COLORS["muted"], bg=COLORS["panel_2"], font=(FONT, 9)).pack(
            anchor="w", padx=12, pady=(9, 0)
        )
        row = tk.Frame(self, bg=COLORS["panel_2"])
        row.pack(fill="x", padx=12, pady=(1, 9))
        self.value = tk.Label(
            row,
            text="--",
            fg=COLORS["text"],
            bg=COLORS["panel_2"],
            font=(FONT, 20, "bold"),
        )
        self.value.pack(side="left")
        tk.Label(row, text=unit, fg=COLORS["muted"], bg=COLORS["panel_2"], font=(FONT, 9)).pack(
            side="left", padx=(5, 0), pady=(10, 0)
        )

    def set(self, value: Any, digits: int = 2, valid: bool = True) -> None:
        if value is None or not valid:
            text = "--"
            color = COLORS["muted"]
        else:
            text = f"{_safe_float(value):.{digits}f}"
            color = self.color
        self.value.configure(text=text, fg=color)


class TrendChart(tk.Canvas):
    SERIES = (
        ("pout_w", "输出功率", "W", COLORS["cyan"]),
        ("vout_v", "输出电压", "V", COLORS["blue"]),
        ("iout_a", "输出电流", "A", COLORS["purple"]),
    )

    def __init__(self, master: tk.Misc) -> None:
        super().__init__(master, bg=COLORS["panel"], highlightthickness=0, height=280)
        self.history: deque[dict[str, Any]] = deque(maxlen=180)
        self.bind("<Configure>", lambda _event: self.redraw())

    def append(self, data: dict[str, Any]) -> None:
        self.history.append(dict(data))
        self.redraw()

    def clear_history(self) -> None:
        self.history.clear()
        self.redraw()

    def redraw(self) -> None:
        self.delete("all")
        width = max(200, self.winfo_width())
        height = max(180, self.winfo_height())
        left, right, top, bottom = 58, 16, 18, 24
        plot_w = width - left - right
        strip_h = (height - top - bottom) / len(self.SERIES)
        samples = list(self.history)
        for index, (field, label, unit, color) in enumerate(self.SERIES):
            y0 = top + index * strip_h
            y1 = y0 + strip_h - 8
            self.create_rectangle(left, y0, width - right, y1, fill=COLORS["panel_2"], outline="")
            for grid in range(1, 4):
                gy = y0 + (y1 - y0) * grid / 4
                self.create_line(left, gy, width - right, gy, fill=COLORS["line"], dash=(2, 5))
            values = [_safe_float(item.get(field)) for item in samples if item.get(field) is not None]
            latest = values[-1] if values else 0.0
            maximum = max(values, default=1.0)
            minimum = min(values, default=0.0)
            if maximum - minimum < max(0.1, abs(maximum) * 0.08):
                pad = max(0.1, abs(maximum) * 0.08)
                maximum += pad
                minimum -= pad
            self.create_text(
                10,
                y0 + 10,
                text=label,
                anchor="w",
                fill=COLORS["muted"],
                font=(FONT, 8),
            )
            self.create_text(
                10,
                y0 + 31,
                text=f"{latest:.1f}{unit}",
                anchor="w",
                fill=color,
                font=(FONT, 10, "bold"),
            )
            if len(samples) >= 2:
                points: list[float] = []
                for sample_index, sample in enumerate(samples):
                    value = _safe_float(sample.get(field))
                    x = left + plot_w * sample_index / max(1, len(samples) - 1)
                    y = y1 - (value - minimum) / (maximum - minimum) * (y1 - y0)
                    points.extend((x, y))
                self.create_line(*points, fill=color, width=2, smooth=True)
        self.create_text(
            width - right,
            height - 6,
            text="最近约 18 秒",
            anchor="se",
            fill=COLORS["muted"],
            font=(FONT, 8),
        )


class SessionRecorder:
    def __init__(self, app_root: Path) -> None:
        self.app_root = app_root
        self._file: Any = None
        self.path: Path | None = None

    def open(self) -> Path:
        if self._file is None:
            log_dir = self.app_root / "logs"
            log_dir.mkdir(parents=True, exist_ok=True)
            self.path = log_dir / f"session_{datetime.now():%Y%m%d_%H%M%S}.jsonl"
            self._file = self.path.open("a", encoding="utf-8")
        return self.path  # type: ignore[return-value]

    def write(self, event: dict[str, Any]) -> None:
        self.open()
        row = {"host_time": datetime.now().astimezone().isoformat(timespec="milliseconds"), **event}
        self._file.write(json.dumps(row, ensure_ascii=False, default=str) + "\n")
        self._file.flush()

    def close(self) -> None:
        if self._file is not None:
            self._file.close()
            self._file = None


class DABHostApp:
    def __init__(self, root: tk.Tk, app_root: Path) -> None:
        self.root = root
        self.app_root = app_root
        self.events: queue.Queue[dict[str, Any]] = queue.Queue()
        self.transport: BaseTransport | None = None
        self.applied_transport: BaseTransport | None = None
        self.telemetry: dict[str, Any] = {}
        self.algorithm_details: dict[str, dict[str, Any]] = {}
        self.algorithm_signatures: dict[str, tuple[Any, ...]] = {}
        self.session_samples: deque[dict[str, Any]] = deque(maxlen=50000)
        self.device_profile: dict[str, Any] = {}
        self.observed_firmware = ""
        self.observed_protocol = ""
        self.last_telemetry_monotonic = 0.0
        self.last_config = DemoConfig()
        self.recorder = SessionRecorder(app_root)
        self._configure_root()
        self._configure_styles()
        self._build_ui()
        self._on_transport_changed()
        self._on_personality_changed(set_defaults=True)
        self.root.after(60, self._poll_events)
        self.root.after(500, self._freshness_tick)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _configure_root(self) -> None:
        self.root.title("RA8P1 智能充电 + MPPT 现场上位机")
        self.root.geometry("1460x900")
        self.root.minsize(1180, 760)
        self.root.configure(bg=COLORS["bg"])

    def _configure_styles(self) -> None:
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(
            "TCombobox",
            foreground=COLORS["text"],
            fieldbackground=COLORS["panel_3"],
            background=COLORS["panel_3"],
            bordercolor=COLORS["line"],
            arrowcolor=COLORS["cyan"],
            padding=6,
            font=(FONT, 9),
        )
        style.map("TCombobox", fieldbackground=[("readonly", COLORS["panel_3"])])
        style.configure(
            "TEntry",
            foreground=COLORS["text"],
            fieldbackground=COLORS["panel_3"],
            bordercolor=COLORS["line"],
            insertcolor=COLORS["text"],
            padding=7,
        )
        style.configure("TCheckbutton", background=COLORS["panel"], foreground=COLORS["text"], font=(FONT, 9))
        style.map("TCheckbutton", background=[("active", COLORS["panel"])])

    def _build_ui(self) -> None:
        self._build_header()
        body = tk.PanedWindow(
            self.root,
            orient="horizontal",
            bg=COLORS["bg"],
            bd=0,
            sashwidth=5,
            sashrelief="flat",
        )
        body.pack(fill="both", expand=True, padx=14, pady=(0, 12))
        self.left = tk.Frame(body, bg=COLORS["panel"], width=330)
        self.center = tk.Frame(body, bg=COLORS["bg"])
        body.add(self.left, minsize=310, width=330)
        body.add(self.center, minsize=760)
        self._build_controls(self.left)
        self._build_dashboard(self.center)

    def _build_header(self) -> None:
        header = tk.Frame(self.root, bg=COLORS["bg"], height=88)
        header.pack(fill="x", padx=18, pady=(12, 8))
        header.pack_propagate(False)
        title_area = tk.Frame(header, bg=COLORS["bg"])
        title_area.pack(side="left", fill="y")
        tk.Label(
            title_area,
            text="RA8P1  智能光储 DAB",
            fg=COLORS["text"],
            bg=COLORS["bg"],
            font=(FONT, 20, "bold"),
        ).pack(anchor="w", pady=(6, 0))
        connection = tk.Frame(header, bg=COLORS["panel"], highlightthickness=1, highlightbackground=COLORS["line"])
        connection.pack(side="right", fill="y", padx=(20, 0))
        inner = tk.Frame(connection, bg=COLORS["panel"])
        inner.pack(fill="both", expand=True, padx=12, pady=9)
        self.transport_var = tk.StringVar(value=SERIAL_TRANSPORT_LABEL)
        tk.Label(
            inner,
            text="串口 UART",
            fg=COLORS["cyan"],
            bg=COLORS["panel"],
            font=(FONT, 9, "bold"),
            padx=8,
        ).grid(row=0, column=0, rowspan=2, padx=(0, 8), sticky="ns")
        self.setting_label_vars = [tk.StringVar(), tk.StringVar()]
        self.setting_value_vars = [tk.StringVar(), tk.StringVar()]
        self.setting_labels: list[tk.Label] = []
        self.setting_entries: list[ttk.Entry] = []
        for index in range(2):
            label = tk.Label(
                inner,
                textvariable=self.setting_label_vars[index],
                fg=COLORS["muted"],
                bg=COLORS["panel"],
                font=(FONT, 8),
            )
            label.grid(row=0, column=1 + index, sticky="w", padx=3)
            entry = ttk.Entry(inner, textvariable=self.setting_value_vars[index], width=13)
            entry.grid(row=1, column=1 + index, sticky="ew", padx=3)
            self.setting_labels.append(label)
            self.setting_entries.append(entry)
        self.connect_button = tk.Button(
            inner,
            text="连接",
            command=self._toggle_connection,
            bg=COLORS["blue"],
            fg=COLORS["white"],
            activebackground="#3977df",
            activeforeground=COLORS["white"],
            relief="flat",
            font=(FONT, 9, "bold"),
            padx=16,
            cursor="hand2",
        )
        self.connect_button.grid(row=0, column=3, rowspan=2, padx=(10, 0), sticky="ns")
        self.link_pill = tk.Label(
            header,
            text="●  未连接",
            fg=COLORS["muted"],
            bg=COLORS["bg"],
            font=(FONT, 9, "bold"),
        )
        self.link_pill.pack(side="right", padx=(0, 2))

    def _section_label(self, master: tk.Misc, text: str) -> tk.Label:
        label = tk.Label(
            master,
            text=text,
            fg=COLORS["text"],
            bg=COLORS["panel"],
            font=(FONT, 11, "bold"),
        )
        label.pack(anchor="w", padx=16, pady=(15, 8))
        return label

    def _build_controls(self, master: tk.Frame) -> None:
        action_frame = tk.Frame(master, bg=COLORS["panel"])
        action_frame.pack(fill="x", side="bottom", padx=14, pady=14)

        scroll_shell = tk.Frame(master, bg=COLORS["panel"])
        scroll_shell.pack(fill="both", expand=True)
        controls_canvas = tk.Canvas(
            scroll_shell,
            bg=COLORS["panel"],
            highlightthickness=0,
            bd=0,
        )
        controls_scrollbar = ttk.Scrollbar(
            scroll_shell, orient="vertical", command=controls_canvas.yview
        )
        controls_canvas.configure(yscrollcommand=controls_scrollbar.set)
        controls_canvas.pack(side="left", fill="both", expand=True)
        controls_scrollbar.pack(side="right", fill="y")
        controls_content = tk.Frame(controls_canvas, bg=COLORS["panel"])
        controls_window = controls_canvas.create_window(
            (0, 0), window=controls_content, anchor="nw"
        )
        controls_content.bind(
            "<Configure>",
            lambda _event: controls_canvas.configure(
                scrollregion=controls_canvas.bbox("all")
            ),
        )
        controls_canvas.bind(
            "<Configure>",
            lambda event: controls_canvas.itemconfigure(
                controls_window, width=event.width
            ),
        )
        controls_canvas.bind(
            "<MouseWheel>",
            lambda event: controls_canvas.yview_scroll(
                int(-event.delta / 120), "units"
            ),
        )
        self.controls_canvas = controls_canvas
        master = controls_content

        self._section_label(master, "运行模式")
        self.personality_var = tk.StringVar(value=DEFAULT_PERSONALITY_LABEL)
        persona_frame = tk.Frame(master, bg=COLORS["panel"])
        persona_frame.pack(fill="x", padx=12)
        for row, label in enumerate(PERSONALITY_LABELS):
            radio = tk.Radiobutton(
                persona_frame,
                text=label,
                value=label,
                variable=self.personality_var,
                command=self._on_personality_changed,
                indicatoron=False,
                anchor="w",
                relief="flat",
                bg=COLORS["panel_2"],
                fg=COLORS["text"],
                selectcolor=COLORS["blue"],
                activebackground=COLORS["panel_3"],
                activeforeground=COLORS["white"],
                font=(FONT, 9, "bold"),
                padx=12,
                pady=7,
                cursor="hand2",
            )
            radio.grid(row=row, column=0, sticky="ew", pady=2)
        persona_frame.columnconfigure(0, weight=1)
        self._section_label(master, "功能配置")
        form = tk.Frame(master, bg=COLORS["panel"])
        form.pack(fill="x", padx=16)
        self.target_voltage_var = tk.StringVar(value="42.0")
        self.target_power_var = tk.StringVar(value="40")
        self.power_limit_var = tk.StringVar(value="100")
        self.vin_set_var = tk.StringVar(value="24")
        self.load_ohm_var = tk.StringVar(value="18")
        self.form_cells: dict[str, tk.Frame] = {}
        self.form_labels: dict[str, tk.Label] = {}
        form_fields = (
            ("voltage", "目标电压 (V)", self.target_voltage_var),
            ("power", "目标功率 (W)", self.target_power_var),
            ("power_limit", "功率上限 (W)", self.power_limit_var),
        )
        for index, (key, label, variable) in enumerate(form_fields):
            row, column = divmod(index, 2)
            cell = tk.Frame(form, bg=COLORS["panel"])
            cell.grid(row=row, column=column, sticky="ew", padx=(0, 6) if column == 0 else (6, 0), pady=4)
            field_label = tk.Label(
                cell,
                text=label,
                fg=COLORS["muted"],
                bg=COLORS["panel"],
                font=(FONT, 8),
            )
            field_label.pack(anchor="w")
            ttk.Entry(cell, textvariable=variable).pack(fill="x", pady=(2, 0))
            self.form_cells[key] = cell
            self.form_labels[key] = field_label
        form.columnconfigure(0, weight=1)
        form.columnconfigure(1, weight=1)

        options = tk.Frame(master, bg=COLORS["panel"])
        options.pack(fill="x", padx=16, pady=(7, 0))
        self.modulation_var = tk.StringVar(value="AUTO")
        # Do not silently reuse a chemistry on a fresh field session.  The two
        # profiles have different precharge thresholds (30.0 V versus 32.4 V),
        # so a default selection can make a valid LFP precharge test run as
        # Li-ion CC without the operator noticing.
        self.battery_var = tk.StringVar(value=BATTERY_PLACEHOLDER)
        self.mppt_var = tk.StringVar(value="P&O 扰动观察")
        self.option_rows: dict[str, tuple[tk.Label, ttk.Combobox]] = {}
        for row, (key, label, variable, values) in enumerate(
            (
                ("modulation", "调制测试策略", self.modulation_var, ["AUTO", "SPS", "EPS", "TPS"]),
                (
                    "battery",
                    "电池类型",
                    self.battery_var,
                    [BATTERY_PLACEHOLDER, *BATTERY_LABELS],
                ),
                ("mppt", "MPPT 算法（仅 CC 中段）", self.mppt_var, list(MPPT_LABELS)),
            )
        ):
            option_label = tk.Label(options, text=label, fg=COLORS["muted"], bg=COLORS["panel"], font=(FONT, 8))
            option_label.grid(
                row=row, column=0, sticky="w", pady=4
            )
            option_combo = ttk.Combobox(options, textvariable=variable, values=values, state="readonly", width=20)
            option_combo.grid(
                row=row, column=1, sticky="ew", pady=4, padx=(8, 0)
            )
            self.option_rows[key] = (option_label, option_combo)
        options.columnconfigure(1, weight=1)
        # Kept in DemoConfig for protocol compatibility only; CP is not part
        # of the current delivered UI or acceptance scope.
        self.cp_damping_var = tk.BooleanVar(value=False)
        self.apply_button = tk.Button(
            action_frame,
            text="应用模式（不启动）",
            command=self._apply_configuration,
            bg=COLORS["blue"],
            fg=COLORS["white"],
            activebackground="#3977df",
            activeforeground=COLORS["white"],
            relief="flat",
            font=(FONT, 10, "bold"),
            pady=9,
            cursor="hand2",
        )
        self.apply_button.pack(fill="x", pady=(0, 7))
        row = tk.Frame(action_frame, bg=COLORS["panel"])
        row.pack(fill="x")
        self.start_button = tk.Button(
            row,
            text="启动功率",
            command=self._start_power,
            bg=COLORS["green"],
            fg="#07150e",
            activebackground="#32b66c",
            relief="flat",
            font=(FONT, 10, "bold"),
            pady=10,
            cursor="hand2",
        )
        self.start_button.pack(side="left", fill="x", expand=True, padx=(0, 4))
        self.stop_button = tk.Button(
            row,
            text="STOP",
            command=self._stop_power,
            bg=COLORS["red"],
            fg=COLORS["white"],
            activebackground="#dc4755",
            activeforeground=COLORS["white"],
            relief="flat",
            font=(FONT, 11, "bold"),
            pady=10,
            cursor="hand2",
        )
        self.stop_button.pack(side="left", fill="x", expand=True, padx=(4, 0))
        def scroll_controls(event: tk.Event) -> str:
            controls_canvas.yview_scroll(int(-event.delta / 120), "units")
            return "break"

        def bind_control_wheel(widget: tk.Misc) -> None:
            widget.bind(
                "<MouseWheel>",
                scroll_controls,
                add="+",
            )
            for child in widget.winfo_children():
                bind_control_wheel(child)

        bind_control_wheel(controls_content)

    def _build_dashboard(self, master: tk.Frame) -> None:
        ribbon = tk.Frame(master, bg=COLORS["panel"], height=52, highlightthickness=1, highlightbackground=COLORS["line"])
        ribbon.pack(fill="x", padx=(10, 0), pady=(0, 10))
        ribbon.pack_propagate(False)
        self.status_labels: dict[str, tk.Label] = {}
        for key, title in (
            ("state", "STATE"),
            ("control_architecture", "ARCH"),
            ("modulation", "MOD"),
            ("energy_mode", "ENERGY"),
            ("fault", "FAULT"),
        ):
            cell = tk.Frame(ribbon, bg=COLORS["panel"])
            cell.pack(side="left", fill="y", padx=14)
            tk.Label(cell, text=title, fg=COLORS["muted"], bg=COLORS["panel"], font=(FONT, 7)).pack(
                anchor="w", pady=(8, 0)
            )
            label = tk.Label(cell, text="--", fg=COLORS["text"], bg=COLORS["panel"], font=(FONT, 9, "bold"))
            label.pack(anchor="w")
            self.status_labels[key] = label
        self.simulated_badge = tk.Label(
            ribbon,
            text="SIMULATED",
            fg=COLORS["bg"],
            bg=COLORS["yellow"],
            font=(FONT, 8, "bold"),
            padx=8,
            pady=3,
        )

        metrics = tk.Frame(master, bg=COLORS["bg"])
        metrics.pack(fill="x", padx=(10, 0), pady=(0, 10))
        self.metric_cards = {
            "vin_v": MetricCard(metrics, "输入电压", "V", COLORS["blue"]),
            "vout_v": MetricCard(metrics, "输出电压", "V", COLORS["cyan"]),
            "iout_a": MetricCard(metrics, "输出电流", "A", COLORS["purple"]),
            "pout_w": MetricCard(metrics, "输出功率", "W", COLORS["green"]),
            "board_efficiency_pct": MetricCard(metrics, "板端计算效率", "%", COLORS["yellow"]),
            "temp_max_c": MetricCard(metrics, "最高温度", "°C", COLORS["orange"]),
        }
        for column, card in enumerate(self.metric_cards.values()):
            card.grid(row=0, column=column, sticky="nsew", padx=(0, 7) if column < 5 else 0)
            metrics.columnconfigure(column, weight=1)

        lower = tk.PanedWindow(master, orient="vertical", bg=COLORS["bg"], bd=0, sashwidth=5)
        lower.pack(fill="both", expand=True, padx=(10, 0))
        chart_panel = tk.Frame(lower, bg=COLORS["panel"], highlightthickness=1, highlightbackground=COLORS["line"])
        lower.add(chart_panel, minsize=230, height=315)
        chart_header = tk.Frame(chart_panel, bg=COLORS["panel"])
        chart_header.pack(fill="x", padx=14, pady=(10, 0))
        tk.Label(chart_header, text="实时趋势", fg=COLORS["text"], bg=COLORS["panel"], font=(FONT, 11, "bold")).pack(
            side="left"
        )
        tk.Button(
            chart_header,
            text="清空曲线",
            command=lambda: self.chart.clear_history(),
            bg=COLORS["panel_2"],
            fg=COLORS["muted"],
            activebackground=COLORS["panel_3"],
            activeforeground=COLORS["text"],
            relief="flat",
            font=(FONT, 8),
            cursor="hand2",
        ).pack(side="right")
        self.chart = TrendChart(chart_panel)
        self.chart.pack(fill="both", expand=True, padx=8, pady=(4, 6))

        tabs_panel = tk.Frame(lower, bg=COLORS["panel"], highlightthickness=1, highlightbackground=COLORS["line"])
        lower.add(tabs_panel, minsize=220)
        tab_bar = tk.Frame(tabs_panel, bg=COLORS["panel"])
        tab_bar.pack(fill="x", padx=12, pady=(8, 0))
        self.tab_var = tk.StringVar(value="参数详情")
        self.tab_buttons: dict[str, tk.Button] = {}
        for name in ("参数详情", "本地控制状态", "通信日志"):
            button = tk.Button(
                tab_bar,
                text=name,
                command=lambda selected=name: self._select_tab(selected),
                bg=COLORS["panel_2"],
                fg=COLORS["text"],
                relief="flat",
                font=(FONT, 9, "bold"),
                padx=14,
                pady=6,
                cursor="hand2",
            )
            button.pack(side="left", padx=(0, 5))
            self.tab_buttons[name] = button

        self.tab_content = tk.Frame(tabs_panel, bg=COLORS["panel"])
        self.tab_content.pack(fill="both", expand=True, padx=12, pady=9)
        self.details_frame = tk.Frame(self.tab_content, bg=COLORS["panel"])
        self.advanced_frame = tk.Frame(self.tab_content, bg=COLORS["panel"])
        self.log_frame = tk.Frame(self.tab_content, bg=COLORS["panel"])
        self._build_details_tab()
        self._build_advanced_tab()
        self._build_log_tab()
        self._select_tab("参数详情")

    def _build_details_tab(self) -> None:
        self.detail_labels: dict[str, tk.Label] = {}
        fields = (
            ("pin_w", "输入功率", "W"),
            ("iin_a", "输入电流", "A"),
            ("temp_pri_c", "原边温度", "°C"),
            ("temp_sec_c", "副边温度", "°C"),
            ("effective_phase_ns", "有效相移", "ns"),
            ("phase_cap_ns", "相移上限", "ns"),
            ("voltage_ref_v", "电压目标", "V"),
            ("power_limit_w", "功率上限", "W"),
            ("charge_stage", "充电阶段", ""),
            ("battery_profile", "电池类型", ""),
            ("mppt_power_target_w", "MPPT 输入实测功率", "W"),
            ("table_entry_id", "参数表条目", ""),
            ("ipc_peer", "双核 IPC", ""),
            ("ai_state", "AI 增强", ""),
            ("pwm_state", "PWM", ""),
            ("valid_state", "测量有效", ""),
        )
        for index, (key, title, unit) in enumerate(fields):
            row, column = divmod(index, 4)
            cell = tk.Frame(self.details_frame, bg=COLORS["panel_2"], highlightthickness=1, highlightbackground=COLORS["line"])
            cell.grid(row=row, column=column, sticky="nsew", padx=4, pady=4)
            tk.Label(cell, text=title, fg=COLORS["muted"], bg=COLORS["panel_2"], font=(FONT, 8)).pack(
                anchor="w", padx=9, pady=(6, 0)
            )
            label = tk.Label(
                cell,
                text=f"-- {unit}" if unit else "--",
                fg=COLORS["text"],
                bg=COLORS["panel_2"],
                font=(FONT, 9, "bold"),
            )
            label.pack(anchor="w", padx=9, pady=(1, 7))
            self.detail_labels[key] = label
        for column in range(4):
            self.details_frame.columnconfigure(column, weight=1)

    def _build_advanced_tab(self) -> None:
        self.algorithm_detail_labels: dict[str, tk.Label] = {}
        titles = {
            "VAD": "宽输入参数选择  VAD",
            "MSW": "SPS/EPS/TPS 自动过渡  MSW",
        }
        for index, family in enumerate(("VAD", "MSW")):
            row, column = divmod(index, 2)
            panel = tk.Frame(
                self.advanced_frame,
                bg=COLORS["panel_2"],
                highlightthickness=1,
                highlightbackground=COLORS["line"],
            )
            panel.grid(row=row, column=column, sticky="nsew", padx=4, pady=4)
            tk.Label(
                panel,
                text=titles[family],
                fg=COLORS["cyan"],
                bg=COLORS["panel_2"],
                font=(FONT, 9, "bold"),
            ).pack(anchor="w", padx=10, pady=(8, 3))
            label = tk.Label(
                panel,
                text="尚未收到该类记录",
                justify="left",
                anchor="nw",
                wraplength=260,
                fg=COLORS["text"],
                bg=COLORS["panel_2"],
                font=("Cascadia Mono", 8),
            )
            label.pack(fill="both", expand=True, padx=10, pady=(0, 8))
            self.algorithm_detail_labels[family] = label
        self.advanced_frame.columnconfigure(0, weight=1)
        self.advanced_frame.columnconfigure(1, weight=1)
        self.advanced_frame.columnconfigure(2, weight=1)
        self.advanced_frame.rowconfigure(0, weight=1)

    def _build_log_tab(self) -> None:
        top = tk.Frame(self.log_frame, bg=COLORS["panel"])
        top.pack(fill="x", pady=(0, 6))
        # Hardware tuning must always leave a replayable command/telemetry
        # trail.  Operators may still opt out explicitly, but a fresh upper-
        # computer session records from its first connection event by default.
        self.record_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            top,
            text="记录本机会话 JSONL",
            variable=self.record_var,
            command=self._toggle_recording,
        ).pack(side="left")
        self.record_path_label = tk.Label(
            top, text="自动记录：连接后创建日志", fg=COLORS["muted"], bg=COLORS["panel"], font=(FONT, 8)
        )
        self.record_path_label.pack(side="left", padx=10)
        for label, command in (("导出曲线 CSV", self._export_samples_csv),):
            tk.Button(
                top,
                text=label,
                command=command,
                bg=COLORS["panel_2"],
                fg=COLORS["muted"],
                relief="flat",
                font=(FONT, 8),
                cursor="hand2",
            ).pack(side="right", padx=(5, 0))
        tk.Button(
            top,
            text="清空日志",
            command=self._clear_log,
            bg=COLORS["panel_2"],
            fg=COLORS["muted"],
            relief="flat",
            font=(FONT, 8),
            cursor="hand2",
        ).pack(side="right")
        self.log_text = tk.Text(
            self.log_frame,
            height=8,
            bg="#07101d",
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            selectbackground=COLORS["blue"],
            relief="flat",
            font=("Cascadia Mono", 8),
            wrap="none",
            state="disabled",
        )
        self.log_text.pack(fill="both", expand=True)
        self.log_text.tag_configure("info", foreground=COLORS["muted"])
        self.log_text.tag_configure("warning", foreground=COLORS["yellow"])
        self.log_text.tag_configure("error", foreground=COLORS["red"])
        self.log_text.tag_configure("tx", foreground=COLORS["blue"])
        self.log_text.tag_configure("rx", foreground=COLORS["cyan"])

    def _select_tab(self, selected: str) -> None:
        self.tab_var.set(selected)
        self.details_frame.pack_forget()
        self.advanced_frame.pack_forget()
        self.log_frame.pack_forget()
        target = {
            "参数详情": self.details_frame,
            "本地控制状态": self.advanced_frame,
            "通信日志": self.log_frame,
        }[selected]
        target.pack(fill="both", expand=True)
        for name, button in self.tab_buttons.items():
            button.configure(bg=COLORS["blue"] if name == selected else COLORS["panel_2"])

    def _on_transport_changed(self) -> None:
        specs = (("端口", "COM4"), ("波特率", "115200"))
        for index, (label, value) in enumerate(specs):
            self.setting_label_vars[index].set(label)
            self.setting_value_vars[index].set(value)
        self.start_button.configure(state="normal")

    def _connection_settings(self) -> dict[str, Any]:
        values = [variable.get().strip() for variable in self.setting_value_vars]
        return {"port": values[0], "baudrate": int(values[1])}

    def _toggle_connection(self) -> None:
        if self.transport and (
            self.transport.connected or self.connect_button.cget("text") in ("断开", "取消")
        ):
            self.transport.disconnect()
            self.transport = None
            self.link_pill.configure(text="●  未连接", fg=COLORS["muted"])
            self.connect_button.configure(text="连接")
            self._reset_received_state()
            return
        if self.transport:
            self.transport.disconnect()
        try:
            self._reset_received_state()
            self.transport = TRANSPORT_CLASSES["serial"](self.events)
            self.transport.connect(self._connection_settings())
            self.connect_button.configure(text="断开")
            self._append_log(f"正在连接 {self.transport.label}…", "info")
        except (TransportError, ValueError) as exc:
            self.transport = None
            messagebox.showerror("连接失败", str(exc), parent=self.root)
            self._append_log(str(exc), "error")

    def _reset_received_state(self) -> None:
        """Prevent telemetry from a previous link authorizing the next link."""

        self.telemetry.clear()
        self.algorithm_details.clear()
        self.algorithm_signatures.clear()
        self.device_profile.clear()
        self.observed_firmware = ""
        self.observed_protocol = ""
        self.last_telemetry_monotonic = 0.0
        self.applied_transport = None
        self.chart.clear_history()
        for card in self.metric_cards.values():
            card.set(None, valid=False)
        for label in self.status_labels.values():
            label.configure(text="--", fg=COLORS["text"])
        for label in self.detail_labels.values():
            label.configure(text="--")
        for label in self.algorithm_detail_labels.values():
            label.configure(text="尚未收到该类记录", fg=COLORS["text"])
        self.simulated_badge.pack_forget()

    def _on_personality_changed(self, set_defaults: bool = True) -> None:
        personality = PERSONALITY_LABELS[self.personality_var.get()]
        visible_fields = set()
        for key, cell in self.form_cells.items():
            if key in visible_fields:
                cell.grid()
            else:
                cell.grid_remove()
        visible_options = {"battery", "mppt"}
        for key, widgets in self.option_rows.items():
            for widget in widgets:
                if key in visible_options:
                    widget.grid()
                else:
                    widget.grid_remove()
        self.form_labels["power"].configure(text="目标功率 (W)")
        self.form_labels["power_limit"].configure(text="功率上限 (W)")
        if not set_defaults:
            return
        defaults = (42.0, 100.0, 360.0, "AUTO")
        self.target_voltage_var.set(f"{defaults[0]:.1f}")
        self.target_power_var.set(f"{defaults[1]:.0f}")
        self.power_limit_var.set(f"{defaults[2]:.0f}")
        self.modulation_var.set(defaults[3])

    def _read_config(self) -> DemoConfig:
        personality = PERSONALITY_LABELS[self.personality_var.get()]
        battery_label = self.battery_var.get()
        if personality == PERSONALITY_CHARGE and battery_label not in BATTERY_LABELS:
            raise ValueError("智能充电必须先明确选择锂离子或磷酸铁锂，禁止使用隐式默认电池类型。")
        config = DemoConfig(
            personality=personality,
            modulation=self.modulation_var.get(),
            target_voltage_v=float(self.target_voltage_var.get()),
            target_current_a=1.0,
            target_power_w=float(self.target_power_var.get()),
            power_limit_w=float(self.power_limit_var.get()),
            battery_profile=BATTERY_LABELS.get(battery_label, "liion10"),
            mppt_algorithm=MPPT_LABELS[self.mppt_var.get()],
            solar_with_charger=True,
            cp_damping=self.cp_damping_var.get(),
            vin_set_v=24.0,
            load_ohm=18.0,
        )
        config.validate()
        return config

    def _require_transport(self) -> BaseTransport:
        if not self.transport or not self.transport.connected:
            raise TransportError("请先连接 COM4 串口。")
        return self.transport

    def _require_fresh_ready_state(self, transport: BaseTransport) -> None:
        if transport.key == "simulator":
            return
        if not self.last_telemetry_monotonic or (time.monotonic() - self.last_telemetry_monotonic) > 3.0:
            raise TransportError("尚无新鲜状态遥测；请等待状态刷新后再配置或启动。")
        state = str(self.telemetry.get("state", ""))
        if state != "READY" or bool(self.telemetry.get("pwm_enabled")):
            raise TransportError(f"当前状态为 {state or 'UNKNOWN'}，配置/启动要求 READY 且 PWM OFF。")
        fault_mask = int(_safe_float(self.telemetry.get("fault_mask", 0)))
        if fault_mask:
            raise TransportError(f"存在故障 0x{fault_mask:X}，已阻止操作。")

    def _apply_configuration(self) -> None:
        try:
            transport = self._require_transport()
            self._require_fresh_ready_state(transport)
            config = self._read_config()
            transport.send_action("configure", config)
            self.last_config = config
            self.applied_transport = transport
            mode_note = ("；调制与参数由下位机自主选择" if
                         config.personality == PERSONALITY_CP else
                         f" / {config.modulation}")
            self._append_log(
                f"已应用 {self.personality_var.get()}{mode_note}；未发送启动命令。",
                "warning",
            )
        except (TransportError, ValueError) as exc:
            messagebox.showerror("无法应用", str(exc), parent=self.root)
            self._append_log(str(exc), "error")

    def _start_power(self) -> None:
        try:
            transport = self._require_transport()
            self._require_fresh_ready_state(transport)
            config = self._read_config()
            if transport is not self.applied_transport or config != self.last_config:
                raise TransportError("当前链路尚未应用这组目标参数，请先点击“应用模式（不启动）”。")
            if (
                transport.key == "serial"
                and config.personality == PERSONALITY_CP
                and not bool(self.telemetry.get("schedule_enabled"))
            ):
                raise TransportError("下位机尚未确认参数表装载成功，已阻止启动；请查看通信日志并重新应用模式。")
            simulated = transport.key == "simulator"
            if config.personality == PERSONALITY_CHARGE:
                charge_label = next(
                    label for label, value in BATTERY_LABELS.items()
                    if value == config.battery_profile
                )
                target_summary = (
                    f"电池：{charge_label}\n"
                    f"MPPT：{self.mppt_var.get()}\n"
                    f"阶段阈值：{BATTERY_STAGE_HINTS[config.battery_profile]}\n"
                    "20–26 V范围、搜索参数、充电状态机、限幅和调制：全部由下位机自主决定\n"
                )
            else:
                target_summary = (
                    f"目标：{config.target_voltage_v:.1f} V, "
                    f"{config.target_power_w:.1f} W；上限 {config.power_limit_w:.1f} W\n"
                    "SPS/EPS/TPS、参数束、冷启动和平滑过渡：全部由下位机自主决定\n"
                )
            prompt = (
                f"链路：{transport.label}\n"
                f"运行模式：{self.personality_var.get()}\n"
                f"{target_summary}\n"
                + ("这是离线模拟，不会访问硬件。" if simulated else "此操作可能启动真实功率级。确认现场限流、负载和停机条件已就绪？")
            )
            if not messagebox.askyesno("确认启动", prompt, parent=self.root):
                return
            transport.send_action("start", config)
            self._append_log("已发送显式启动请求。", "warning")
        except (TransportError, ValueError) as exc:
            messagebox.showerror("启动被阻止", str(exc), parent=self.root)
            self._append_log(str(exc), "error")

    def _stop_power(self) -> None:
        try:
            transport = self._require_transport()
            transport.send_action("stop", self.last_config)
            self._append_log("已发送 STOP；该按钮不等待确认。", "warning")
        except TransportError as exc:
            self._append_log(str(exc), "error")

            self._append_log(str(exc), "error")

    def _poll_events(self) -> None:
        processed = 0
        while processed < 200:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            processed += 1
            self._handle_event(event)
        self.root.after(60, self._poll_events)

    def _handle_event(self, event: dict[str, Any]) -> None:
        event_transport = event.get("transport")
        if event_transport and (
            self.transport is None or event_transport != self.transport.key
        ):
            return
        if self.record_var.get() and event.get("type") not in ("rx",):
            try:
                self.recorder.write(event)
                if self.recorder.path:
                    self.record_path_label.configure(text=self.recorder.path.name)
            except OSError as exc:
                self.record_var.set(False)
                self._append_log(f"记录失败: {exc}", "error")
        event_type = event.get("type")
        if event_type == "connection":
            connected = bool(event.get("connected"))
            connecting = bool(event.get("connecting"))
            if connected:
                self.link_pill.configure(text="●  已连接", fg=COLORS["green"])
                self.connect_button.configure(text="断开")
            elif connecting:
                self.link_pill.configure(text="●  连接中", fg=COLORS["yellow"])
                self.connect_button.configure(text="取消")
            else:
                self.link_pill.configure(text="●  未连接", fg=COLORS["muted"])
                self.connect_button.configure(text="连接")
        elif event_type in ("telemetry", "status"):
            data = event.get("data")
            if isinstance(data, dict):
                self.telemetry = merge_telemetry(self.telemetry, data)
                self.last_telemetry_monotonic = time.monotonic()
                firmware = data.get("firmware")
                if firmware:
                    self._set_device_identity(
                        str(event.get("source") or event_transport or ""),
                        firmware=str(firmware),
                    )
                self._update_dashboard()
                if event_type == "telemetry":
                    self.chart.append(self.telemetry)
                    self.session_samples.append(
                        {
                            "host_time": datetime.now().astimezone().isoformat(timespec="milliseconds"),
                            "transport": event_transport or event.get("source", ""),
                            **self.telemetry,
                        }
                    )
        elif event_type == "telemetry_extension":
            data = event.get("data")
            if isinstance(data, dict):
                self.telemetry = merge_telemetry(self.telemetry, data)
                self.last_telemetry_monotonic = time.monotonic()
                self._update_dashboard()
        elif event_type == "algorithm_detail":
            family = str(event.get("family", ""))
            data = event.get("data")
            if family in self.algorithm_detail_labels and isinstance(data, dict):
                self.algorithm_details[family] = dict(data)
                self._update_algorithm_detail(family, data)
        elif event_type == "identity":
            firmware = event.get("firmware") or event.get("protocol") or "device"
            self._set_device_identity(
                str(event.get("source") or event_transport or ""),
                firmware=str(event.get("firmware") or ""),
                protocol=str(event.get("protocol") or ""),
            )
            self._append_log(f"设备标识: {firmware}", "info")
        elif event_type == "log":
            self._append_log(str(event.get("text", "")), str(event.get("level", "info")))
        elif event_type == "tx":
            self._append_log(f"TX  {event.get('text', '')}", "tx")
        elif event_type == "rx":
            text = str(event.get("text", ""))
            if not text.startswith(("DAB:", "ALG:", "VAD:", "ATI:", "MSW:", "CPD:")):
                self._append_log(f"RX  {text}", "rx")
        elif event_type == "ack":
            result = str(event.get("result"))
            level = "info" if result == "ok" else "error"
            self._set_device_identity("can", can_version=int(event.get("version", -1)))
            self._append_log(
                f"CAN ACK seq={event.get('sequence')} result={result} "
                f"rxDrop={event.get('rx_dropped_low8')} errors={event.get('error_events_low8')}",
                level,
            )
        elif event_type == "protocol":
            message = event.get("message", event)
            level = "error" if isinstance(message, dict) and message.get("ok") is False else "info"
            self._append_log(f"协议: {json.dumps(message, ensure_ascii=False, default=str)}", level)

    def _set_device_identity(
        self,
        source: str,
        *,
        firmware: str = "",
        protocol: str = "",
        can_version: int | None = None,
    ) -> None:
        source = source or (self.transport.key if self.transport else "")
        if firmware:
            self.observed_firmware = firmware
        if protocol:
            self.observed_protocol = protocol
        profile = infer_device_capabilities(
            source,
            firmware=self.observed_firmware,
            protocol=self.observed_protocol,
            can_version=can_version,
        )
        self.device_profile = profile

    def _update_algorithm_detail(self, family: str, data: dict[str, Any]) -> None:
        on_off = lambda value: "ON" if bool(value) else "OFF"
        if family == "VAD":
            text = (
                f"enabled={on_off(data.get('enabled'))}  active={on_off(data.get('active'))}  "
                f"band={data.get('band', data.get('band_code', '--'))}\n"
                f"VinFilt={_safe_float(data.get('filtered_vin_v')):.3f} V  "
                f"gain={_safe_float(data.get('gain_scale')):.4f}\n"
                f"Kp={_safe_float(data.get('effective_kp_ns_per_a')):.3f} ns/A  "
                f"Ki={_safe_float(data.get('effective_ki_us_per_as')):.4f} us/(A·s)\n"
                f"FF={_safe_float(data.get('feedforward_delta_ns')):.3f} ns  "
                f"headroom={_safe_float(data.get('phase_headroom_pct')):.1f}%\n"
                f"Pest={_safe_float(data.get('estimated_max_output_power_w')):.1f} W  "
                f"limited={on_off(data.get('actuator_limited'))}"
            )
            signature = (
                bool(data.get("enabled")),
                bool(data.get("active")),
                data.get("band_code"),
                bool(data.get("actuator_limited")),
            )
        elif family == "ATI":
            text = (
                f"stage={data.get('stage', '--')}  running={on_off(data.get('running'))}\n"
                f"step/inject={_safe_float(data.get('perturbation_ns')):.1f}/"
                f"{_safe_float(data.get('injection_ns')):.1f} ns\n"
                f"plantGain={_safe_float(data.get('local_gain_a_per_ns')):.6f} A/ns\n"
                f"Kp/Ki={_safe_float(data.get('recommended_kp_ns_per_a')):.3f}/"
                f"{_safe_float(data.get('recommended_ki_us_per_as')):.4f}\n"
                f"phaseCap={_safe_float(data.get('recommended_phase_cap_ns')):.1f} ns  "
                f"valid={on_off(data.get('result_valid'))}\n"
                f"applied={on_off(data.get('result_applied'))}  failure={data.get('failure', '--')}"
            )
            signature = (
                data.get("stage"),
                bool(data.get("running")),
                bool(data.get("result_valid")),
                bool(data.get("result_applied")),
                data.get("failure"),
            )
        elif family == "MSW":
            text = (
                f"stage={data.get('stage', '--')}  "
                f"{data.get('source_mode', '--')} -> {data.get('target_mode', '--')}\n"
                f"inner target={_safe_float(data.get('target_primary_inner_ns')):.1f}/"
                f"{_safe_float(data.get('target_secondary_inner_ns')):.1f} ns\n"
                f"baseline={_safe_float(data.get('baseline_metric')):.3f}  "
                f"error={_safe_float(data.get('metric_error_pct')):.2f}%\n"
                f"elapsed={_safe_float(data.get('elapsed_ms')):.1f} ms  "
                f"failure={data.get('failure', '--')}"
            )
            signature = (
                data.get("stage"),
                data.get("source_mode"),
                data.get("target_mode"),
                data.get("failure"),
            )
        else:
            text = (
                f"enabled={on_off(data.get('enabled'))}  saturated={on_off(data.get('saturated'))}\n"
                f"P target/active={_safe_float(data.get('target_power_w')):.1f}/"
                f"{_safe_float(data.get('active_power_target_w')):.1f} W\n"
                f"P error={_safe_float(data.get('power_error_w')):.2f} W  "
                f"Vnom={_safe_float(data.get('nominal_output_v')):.2f} V\n"
                f"I ff/damp/int={_safe_float(data.get('feedforward_current_a')):.3f}/"
                f"{_safe_float(data.get('damping_current_a')):.3f}/"
                f"{_safe_float(data.get('integral_current_a')):.3f} A\n"
                f"I request={_safe_float(data.get('requested_current_a')):.3f} A"
            )
            signature = (bool(data.get("enabled")), bool(data.get("saturated")))
        self.algorithm_detail_labels[family].configure(text=text)
        if self.algorithm_signatures.get(family) != signature:
            self.algorithm_signatures[family] = signature
            first_line = text.splitlines()[0]
            self._append_log(f"{family} 状态更新: {first_line}", "info")

    def _freshness_tick(self) -> None:
        if self.transport and self.transport.connected and self.last_telemetry_monotonic:
            age = time.monotonic() - self.last_telemetry_monotonic
            if age > 3.0:
                self.link_pill.configure(text=f"●  遥测超时 {age:.0f}s", fg=COLORS["yellow"])
        self.root.after(500, self._freshness_tick)

    def _update_dashboard(self) -> None:
        data = self.telemetry
        valid = bool(data.get("valid", True))
        temp_max = max(_safe_float(data.get("temp_pri_c")), _safe_float(data.get("temp_sec_c")))
        values = {
            "vin_v": data.get("vin_v"),
            "vout_v": data.get("vout_v"),
            "iout_a": data.get("iout_a"),
            "pout_w": data.get("pout_w"),
            "board_efficiency_pct": data.get("board_efficiency_pct"),
            "temp_max_c": temp_max if (data.get("temp_pri_c") is not None or data.get("temp_sec_c") is not None) else None,
        }
        digits = {"pout_w": 1, "board_efficiency_pct": 1, "temp_max_c": 1}
        for key, card in self.metric_cards.items():
            card.set(values.get(key), digits.get(key, 2), valid=valid or key == "temp_max_c")
        state = str(data.get("state", "--"))
        fault_mask = int(_safe_float(data.get("fault_mask", 0)))
        self.status_labels["state"].configure(
            text=state,
            fg=COLORS["red"] if state == "FAULT" else (COLORS["green"] if state == "RUN" else COLORS["text"]),
        )
        self.status_labels["control_architecture"].configure(text=str(data.get("control_architecture", "--")))
        self.status_labels["modulation"].configure(
            text=str(data.get("modulation", data.get("modulation_effective", "--"))), fg=COLORS["cyan"]
        )
        self.status_labels["energy_mode"].configure(text=str(data.get("energy_mode", "--")))
        self.status_labels["fault"].configure(
            text=f"0x{fault_mask:X}", fg=COLORS["green"] if fault_mask == 0 else COLORS["red"]
        )
        if data.get("simulated"):
            self.simulated_badge.pack(side="right", padx=12, pady=13)
        else:
            self.simulated_badge.pack_forget()
        detail_values = {
            "pin_w": self._format(data.get("pin_w"), "W", 1),
            "iin_a": self._format(data.get("iin_a"), "A", 2),
            "temp_pri_c": self._format(data.get("temp_pri_c"), "°C", 1),
            "temp_sec_c": self._format(data.get("temp_sec_c"), "°C", 1),
            "effective_phase_ns": self._format(data.get("effective_phase_ns"), "ns", 1),
            "phase_cap_ns": self._format(data.get("phase_cap_ns"), "ns", 0),
            "voltage_ref_v": self._format(data.get("voltage_ref_v"), "V", 1),
            "power_limit_w": self._format(data.get("power_limit_w"), "W", 1),
            "charge_stage": str(data.get("charge_stage", "--")),
            "battery_profile": str(data.get("battery_profile", "--")),
            "mppt_power_target_w": self._format(data.get("mppt_power_target_w"), "W", 1),
            "table_entry_id": str(data.get("table_entry_id", "--")),
            "ipc_peer": "OK" if data.get("ipc_peer_seen") and not data.get("ipc_peer_stale") else "--",
            "ai_state": "APPLIED" if data.get("ai_applied") else "LOCAL",
            "pwm_state": "ON" if data.get("pwm_enabled") else "OFF",
            "valid_state": "VALID" if valid else "INVALID",
        }
        for key, label in self.detail_labels.items():
            label.configure(text=detail_values.get(key, "--"))

    @staticmethod
    def _format(value: Any, unit: str, digits: int) -> str:
        if value is None:
            return "--"
        return f"{_safe_float(value):.{digits}f} {unit}".strip()

    def _append_log(self, text: str, level: str = "info") -> None:
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log_text.configure(state="normal")
        self.log_text.insert("end", f"{timestamp}  {text}\n", level if level in ("info", "warning", "error", "tx", "rx") else "info")
        lines = int(self.log_text.index("end-1c").split(".")[0])
        if lines > 1500:
            self.log_text.delete("1.0", "200.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _clear_log(self) -> None:
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def _toggle_recording(self) -> None:
        if self.record_var.get():
            try:
                path = self.recorder.open()
                self.record_path_label.configure(text=path.name)
                self.recorder.write(
                    {
                        "type": "session_start",
                        "ui_version": "0.3-plan-scope",
                        "transport_selection": self.transport_var.get(),
                    }
                )
                self._append_log(f"开始记录本机会话：{path}", "info")
            except OSError as exc:
                self.record_var.set(False)
                messagebox.showerror("无法记录", str(exc), parent=self.root)
        else:
            path = self.recorder.path
            self.recorder.close()
            self.record_path_label.configure(text=f"已停止：{path.name}" if path else "未记录")

    def _export_samples_csv(self) -> None:
        if not self.session_samples:
            messagebox.showinfo("暂无数据", "尚未收到可导出的遥测样本。", parent=self.root)
            return
        export_dir = self.app_root / "exports"
        export_dir.mkdir(parents=True, exist_ok=True)
        filename = filedialog.asksaveasfilename(
            parent=self.root,
            title="导出遥测曲线 CSV",
            initialdir=str(export_dir),
            initialfile=f"telemetry_{datetime.now():%Y%m%d_%H%M%S}.csv",
            defaultextension=".csv",
            filetypes=(("CSV 数据", "*.csv"), ("所有文件", "*.*")),
        )
        if not filename:
            return
        try:
            samples = list(self.session_samples)
            preferred = [
                "host_time",
                "transport",
                "seq",
                "state",
                "valid",
                "pwm_enabled",
                "fault_mask",
                "vin_v",
                "vout_v",
                "iin_a",
                "iout_a",
                "pin_w",
                "pout_w",
                "board_efficiency_pct",
                "temp_pri_c",
                "temp_sec_c",
                "modulation",
                "control_architecture",
                "energy_mode",
            ]
            all_keys = set().union(*(sample.keys() for sample in samples))
            fieldnames = [key for key in preferred if key in all_keys]
            fieldnames.extend(sorted(all_keys.difference(fieldnames)))
            with Path(filename).open("w", newline="", encoding="utf-8-sig") as csv_file:
                writer = csv.DictWriter(csv_file, fieldnames=fieldnames, extrasaction="ignore")
                writer.writeheader()
                for sample in samples:
                    row = {
                        key: json.dumps(value, ensure_ascii=False, default=str)
                        if isinstance(value, (dict, list, tuple))
                        else value
                        for key, value in sample.items()
                    }
                    writer.writerow(row)
            self._append_log(f"已导出 {len(samples)} 个遥测样本：{filename}", "info")
            messagebox.showinfo("导出完成", f"已导出 {len(samples)} 个样本。", parent=self.root)
        except OSError as exc:
            messagebox.showerror("导出失败", str(exc), parent=self.root)

    def close(self) -> None:
        if self.transport:
            self.transport.disconnect()
        self.recorder.close()
        self.root.destroy()
