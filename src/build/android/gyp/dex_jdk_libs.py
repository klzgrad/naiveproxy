#!/usr/bin/env python
#
# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import subprocess
import sys
import zipfile

from util import build_utils


def _ParseArgs(args):
  args = build_utils.ExpandFileArgs(args)
  parser = argparse.ArgumentParser()

  parser.add_argument('--output', required=True, help='Dex output path.')
  parser.add_argument('--r8-path', required=True, help='Path to R8 jar.')
  parser.add_argument(
      '--desugar-jdk-libs-json', help='Path to desugar_jdk_libs.json.')
  parser.add_argument(
      '--desugar-jdk-libs-jar', help='Path to desugar_jdk_libs.jar.')
  parser.add_argument('--min-api', help='minSdkVersion', required=True)
  options = parser.parse_args(args)
  return options


def main(args):
  options = _ParseArgs(args)

  # TODO(agrieve): Spews a lot of stderr about missing classes.
  with build_utils.TempDir() as tmp_dir:
    cmd = [
        build_utils.JAVA_PATH,
        '-jar',
        options.r8_path,
        'l8',
        '--min-api',
        options.min_api,
        #'--lib', build_utils.JAVA_HOME,
        '--desugared-lib',
        options.desugar_jdk_libs_json,
        '--output',
        tmp_dir,
        options.desugar_jdk_libs_jar
    ]
    subprocess.check_output(cmd, stderr=subprocess.STDOUT)

    if os.path.exists(os.path.join(tmp_dir, 'classes2.dex')):
      raise Exception('Achievement unlocked: desugar_jdk_libs is multidex!')
    shutil.move(os.path.join(tmp_dir, 'classes.dex'), options.output)


if __name__ == '__main__':
  main(sys.argv[1:])
