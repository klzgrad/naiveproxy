#! /usr/bin/env python
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import os
import re
import shutil
import sys
import tempfile
import zipfile

import devil_chromium
from devil.android.sdk import dexdump
from pylib.constants import host_paths

sys.path.append(os.path.join(host_paths.DIR_SOURCE_ROOT, 'build', 'util', 'lib',
                             'common'))
import perf_tests_results_helper # pylint: disable=import-error

# Example dexdump output:
# DEX file header:
# magic               : 'dex\n035\0'
# checksum            : b664fc68
# signature           : ae73...87f1
# file_size           : 4579656
# header_size         : 112
# link_size           : 0
# link_off            : 0 (0x000000)
# string_ids_size     : 46148
# string_ids_off      : 112 (0x000070)
# type_ids_size       : 5730
# type_ids_off        : 184704 (0x02d180)
# proto_ids_size      : 8289
# proto_ids_off       : 207624 (0x032b08)
# field_ids_size      : 17854
# field_ids_off       : 307092 (0x04af94)
# method_ids_size     : 33699
# method_ids_off      : 449924 (0x06dd84)
# class_defs_size     : 2616
# class_defs_off      : 719516 (0x0afa9c)
# data_size           : 3776428
# data_off            : 803228 (0x0c419c)

# For what these mean, refer to:
# https://source.android.com/devices/tech/dalvik/dex-format.html


CONTRIBUTORS_TO_DEX_CACHE = {'type_ids_size': 'types',
                             'string_ids_size': 'strings',
                             'method_ids_size': 'methods',
                             'field_ids_size': 'fields'}


def _ExtractSizesFromDexFile(dex_path):
  counts = {}
  for line in dexdump.DexDump(dex_path, file_summary=True):
    if not line.strip():
      # Each method, type, field, and string contributes 4 bytes (1 reference)
      # to our DexCache size.
      counts['dex_cache_size'] = (
          sum(counts[x] for x in CONTRIBUTORS_TO_DEX_CACHE)) * 4
      return counts
    m = re.match(r'([a-z_]+_size) *: (\d+)', line)
    if m:
      counts[m.group(1)] = int(m.group(2))
  raise Exception('Unexpected end of output.')


def ExtractSizesFromZip(path):
  tmpdir = tempfile.mkdtemp(suffix='_dex_extract')
  try:
    counts = collections.defaultdict(int)
    with zipfile.ZipFile(path, 'r') as z:
      for subpath in z.namelist():
        if not subpath.endswith('.dex'):
          continue
        extracted_path = z.extract(subpath, tmpdir)
        cur_counts = _ExtractSizesFromDexFile(extracted_path)
        for k in cur_counts:
          counts[k] += cur_counts[k]
    return dict(counts)
  finally:
    shutil.rmtree(tmpdir)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--apk-name', help='Name of the APK to which the dexfile corresponds.')
  parser.add_argument('dexfile')

  args = parser.parse_args()

  devil_chromium.Initialize()

  if not args.apk_name:
    dirname, basename = os.path.split(args.dexfile)
    while basename:
      if 'apk' in basename:
        args.apk_name = basename
        break
      dirname, basename = os.path.split(dirname)
    else:
      parser.error(
          'Unable to determine apk name from %s, '
          'and --apk-name was not provided.' % args.dexfile)

  if os.path.splitext(args.dexfile)[1] in ('.zip', '.apk', '.jar'):
    sizes = ExtractSizesFromZip(args.dexfile)
  else:
    sizes = _ExtractSizesFromDexFile(args.dexfile)

  def print_result(name, value_key, description=None):
    perf_tests_results_helper.PrintPerfResult(
        '%s_%s' % (args.apk_name, name), 'total', [sizes[value_key]],
        description or name)

  for dex_header_name, readable_name in CONTRIBUTORS_TO_DEX_CACHE.iteritems():
    print_result(readable_name, dex_header_name)
  print_result(
      'DexCache_size', 'dex_cache_size', 'bytes of permanent dirty memory')
  return 0

if __name__ == '__main__':
  sys.exit(main())

