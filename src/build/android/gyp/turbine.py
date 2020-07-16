#!/usr/bin/env python
# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Wraps bin/helper/turbine and expands @FileArgs."""

import argparse
import logging
import os
import shutil
import subprocess
import sys
import time

from util import build_utils
from util import md5_check


def _OnStaleMd5(options, cmd, javac_cmd, files, classpath):
  if classpath:
    cmd += ['--classpath']
    cmd += classpath

  if options.java_srcjars:
    cmd += ['--source_jars']
    cmd += options.java_srcjars

  if files:
    # Use jar_path to ensure paths are relative (needed for goma).
    files_rsp_path = options.jar_path + '.files_list.txt'
    with open(files_rsp_path, 'w') as f:
      f.write(' '.join(files))
    # Pass source paths as response files to avoid extremely long command lines
    # that are tedius to debug.
    cmd += ['--sources']
    cmd += ['@' + files_rsp_path]

  if javac_cmd:
    cmd.append('--javacopts')
    cmd += javac_cmd
    cmd.append('--')  # Terminate javacopts

  # Use AtomicOutput so that output timestamps are not updated when outputs
  # are not changed.
  with build_utils.AtomicOutput(options.jar_path) as output_jar, \
      build_utils.AtomicOutput(options.generated_jar_path) as generated_jar:
    cmd += ['--output', output_jar.name, '--gensrc_output', generated_jar.name]
    logging.debug('Command: %s', cmd)
    start = time.time()
    subprocess.check_call(cmd)
    end = time.time() - start
    logging.info('Header compilation took %ss', end)

  logging.info('Completed all steps in _OnStaleMd5')


def main(argv):
  build_utils.InitLogging('TURBINE_DEBUG')
  argv = build_utils.ExpandFileArgs(argv[1:])
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  parser.add_argument(
      '--turbine-jar-path', required=True, help='Path to the turbine jar file.')
  parser.add_argument(
      '--java-srcjars',
      action='append',
      default=[],
      help='List of srcjars to include in compilation.')
  parser.add_argument(
      '--bootclasspath',
      action='append',
      default=[],
      help='Boot classpath for javac. If this is specified multiple times, '
      'they will all be appended to construct the classpath.')
  parser.add_argument(
      '--java-version',
      help='Java language version to use in -source and -target args to javac.')
  parser.add_argument('--classpath', action='append', help='Classpath to use.')
  parser.add_argument(
      '--processors',
      action='append',
      help='GN list of annotation processor main classes.')
  parser.add_argument(
      '--processorpath',
      action='append',
      help='GN list of jars that comprise the classpath used for Annotation '
      'Processors.')
  parser.add_argument(
      '--processor-args',
      action='append',
      help='key=value arguments for the annotation processors.')
  parser.add_argument('--jar-path', help='Jar output path.', required=True)
  parser.add_argument(
      '--generated-jar-path',
      required=True,
      help='Output path for generated source files.')
  options, unknown_args = parser.parse_known_args(argv)

  options.bootclasspath = build_utils.ParseGnList(options.bootclasspath)
  options.classpath = build_utils.ParseGnList(options.classpath)
  options.processorpath = build_utils.ParseGnList(options.processorpath)
  options.processors = build_utils.ParseGnList(options.processors)
  options.java_srcjars = build_utils.ParseGnList(options.java_srcjars)

  files = []
  for arg in unknown_args:
    # Interpret a path prefixed with @ as a file containing a list of sources.
    if arg.startswith('@'):
      files.extend(build_utils.ReadSourcesList(arg[1:]))

  cmd = [
      build_utils.JAVA_PATH, '-classpath', options.turbine_jar_path,
      'com.google.turbine.main.Main'
  ]
  javac_cmd = []

  # Turbine reads lists from command line args by consuming args until one
  # starts with double dash (--). Thus command line args should be grouped
  # together and passed in together.
  if options.processors:
    cmd += ['--processors']
    cmd += options.processors

  if options.java_version:
    javac_cmd.extend([
        '-source',
        options.java_version,
        '-target',
        options.java_version,
    ])
  if options.java_version == '1.8':
    # Android's boot jar doesn't contain all java 8 classes.
    options.bootclasspath.append(build_utils.RT_JAR_PATH)

  if options.bootclasspath:
    cmd += ['--bootclasspath']
    for bootclasspath in options.bootclasspath:
      cmd += bootclasspath.split(':')

  if options.processorpath:
    cmd += ['--processorpath']
    cmd += options.processorpath

  if options.processor_args:
    for arg in options.processor_args:
      javac_cmd.extend(['-A%s' % arg])

  classpath_inputs = (
      options.bootclasspath + options.classpath + options.processorpath)

  # GN already knows of the java files, so avoid listing individual java files
  # in the depfile.
  depfile_deps = classpath_inputs + options.java_srcjars
  input_paths = depfile_deps + files

  output_paths = [
      options.jar_path,
      options.generated_jar_path,
  ]

  input_strings = cmd + options.classpath + files

  md5_check.CallAndWriteDepfileIfStale(
      lambda: _OnStaleMd5(options, cmd, javac_cmd, files, options.classpath),
      options,
      depfile_deps=depfile_deps,
      input_paths=input_paths,
      input_strings=input_strings,
      output_paths=output_paths)


if __name__ == '__main__':
  sys.exit(main(sys.argv))
