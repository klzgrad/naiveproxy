#!/usr/bin/env vpython
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate linker version scripts for Chrome on Android shared libraries."""

import argparse
import os

from util import build_utils

_SCRIPT_HEADER = """\
# AUTO-GENERATED FILE.  DO NOT MODIFY.
#
# See: %s

{
  global:
""" % os.path.relpath(__file__, build_utils.DIR_SOURCE_ROOT)

_SCRIPT_FOOTER = """\
  local:
    *;
};
"""


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--output',
      required=True,
      help='Path to output linker version script file.')
  parser.add_argument(
      '--export-java-symbols',
      action='store_true',
      help='Export Java_* JNI methods')
  parser.add_argument(
      '--export-symbol-whitelist-file',
      help='Path to input file containing whitelist of extra '
      'symbols to export. One symbol per line.')
  options = parser.parse_args()

  # JNI_OnLoad is always exported.
  symbol_list = ['JNI_OnLoad']

  if options.export_java_symbols:
    symbol_list.append('Java_*')

  if options.export_symbol_whitelist_file:
    with open(options.export_symbol_whitelist_file, 'rt') as f:
      for line in f:
        line = line.strip()
        if not line or line[0] == '#':
          continue
        symbol_list.append(line)

  script_content = [_SCRIPT_HEADER]
  for symbol in symbol_list:
    script_content.append('    %s;\n' % symbol)
  script_content.append(_SCRIPT_FOOTER)

  script = ''.join(script_content)

  with build_utils.AtomicOutput(options.output) as f:
    f.write(script)


if __name__ == '__main__':
  main()
