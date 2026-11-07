"""Capture unmodified UART transcript plus validated CSV; requires a real COM port."""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time

FIELDS = ['run_id', 'version', 'test_id', 'tick', 'event_id', 'task_id', 'generation', 'event', 'reason', 'value', 'cycles']

def parse_row(line):
    cells = next(csv.reader([line]))
    if len(cells) != len(FIELDS) or cells == FIELDS:
        return None
    row = dict(zip(FIELDS, cells))
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,31}', row['run_id']):
        return None
    for field in ['test_id', 'tick', 'event_id', 'task_id', 'generation', 'reason', 'value', 'cycles']:
        try:
            number = int(row[field])
        except ValueError:
            return None
        if number < 0 or number > 0xFFFFFFFF:
            return None
    return row

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--seconds', type=float, default=30)
    p.add_argument('--run-id', default=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
    p.add_argument('--command', action='append', default=[])
    p.add_argument('--out', type=Path, default=Path('results/raw'))
    a = p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,31}', a.run_id) or a.seconds <= 0:
        p.error('Invalid run ID or duration')
    try:
        import serial
    except ImportError:
        p.error('Install tools/requirements.txt (pyserial). No UART data collected.')
    a.out.mkdir(parents=True, exist_ok=True)
    base = a.out / a.run_id
    # Exclusive creation protects existing raw evidence.
    with base.with_suffix('.csv').open('x', newline='', encoding='utf-8') as out, \
         base.with_suffix('.uart.txt').open('x', encoding='utf-8') as transcript:
        writer = csv.DictWriter(out, fieldnames=FIELDS)
        writer.writeheader()
        rows = malformed = 0
        with serial.Serial(a.port, a.baud, timeout=0.2) as uart:
            commands = ['run ' + a.run_id, *a.command]
            next_command = time.monotonic() + 0.5
            end = time.monotonic() + a.seconds
            final_status_sent = False
            while time.monotonic() < end:
                if commands and time.monotonic() >= next_command:
                    command = commands.pop(0)
                    uart.write((command + '\n').encode('ascii'))
                    transcript.write('# HOST_COMMAND ' + command + '\n')
                    next_command = time.monotonic() + 0.5
                if not final_status_sent and time.monotonic() >= end - 0.7:
                    uart.write(b'status\n')
                    transcript.write('# HOST_COMMAND status (end of capture)\n')
                    final_status_sent = True
                raw = uart.readline()
                if not raw:
                    continue
                line = raw.decode('ascii', errors='replace').rstrip('\r\n')
                transcript.write(line + '\n'); transcript.flush()
                row = parse_row(line)
                if row is not None:
                    writer.writerow(row); out.flush(); rows += 1
                elif line and not line.startswith('#') and line != ','.join(FIELDS):
                    malformed += 1
        metadata = {'port': a.port, 'baud': a.baud, 'seconds': a.seconds, 'rows': rows,
                    'malformed_lines': malformed, 'commands': a.command, 'run_id': a.run_id,
                    'test_verdict': 'NOT_EVALUATED', 'collected_at_utc': datetime.now(timezone.utc).isoformat()}
        base.with_suffix('.metadata.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    print(f'Collected {rows} rows; malformed={malformed}; no test pass inferred.')

if __name__ == '__main__':
    main()
