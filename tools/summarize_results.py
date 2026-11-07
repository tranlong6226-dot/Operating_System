"""Correlate actual UART events. Missing evidence remains blank/NOT_RUN."""
from __future__ import annotations
import argparse
import csv
from collections import defaultdict
import math
from pathlib import Path
import statistics

def elapsed(start, end):
    value = (int(end) - int(start)) & 0xFFFFFFFF
    return value if value < 0x80000000 else None

def percentile95(values):
    return sorted(values)[math.ceil(0.95 * len(values)) - 1] if values else None

def summarize(rows):
    groups = defaultdict(list)
    for row in rows:
        if row.get('event_id', '0') != '0':
            key = tuple(row[x] for x in ['run_id', 'version', 'test_id', 'task_id', 'event_id'])
            groups[key].append(row)
    result = []
    for key, events in groups.items():
        kinds = {name: [e for e in events if e['event'] == name] for name in
                 ['INJECT', 'FAULT_DETECTED', 'RECOVERY_OK', 'FIRST_PROGRESS']}
        injection, fault, recovery, progress = [kinds[x] for x in kinds]
        detection = recovery_time = None
        # Ambiguous duplicates are not guessed. Recovery needs progress from NEW generation.
        if len(injection) == 1 and len(fault) == 1 and injection[0]['generation'] == fault[0]['generation']:
            detection = elapsed(injection[0]['tick'], fault[0]['tick'])
        if len(fault) == 1 and len(recovery) == 1:
            new = [e for e in progress if e['generation'] == recovery[0]['generation'] and
                   int(e['generation']) > int(fault[0]['generation'])]
            if len(new) == 1:
                recovery_time = elapsed(fault[0]['tick'], new[0]['tick'])
        result.append(dict(zip(['run_id', 'version', 'test_id', 'task_id', 'event_id'], key),
                           detection_ticks=detection, recovery_ticks=recovery_time))
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs', type=Path, nargs='+')
    p.add_argument('--out', type=Path, default=Path('results/summary'))
    p.add_argument('--tick-hz', type=int, help='Confirmed acquisition tick rate; unset -> ticks only')
    a = p.parse_args()
    if a.tick_hz is not None and a.tick_hz <= 0:
        p.error('--tick-hz must be positive')
    rows = []
    for path in a.inputs:
        with path.open(newline='', encoding='utf-8-sig') as handle:
            rows.extend(csv.DictReader(handle))
    samples = summarize(rows)
    a.out.mkdir(parents=True, exist_ok=True)
    fields = ['run_id', 'version', 'test_id', 'task_id', 'event_id', 'detection_ticks', 'recovery_ticks']
    with (a.out / 'latency_samples.csv').open('w', newline='', encoding='utf-8') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader(); writer.writerows(samples)
    lines = ['# Benchmark from supplied raw logs', '',
             'Calculated latencies are observations, not a full test verdict. Missing values are unmeasured.', '',
             '| Version | Metric | n | Median (ticks) | p95 (ticks) |', '|---|---|---:|---:|---:|']
    for version in ['V0', 'V1', 'V2', 'M5_ONLY']:
        for metric in ['detection_ticks', 'recovery_ticks']:
            values = [s[metric] for s in samples if s['version'] == version and s[metric] is not None]
            lines.append(f"| {version} | {metric} | {len(values)} | {statistics.median(values) if values else ''} | {percentile95(values) if values else ''} |")
    if a.tick_hz:
        lines += ['', f'Confirmed tick rate supplied: {a.tick_hz} Hz. Convert ticks to ms using 1000/{a.tick_hz}.']
    lost = any(r['event'] == 'TRACE_DROPPED' or
               (r['event'] == 'STATUS' and r.get('reason') in ['1', '2'] and int(r.get('value', 0)) > 0) for r in rows)
    lines += ['', f'Trace/UART loss observed: {lost}. Runs with loss require review and must not establish a PASS.',
              'CPU overhead, continuity, false positives, heap stability and hardware watchdog verdict: NOT_EVALUATED.',
              'Synthetic fixtures are for script tests only and are not experimental results.']
    (a.out / 'benchmark.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Wrote {len(samples)} correlated groups; no hardware verdict inferred.')

if __name__ == '__main__':
    main()
