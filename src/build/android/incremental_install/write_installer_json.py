#!/usr/bin/env python3

# Copyright 2017 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Writes a .json file with the per-apk details for an incremental install."""

import argparse
import json
import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), os.pardir, 'gyp'))

from util import build_utils
import action_helpers  # build_utils adds //build to sys.path.


def _ParseArgs(args):
  args = build_utils.ExpandFileArgs(args)
  parser = argparse.ArgumentParser()
  parser.add_argument('--output-path',
                      help='Output path for .json file.',
                      required=True)
  parser.add_argument('--apk-path',
                      help='Path to .apk relative to output directory.',
                      required=True)
  parser.add_argument('--split',
                      action='append',
                      dest='split_globs',
                      default=[],
                      help='A glob matching the apk splits. '
                           'Can be specified multiple times.')
  parser.add_argument(
      '--native-libs',
      action='append',
      help='GN-list of paths to native libraries relative to '
      'output directory. Can be repeated.')
  parser.add_argument(
      '--dex-files', help='GN-list of dex paths relative to output directory.')
  parser.add_argument('--show-proguard-warning',
                      action='store_true',
                      default=False,
                      help='Print a warning about proguard being disabled')

  options = parser.parse_args(args)
  options.dex_files = action_helpers.parse_gn_list(options.dex_files)
  options.native_libs = action_helpers.parse_gn_list(options.native_libs)
  return options


def main(args):
  options = _ParseArgs(args)

  data = {
      'apk_path': options.apk_path,
      'native_libs': options.native_libs,
      'dex_files': options.dex_files,
      'show_proguard_warning': options.show_proguard_warning,
      'split_globs': options.split_globs,
  }

  with action_helpers.atomic_output(options.output_path, mode='w+') as f:
    json.dump(data, f, indent=2, sort_keys=True)


if __name__ == '__main__':
  main(sys.argv[1:])
