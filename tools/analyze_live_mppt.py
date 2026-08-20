import json
import math
import statistics
import sys
from collections import Counter, defaultdict
from datetime import datetime


def median(values):
    values = [value for value in values if isinstance(value, (int, float)) and math.isfinite(value)]
    return statistics.median(values) if values else float("nan")


path = sys.argv[1]
telemetry = {}
extensions = {}
events = []
with open(path, "r", encoding="utf-8") as stream:
    for raw in stream:
        try:
            row = json.loads(raw)
        except json.JSONDecodeError:
            continue
        kind = row.get("type")
        data = row.get("data") or {}
        if kind == "telemetry":
            telemetry[data.get("seq")] = row
        elif kind == "telemetry_extension":
            extensions[data.get("seq")] = row
        elif kind == "log":
            text = row.get("text", "")
            if any(token in text for token in ("[PHASE", "[ENERGY", "[MPPT", "[CHARGE", "[MODE")):
                events.append((row.get("host_time", ""), text))

joined = []
for seq, row in telemetry.items():
    data = row["data"]
    ext = extensions.get(seq, {}).get("data", {})
    if row.get("host_time", "") < "2026-08-21T02:58:31":
        continue
    joined.append((row.get("host_time", ""), data, ext))

joined.sort(key=lambda item: item[0])
run = [item for item in joined if item[1].get("control_enabled") and item[1].get("pwm_enabled")]
print(f"samples={len(joined)} run={len(run)} first={joined[0][0] if joined else '-'} last={joined[-1][0] if joined else '-'}")
if run:
    max_pin = max(item[1].get("pin_w", 0.0) for item in run)
    max_iin = max(item[1].get("iin_a", 0.0) for item in run)
    max_temp = max(max(item[1].get("temp_pri_c", 0.0), item[1].get("temp_sec_c", 0.0)) for item in run)
    faults = Counter(item[1].get("fault_mask") for item in run)
    print(f"run_range={run[0][0]}..{run[-1][0]} maxPin={max_pin:.1f}W maxIin={max_iin:.2f}A maxTemp={max_temp:.1f}C faults={dict(faults)}")

bins = defaultdict(list)
for item in run:
    vout = item[1].get("vout_v")
    if isinstance(vout, (int, float)) and 30.0 <= vout <= 46.0:
        key = round(vout * 10.0) / 10.0
        bins[key].append(item)

print("Vout n Vin Iin Iout Pin Pout Iref Vmppt lim mode raw eff cap sat stage")
for key in sorted(bins):
    group = bins[key]
    if len(group) < 3:
        continue
    data = [item[1] for item in group]
    ext = [item[2] for item in group]
    modes = Counter(item.get("modulation", "?") for item in data)
    stages = Counter(item.get("charge_stage", "?") for item in ext)
    limited = sum(bool(item.get("mppt_limited")) for item in ext) / len(ext)
    saturated = sum(bool(item.get("outer_saturated")) for item in data) / len(data)
    print(
        f"{key:4.1f} {len(group):4d} {median([x.get('vin_v') for x in data]):5.2f} "
        f"{median([x.get('iin_a') for x in data]):5.2f} {median([x.get('iout_a') for x in data]):5.2f} "
        f"{median([x.get('pin_w') for x in data]):6.1f} {median([x.get('pout_w') for x in data]):6.1f} "
        f"{median([x.get('energy_current_reference_a') for x in ext]):5.2f} "
        f"{median([x.get('mppt_voltage_target_v') for x in ext]):5.2f} {limited:3.0%} "
        f"{modes.most_common(1)[0][0]:4s} {median([x.get('raw_phase_ns') for x in data]):3.0f} "
        f"{median([x.get('effective_phase_ns') for x in data]):3.0f} {median([x.get('phase_cap_ns') for x in data]):3.0f} "
        f"{saturated:3.0%} {stages.most_common(1)[0][0]}"
    )

print("EVENTS_LAST_80")
for timestamp, text in events[-80:]:
    print(timestamp, text)
