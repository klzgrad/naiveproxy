#!/usr/bin/env python
#
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import sys

from util import build_utils


_SRC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                         '..', '..', '..'))
_DESUGAR_JAR_PATH = os.path.normpath(os.path.join(
    _SRC_ROOT, 'third_party', 'bazel', 'desugar', 'Desugar.jar'))


def _OnStaleMd5(input_jar, output_jar, classpath, bootclasspath_entry):
  cmd = [
      'java',
      '-jar',
      _DESUGAR_JAR_PATH,
      '--input',
      input_jar,
      '--bootclasspath_entry',
      bootclasspath_entry,
      '--output',
      output_jar,
      # Don't include try-with-resources files in every .jar. Instead, they
      # are included via //third_party/bazel/desugar:desugar_runtime_java.
      '--desugar_try_with_resources_omit_runtime_classes',
  ]
  for path in classpath:
    cmd += ['--classpath_entry', path]
  build_utils.CheckOutput(cmd, print_stdout=False)


def main():
  args = build_utils.ExpandFileArgs(sys.argv[1:])
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--input-jar', required=True,
                      help='Jar input path to include .class files from.')
  parser.add_argument('--output-jar', required=True,
                      help='Jar output path.')
  parser.add_argument('--classpath', required=True,
                      help='Classpath.')
  parser.add_argument('--bootclasspath-entry', required=True,
                      help='Path to javac bootclasspath interface jar.')
  options = parser.parse_args(args)

  options.classpath = build_utils.ParseGnList(options.classpath)
  input_paths = options.classpath + [
      options.bootclasspath_entry,
      options.input_jar,
  ]
  output_paths = [options.output_jar]
  depfile_deps = options.classpath + [_DESUGAR_JAR_PATH]

  build_utils.CallAndWriteDepfileIfStale(
      lambda: _OnStaleMd5(options.input_jar, options.output_jar,
                          options.classpath, options.bootclasspath_entry),
      options,
      input_paths=input_paths,
      input_strings=[],
      output_paths=output_paths,
      depfile_deps=depfile_deps)


if __name__ == '__main__':
  sys.exit(main())
