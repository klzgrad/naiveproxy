#!/usr/bin/env python3
# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Pack wattson curve CSVs into a compressed C++ blob.

Reads one or more <device>/<curve_type>.csv files (header row plus rows of
values), validates that each header matches the schema for the given curve
type, then serializes the union of all rows into a column-oriented binary
layout and emits a C++ header containing the zlib-compressed bytes.

Layout (little-endian, before compression):
  uint32 row_count
  uint32 string_count
  for each string in the string table:
    uint16 length, char[length]
  for each column, in schema order:
    if string column: uint32[row_count]  (indices into the string table)
    if int column:    int64[row_count]
    if double column: float64[row_count]
"""

import argparse
import csv
import os
import struct
import sys

ROOT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), *(['..'] * 4)))
sys.path.append(ROOT_DIR)

# pylint: disable=wrong-import-position
from python.tools.cpp_blob_emitter import (derive_include_guard, derive_symbol,
                                           emit_compressed_array)
# pylint: enable=wrong-import-position

# Column types: 'string' | 'int' | 'double'.
SCHEMAS = {
    'cpu_1d': [
        ('device', 'string'),
        ('policy', 'int'),
        ('freq_khz', 'int'),
        ('static', 'double'),
        ('active', 'double'),
        ('idle0', 'double'),
        ('idle1', 'double'),
    ],
    'cpu_2d': [
        ('device', 'string'),
        ('policy', 'int'),
        ('freq_khz', 'int'),
        ('dep_policy', 'int'),
        ('dep_freq', 'int'),
        ('static', 'double'),
        ('active', 'double'),
        ('idle0', 'double'),
        ('idle1', 'double'),
    ],
    'gpu': [
        ('device', 'string'),
        ('freq_khz', 'int'),
        ('active', 'double'),
        ('idle1', 'double'),
        ('idle2', 'double'),
    ],
    'l3': [
        ('device', 'string'),
        ('freq_khz', 'int'),
        ('dep_policy', 'int'),
        ('dep_freq', 'int'),
        ('l3_hit', 'double'),
        ('l3_miss', 'double'),
    ],
    'tpu': [
        ('device', 'string'),
        ('cluster', 'int'),
        ('requests', 'int'),
        ('freq', 'int'),
        ('active', 'double'),
    ],
}


def _read_csv(path, expected_cols):
  with open(path, newline='') as f:
    reader = csv.reader(f)
    header = next(reader)
    if header != expected_cols:
      raise ValueError('{}: header {} does not match schema {}'.format(
          path, header, expected_cols))
    rows = list(reader)
  if not rows:
    raise ValueError('{}: empty CSV'.format(path))
  return rows


def _coerce(value, kind):
  if kind == 'string':
    return value
  if kind == 'int':
    return int(value)
  if kind == 'double':
    return float(value)
  raise ValueError('unknown kind ' + kind)


def _serialize(schema, rows):
  buf = bytearray()
  # String table: collect unique strings (ordered by first appearance).
  strings = []
  string_index = {}
  for row in rows:
    for (_, kind), value in zip(schema, row):
      if kind == 'string' and value not in string_index:
        string_index[value] = len(strings)
        strings.append(value)

  buf += struct.pack('<I', len(rows))
  buf += struct.pack('<I', len(strings))
  for s in strings:
    encoded = s.encode('utf-8')
    if len(encoded) > 0xFFFF:
      raise ValueError('string too long: {}'.format(s))
    buf += struct.pack('<H', len(encoded))
    buf += encoded

  # Column-oriented payload (matches the column storage of the typed
  # `tables::*` and compresses better than row-oriented because consecutive
  # values in a column tend to repeat).
  for col_idx, (_, kind) in enumerate(schema):
    if kind == 'string':
      for row in rows:
        buf += struct.pack('<I', string_index[row[col_idx]])
    elif kind == 'int':
      for row in rows:
        buf += struct.pack('<q', row[col_idx])
    else:  # double
      for row in rows:
        buf += struct.pack('<d', row[col_idx])
  return bytes(buf)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--output', required=True)
  parser.add_argument('--gen-dir', default='')
  parser.add_argument('--namespace', required=True)
  parser.add_argument('--curve-type', required=True, choices=sorted(SCHEMAS))
  parser.add_argument('inputs', nargs='+', help='per-device CSV files')
  args = parser.parse_args()

  schema = SCHEMAS[args.curve_type]
  expected_cols = [name for name, _ in schema]

  # Aggregate rows from every CSV. Sort by input path for determinism — each
  # file already groups one device's rows in source order.
  rows = []
  for path in sorted(args.inputs):
    raw = _read_csv(path, expected_cols)
    for raw_row in raw:
      if len(raw_row) != len(schema):
        raise ValueError('{}: row {} has {} fields, expected {}'.format(
            path, raw_row, len(raw_row), len(schema)))
      rows.append([_coerce(v, kind) for v, (_, kind) in zip(raw_row, schema)])

  payload = _serialize(schema, rows)

  symbol = derive_symbol(args.output, suffix='Data')
  include_guard = derive_include_guard(args.output, args.gen_dir)
  emit_compressed_array(
      payload,
      args.output,
      symbol=symbol,
      namespace=args.namespace,
      include_guard=include_guard)
  return 0


if __name__ == '__main__':
  sys.exit(main())
