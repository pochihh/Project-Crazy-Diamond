"""Exact CSV command names. No interpolation or row scheduling occurs in Python."""
import csv
import io
import math
import re
from pathlib import Path

COMMANDS = {**{f'axis{a}_reference_mm': a for a in range(6)},
            **{f'axis{a}_reference_deg': 18+a for a in range(6)},
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
    if len(set(channels)) != len(channels) or any(sum(c in channels for c in (a,a+6,a+18)) > 1 for a in range(6)):
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
            if 12 <= channel < 18 and (value != int(value) or not 0 <= value <= (1 if channel < 16 else 4095)):
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


def preview_bins(channels, values, max_bins=None):
    width = len(channels)
    rows = len(values) // width
    # Full resolution for ordinary profiles. Bound the one-time ROS/UI transfer for large files.
    if max_bins is None:
        max_bins = rows if len(values) <= 50_000 else 1024
    stride = (rows + max_bins - 1) // max_bins
    names = {number: name for name, number in COMMANDS.items()}
    metadata = dict(rows=rows, rows_per_bin=stride, channels=[names[c] for c in channels])
    if stride == 1:
        return dict(**metadata, minimum=values, maximum=values)
    low, high = [], []
    for start in range(0, rows, stride):
        end = min(rows, start + stride) * width
        for column in range(width):
            samples = values[start * width + column:end:width]
            low.append(min(samples))
            high.append(max(samples))
    return dict(**metadata, minimum=low, maximum=high)
