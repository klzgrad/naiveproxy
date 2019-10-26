#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Instruments classes and jar files.

This script corresponds to the 'jacoco_instr' action in the java build process.
Depending on whether jacoco_instrument is set, the 'jacoco_instr' action will
call the instrument command which accepts a jar and instruments it using
jacococli.jar.

"""

from __future__ import print_function

import argparse
import json
import os
import shutil
import sys
import tempfile

from util import build_utils


def _AddArguments(parser):
  """Adds arguments related to instrumentation to parser.

  Args:
    parser: ArgumentParser object.
  """
  parser.add_argument(
      '--input-path',
      required=True,
      help='Path to input file(s). Either the classes '
      'directory, or the path to a jar.')
  parser.add_argument(
      '--output-path',
      required=True,
      help='Path to output final file(s) to. Either the '
      'final classes directory, or the directory in '
      'which to place the instrumented/copied jar.')
  parser.add_argument(
      '--sources-json-file',
      required=True,
      help='File to create with the list of source directories '
      'and input path.')
  parser.add_argument(
      '--java-sources-file',
      required=True,
      help='File containing newline-separated .java paths')
  parser.add_argument(
      '--jacococli-jar', required=True, help='Path to jacococli.jar.')


def _GetSourceDirsFromSourceFiles(source_files):
  """Returns list of directories for the files in |source_files|.

  Args:
    source_files: List of source files.

  Returns:
    List of source directories.
  """
  return list(set(os.path.dirname(source_file) for source_file in source_files))


def _CreateSourcesJsonFile(source_dirs, input_path, sources_json_file,
                           src_root):
  """Adds all normalized source directories and input path to
  |sources_json_file|.

  Args:
    source_dirs: List of source directories.
    input_path: The input path to non-instrumented class files.
    sources_json_file: File into which to write the list of source directories
    and input path.
    src_root: Root which sources added to the file should be relative to.

  Returns:
    An exit code.
  """
  src_root = os.path.abspath(src_root)
  relative_sources = []
  for s in source_dirs:
    abs_source = os.path.abspath(s)
    if abs_source[:len(src_root)] != src_root:
      print('Error: found source directory not under repository root: %s %s' %
            (abs_source, src_root))
      return 1
    rel_source = os.path.relpath(abs_source, src_root)

    relative_sources.append(rel_source)

  data = {}
  data['source_dirs'] = relative_sources
  data['input_path'] = os.path.abspath(input_path)
  with open(sources_json_file, 'w') as f:
    json.dump(data, f)


def _RunInstrumentCommand(parser):
  """Instruments jar files using Jacoco.

  Args:
    parser: ArgumentParser object.

  Returns:
    An exit code.
  """
  args = parser.parse_args()

  temp_dir = tempfile.mkdtemp()
  try:
    cmd = [
        'java', '-jar', args.jacococli_jar, 'instrument', args.input_path,
        '--dest', temp_dir
    ]

    build_utils.CheckOutput(cmd)

    jars = os.listdir(temp_dir)
    if len(jars) != 1:
      print('Error: multiple output files in: %s' % (temp_dir))
      return 1

    # Delete output_path first to avoid modifying input_path in the case where
    # input_path is a hardlink to output_path. http://crbug.com/571642
    if os.path.exists(args.output_path):
      os.unlink(args.output_path)
    shutil.move(os.path.join(temp_dir, jars[0]), args.output_path)
  finally:
    shutil.rmtree(temp_dir)

  source_files = []
  if args.java_sources_file:
    source_files.extend(build_utils.ReadSourcesList(args.java_sources_file))
  source_dirs = _GetSourceDirsFromSourceFiles(source_files)

  # TODO(GYP): In GN, we are passed the list of sources, detecting source
  # directories, then walking them to re-establish the list of sources.
  # This can obviously be simplified!
  _CreateSourcesJsonFile(source_dirs, args.input_path, args.sources_json_file,
                         build_utils.DIR_SOURCE_ROOT)

  return 0


def main():
  parser = argparse.ArgumentParser()
  _AddArguments(parser)
  _RunInstrumentCommand(parser)


if __name__ == '__main__':
  sys.exit(main())
