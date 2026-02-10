#!/usr/bin/env python3
"""
Open Octave Firmware V3 - Test Log Summariser

Usage examples:
  python summarise_testlog.py --test 1 --input test1.csv --output test1_summary.csv
  python summarise_testlog.py --test 2 --input test2.csv --output test2_summary.csv --expected_presses 10
  python summarise_testlog.py --test 3 --input test3.csv --output test3_summary.csv --within_ms 20

Assumptions:
- Each test is stored in its own CSV file (your plan).
- CSV is "clean" (header + data lines), but we also tolerate CSV_BEGIN/CSV_END markers.
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from statistics import mean, pstdev
from typing import Dict, List, Optional, Tuple


# ---------------------------
# Basic stats helpers
# ---------------------------

def percentile(values: List[float], p: float) -> float:
    """
    Nearest-rank percentile with linear interpolation (robust, simple).
    p is in [0, 100].
    """
    if not values:
        return float("nan")
    if p <= 0:
        return float(min(values))
    if p >= 100:
        return float(max(values))

    xs = sorted(values)
    n = len(xs)
    # position in [0, n-1]
    pos = (p / 100.0) * (n - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return float(xs[lo])
    frac = pos - lo
    return float(xs[lo] * (1.0 - frac) + xs[hi] * frac)


@dataclass
class Row:
    run_id: int
    event_id: int
    mode: str
    event_type: str
    key_index: int
    repeat_streak: int
    input_to_audio_ms: int
    step_led_cmd_ms: int
    step_servo_cmd_ms: int
    autoplay_timing_error_ms: int
    success: int
    error_code: int


def parse_int(field: str, default: int = 0) -> int:
    field = (field or "").strip()
    if field == "":
        return default
    try:
        return int(field)
    except ValueError:
        # Sometimes Arduino logs can include stray whitespace; last resort:
        try:
            return int(float(field))
        except ValueError:
            return default


def load_rows(path: str) -> List[Row]:
    rows: List[Row] = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        # Filter out marker lines if they exist
        raw_lines = []
        for line in f:
            stripped = line.strip()
            if not stripped:
                continue
            if stripped in ("CSV_BEGIN", "CSV_DATA", "CSV_END"):
                continue
            raw_lines.append(line)

        reader = csv.DictReader(raw_lines)
        required = [
            "run_id","event_id","mode","event_type","key_index","repeat_streak",
            "input_to_audio_ms","step_led_cmd_ms","step_servo_cmd_ms","autoplay_timing_error_ms",
            "success","error_code"
        ]
        for r in reader:
            # tolerate missing columns (but your header should match)
            for col in required:
                if col not in r:
                    r[col] = ""

            rows.append(Row(
                run_id=parse_int(r["run_id"]),
                event_id=parse_int(r["event_id"]),
                mode=(r["mode"] or "").strip(),
                event_type=(r["event_type"] or "").strip(),
                key_index=parse_int(r["key_index"], default=-1),
                repeat_streak=parse_int(r["repeat_streak"]),
                input_to_audio_ms=parse_int(r["input_to_audio_ms"], default=-1),
                step_led_cmd_ms=parse_int(r["step_led_cmd_ms"], default=-1),
                step_servo_cmd_ms=parse_int(r["step_servo_cmd_ms"], default=-1),
                autoplay_timing_error_ms=parse_int(r["autoplay_timing_error_ms"], default=-1),
                success=parse_int(r["success"], default=0),
                error_code=parse_int(r["error_code"], default=-1),
            ))
    return rows


def group_by_key(rows: List[Row]) -> Dict[int, List[Row]]:
    by: Dict[int, List[Row]] = {}
    for r in rows:
        by.setdefault(r.key_index, []).append(r)
    return by


def write_summary_csv(output_path: str, header: List[str], data_rows: List[List[object]]) -> None:
    with open(output_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        for row in data_rows:
            w.writerow(row)


# ---------------------------
# Test 1: Manual latency
# ---------------------------

def summarise_test1(rows: List[Row]) -> Tuple[List[str], List[List[object]]]:
    manual = [r for r in rows if r.event_type == "MANUAL_PRESS" and r.success == 1]
    # keep only valid latency values (>=0). if you ever log -1, we ignore it.
    manual = [r for r in manual if r.input_to_audio_ms >= 0]

    by_key = group_by_key(manual)

    def latency_list(rs: List[Row]) -> List[float]:
        return [float(r.input_to_audio_ms) for r in rs]

    header = [
        "scope",
        "key_index",
        "press_count",
        "lat_mean_ms",
        "lat_p95_ms",
        "lat_max_ms",
        "lat_std_ms",
    ]

    out: List[List[object]] = []

    # All keys combined
    all_lat = latency_list(manual)
    if all_lat:
        out.append([
            "ALL_KEYS", "ALL",
            len(all_lat),
            round(mean(all_lat), 3),
            round(percentile(all_lat, 95), 3),
            round(max(all_lat), 3),
            round(pstdev(all_lat), 3),
        ])
    else:
        out.append(["ALL_KEYS", "ALL", 0, "", "", "", ""])

    # Per key
    for k in sorted(by_key.keys()):
        lat = latency_list(by_key[k])
        if not lat:
            continue
        out.append([
            "PER_KEY", k,
            len(lat),
            round(mean(lat), 3),
            round(percentile(lat, 95), 3),
            round(max(lat), 3),
            round(pstdev(lat), 3),
        ])

    return header, out


# ---------------------------
# Test 2: Single-key reliability + latency stability
# ---------------------------

def summarise_test2(rows: List[Row], expected_presses: int) -> Tuple[List[str], List[List[object]]]:
    manual = [r for r in rows if r.event_type == "MANUAL_PRESS"]
    # For reliability, count even if latency is weird, but we still want success==1 presses.
    manual_ok = [r for r in manual if r.success == 1]

    by_key = group_by_key(manual_ok)

    header = [
        "scope",
        "key_index",
        "observed_presses",
        "expected_presses",
        "missed_press_rate",
        "max_repeat_streak",
        "lat_mean_ms",
        "lat_p95_ms",
        "lat_max_ms",
        "lat_std_ms",
    ]

    out: List[List[object]] = []

    def latency_vals(rs: List[Row]) -> List[float]:
        return [float(r.input_to_audio_ms) for r in rs if r.input_to_audio_ms >= 0]

    # All keys combined (mostly for sanity)
    all_lat = latency_vals(manual_ok)
    max_streak_all = max((r.repeat_streak for r in manual_ok), default=0)
    obs_all = len(manual_ok)
    miss_rate_all = ""
    if expected_presses > 0:
        miss_rate_all = round(max(0, expected_presses - obs_all) / expected_presses, 6)

    out.append([
        "ALL_KEYS", "ALL",
        obs_all, expected_presses,
        miss_rate_all,
        max_streak_all,
        round(mean(all_lat), 3) if all_lat else "",
        round(percentile(all_lat, 95), 3) if all_lat else "",
        round(max(all_lat), 3) if all_lat else "",
        round(pstdev(all_lat), 3) if all_lat else "",
    ])

    # Per key (this is what you’ll actually report in Test 2)
    for k in sorted(by_key.keys()):
        rs = by_key[k]
        obs = len(rs)
        max_streak = max((r.repeat_streak for r in rs), default=0)
        miss_rate = ""
        if expected_presses > 0:
            miss_rate = round(max(0, expected_presses - obs) / expected_presses, 6)

        lat = latency_vals(rs)

        out.append([
            "PER_KEY", k,
            obs, expected_presses,
            miss_rate,
            max_streak,
            round(mean(lat), 3) if lat else "",
            round(percentile(lat, 95), 3) if lat else "",
            round(max(lat), 3) if lat else "",
            round(pstdev(lat), 3) if lat else "",
        ])

    return header, out


# ---------------------------
# Test 3: Autoplay timing accuracy + command latencies
# ---------------------------

def summarise_test3(rows: List[Row], within_ms: int) -> Tuple[List[str], List[List[object]]]:
    auto = [r for r in rows if r.event_type == "AUTO_STEP" and r.success == 1]

    # timing error can be negative; keep as-is
    timing = [float(r.autoplay_timing_error_ms) for r in auto if r.autoplay_timing_error_ms != -1]
    abs_timing = [abs(x) for x in timing]

    led_cmd = [float(r.step_led_cmd_ms) for r in auto if r.step_led_cmd_ms >= 0]
    servo_cmd = [float(r.step_servo_cmd_ms) for r in auto if r.step_servo_cmd_ms >= 0]

    def pct_within(vals: List[float], threshold: int) -> str:
        if not vals:
            return ""
        ok = sum(1 for v in vals if v <= threshold)
        return round((ok / len(vals)) * 100.0, 3)

    header = [
        "metric_group",
        "step_count",
        "mean_ms",
        "p95_ms",
        "max_ms",
        "mean_abs_ms",
        "max_abs_ms",
        f"pct_abs_within_{within_ms}ms",
        f"pct_abs_within_50ms",
    ]

    out: List[List[object]] = []

    # Timing error group
    out.append([
        "AUTOPLAY_TIMING_ERROR",
        len(timing),
        round(mean(timing), 3) if timing else "",
        round(percentile(timing, 95), 3) if timing else "",
        round(max(timing), 3) if timing else "",
        round(mean(abs_timing), 3) if abs_timing else "",
        round(max(abs_timing), 3) if abs_timing else "",
        pct_within(abs_timing, within_ms),
        pct_within(abs_timing, 50),
    ])

    # LED cmd latency group
    out.append([
        "LED_CMD_LATENCY",
        len(led_cmd),
        round(mean(led_cmd), 3) if led_cmd else "",
        round(percentile(led_cmd, 95), 3) if led_cmd else "",
        round(max(led_cmd), 3) if led_cmd else "",
        "", "", "", ""
    ])

    # Servo cmd latency group
    out.append([
        "SERVO_CMD_LATENCY",
        len(servo_cmd),
        round(mean(servo_cmd), 3) if servo_cmd else "",
        round(percentile(servo_cmd, 95), 3) if servo_cmd else "",
        round(max(servo_cmd), 3) if servo_cmd else "",
        "", "", "", ""
    ])

    return header, out


# ---------------------------
# CLI
# ---------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--test", type=int, choices=[1, 2, 3], required=True, help="Which test summary to produce (1, 2, or 3).")
    ap.add_argument("--input", required=True, help="Path to cleaned CSV file for that test.")
    ap.add_argument("--output", required=True, help="Path to write the summary CSV.")
    ap.add_argument("--expected_presses", type=int, default=10, help="Test 2: expected number of presses (default 10).")
    ap.add_argument("--within_ms", type=int, default=20, help="Test 3: threshold for timing accuracy (default 20ms).")
    args = ap.parse_args()

    rows = load_rows(args.input)

    if args.test == 1:
        header, out = summarise_test1(rows)
    elif args.test == 2:
        header, out = summarise_test2(rows, expected_presses=args.expected_presses)
    else:
        header, out = summarise_test3(rows, within_ms=args.within_ms)

    write_summary_csv(args.output, header, out)
    print(f"Wrote summary to: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
