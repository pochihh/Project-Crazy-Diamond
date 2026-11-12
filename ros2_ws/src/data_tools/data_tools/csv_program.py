"""Exact CSV command names. No interpolation or row scheduling occurs in Python."""
import csv
import io
import math
import re
from pathlib import Path

COMMANDS = {**{f'axis{a}_reference_mm': a for a in range(6)},
            **{f'axis{a}_duty': 6+a for a in range(6)},
            **{f'do{i}': 12+i for i in range(4)},
            **{f'ao{i}_raw': 16+i for i in range(2)}}
MAX_VALUES = 2_000_000
MAX_BYTES = 32_000_000


def parse_csv(content):
    if len(content.encode('utf-8')) > MAX_BYTES:
        raise ValueError('CSV exceeds 32 MB')
    rows = csv.reader(line for line in io.StringIO(content.lstrip('\ufeff')) if line.strip() and not line.lstrip().startswith('#'))
    header = next(rows, [])
    recognized = [(i, COMMANDS[name.strip()]) for i, name in enumerate(header) if name.strip() in COMMANDS]
    warnings = [f'Ignored unknown column: {name}' for name in header if name.strip() not in COMMANDS]
    channels = [c for _, c in recognized]
    if not channels:
        raise ValueError('No supported command columns')
    if len(set(channels)) != len(channels) or any(a in channels and a+6 in channels for a in range(6)):
        raise ValueError('Duplicate channel or both reference and duty for one axis')
    values = []
    for number, row in enumerate(rows, 1):
        if len(row) != len(header):
            raise ValueError(f'Row {number}: expected {len(header)} cells')
        for index, channel in recognized:
            try:
                value = float(row[index])
            except ValueError:
                raise ValueError(f'Row {number}, {header[index]}: numeric value required') from None
            if not math.isfinite(value):
                raise ValueError(f'Row {number}: non-finite value')
            if channel >= 12 and (value != int(value) or not 0 <= value <= (1 if channel < 16 else 4095)):
                raise ValueError(f'Row {number}: invalid digital/analog output code')
            if 6 <= channel < 12 and abs(value) > 1:
                raise ValueError(f'Row {number}: duty must be within -1..1')
            values.append(value)
        if len(values) > MAX_VALUES:
            raise ValueError('Program exceeds 2,000,000 command values')
    if not values:
        raise ValueError('CSV has no data rows')
    return channels, values, warnings


def library_path(directory, name):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_. -]{0,94}\.csv', name):
        raise ValueError('Use a simple .csv filename without directory components')
    root = Path(directory).resolve()
    path = root / name
    if path.is_symlink():
        raise ValueError('Program symlinks are not supported')
    return path
