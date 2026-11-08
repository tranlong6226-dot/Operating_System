#!/usr/bin/env python3
"""Summarise RTSFR UART CSV without third-party dependencies."""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


HEADER = [
    "run_id", "version", "test_id", "tick", "event_id", "task_id",
    "generation", "event", "reason", "value", "cycles",
]


def delta32(end: int, start: int) -> int:
    return (end - start) & 0xFFFFFFFF


def load_events(path: Path) -> list[dict[str, int | str]]:
    rows: list[dict[str, int | str]] = []
    header_seen = False
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line.startswith(",".join(HEADER)):
            header_seen = True
            continue
        if not header_seen or not line or line.startswith("#"):
            continue
        fields = next(csv.reader([line]))
        if len(fields) != len(HEADER):
            continue
        row: dict[str, int | str] = dict(zip(HEADER, fields))
        try:
            for key in (
                "test_id", "tick", "event_id", "task_id", "generation",
                "reason", "value", "cycles",
            ):
                row[key] = int(str(row[key]), 10)
        except ValueError:
            continue
        rows.append(row)
    if not header_seen:
        raise ValueError("Không tìm thấy header CSV RTSFR trong log")
    return rows


def percentile(values: list[int], fraction: float) -> int:
    ordered = sorted(values)
    if not ordered:
        return 0
    index = max(0, min(len(ordered) - 1,
                       round((len(ordered) - 1) * fraction)))
    return ordered[index]


def summarize(events: list[dict[str, int | str]]) -> str:
    grouped: dict[tuple[str, str, int], list[dict[str, int | str]]] = \
        defaultdict(list)
    for event in events:
        grouped[(str(event["run_id"]), str(event["version"]),
                 int(event["test_id"]))].append(event)

    lines = [
        "# RTSFR experiment summary",
        "",
        "| Run | Version | Test | Result | Detect ticks | Recovery ticks | "
        "Trace drops | Min heap | Max monitor cycles |",
        "|---|---|---:|---|---:|---:|---:|---:|---:|",
    ]
    all_detect: list[int] = []
    all_recovery: list[int] = []

    for (run_id, version, test_id), group in sorted(grouped.items()):
        result = "N/A"
        inject: dict[int, int] = {}
        detected: dict[int, int] = {}
        first_progress: dict[int, int] = {}
        trace_drops = 0
        min_heap = 0
        max_monitor_cycles = 0

        for event in group:
            name = str(event["event"])
            event_id = int(event["event_id"])
            tick = int(event["tick"])
            if name == "TEST_PASS":
                result = "PASS"
            elif name == "TEST_FAIL":
                result = "FAIL"
            elif name == "INJECT" and event_id:
                inject[event_id] = tick
            elif name == "FAULT_DETECTED" and event_id:
                detected[event_id] = tick
            elif name in ("FIRST_PROGRESS", "RECOVERY_OK") and event_id:
                first_progress.setdefault(event_id, tick)
            elif name == "TRACE_DROPPED":
                trace_drops = max(trace_drops, int(event["value"]))
            elif name == "STATUS" and int(event["reason"]) == 6:
                min_heap = int(event["value"])
            elif name == "STATUS" and int(event["reason"]) == 15:
                max_monitor_cycles = int(event["value"])

        detect_samples = [
            delta32(tick, inject[event_id])
            for event_id, tick in detected.items() if event_id in inject
        ]
        recovery_samples = [
            delta32(tick, detected[event_id])
            for event_id, tick in first_progress.items()
            if event_id in detected
        ]
        all_detect.extend(detect_samples)
        all_recovery.extend(recovery_samples)
        detect_text = str(int(statistics.median(detect_samples))) \
            if detect_samples else "-"
        recovery_text = str(int(statistics.median(recovery_samples))) \
            if recovery_samples else "-"
        lines.append(
            f"| {run_id} | {version} | {test_id} | {result} | "
            f"{detect_text} | {recovery_text} | {trace_drops} | "
            f"{min_heap or '-'} | {max_monitor_cycles or '-'} |"
        )

    lines.extend(["", "## Aggregate latency", ""])
    for label, values in (("Detection", all_detect),
                          ("Recovery", all_recovery)):
        if values:
            lines.append(
                f"- {label}: N={len(values)}, median={int(statistics.median(values))} "
                f"ticks, p95={percentile(values, 0.95)} ticks, "
                f"min={min(values)}, max={max(values)}."
            )
        else:
            lines.append(f"- {label}: chưa có mẫu ghép cặp hợp lệ.")
    return "\n".join(lines) + "\n"


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(
        description="Tổng hợp log CSV UART của RTSFR thành Markdown")
    parser.add_argument("log", type=Path, help="File log UART chứa header CSV")
    parser.add_argument("-o", "--output", type=Path,
                        help="File Markdown đầu ra; mặc định in ra stdout")
    args = parser.parse_args()
    report = summarize(load_events(args.log))
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
