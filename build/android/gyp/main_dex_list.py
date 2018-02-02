#!/usr/bin/env python
#
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import sys
import tempfile

from util import build_utils

sys.path.append(os.path.abspath(os.path.join(
    os.path.dirname(__file__), os.pardir)))
from pylib import constants


def main(args):
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--android-sdk-tools', required=True,
                      help='Android sdk build tools directory.')
  parser.add_argument('--main-dex-rules-path', action='append', default=[],
                      dest='main_dex_rules_paths',
                      help='A file containing a list of proguard rules to use '
                           'in determining the class to include in the '
                           'main dex.')
  parser.add_argument('--main-dex-list-path', required=True,
                      help='The main dex list file to generate.')
  parser.add_argument('--enabled-configurations',
                      help='The build configurations for which a main dex list'
                           ' should be generated.')
  parser.add_argument('--configuration-name',
                      help='The current build configuration.')
  parser.add_argument('--multidex-configuration-path',
                      help='A JSON file containing multidex build '
                           'configuration.')
  parser.add_argument('--inputs',
                      help='JARs for which a main dex list should be '
                           'generated.')
  parser.add_argument('--proguard-path', required=True,
                      help='Path to the proguard executable.')

  parser.add_argument('paths', nargs='*', default=[],
                      help='JARs for which a main dex list should be '
                           'generated.')

  args = parser.parse_args(build_utils.ExpandFileArgs(args))

  if args.multidex_configuration_path:
    with open(args.multidex_configuration_path) as multidex_config_file:
      multidex_config = json.loads(multidex_config_file.read())

    if not multidex_config.get('enabled', False):
      return 0

  if args.inputs:
    args.paths.extend(build_utils.ParseGnList(args.inputs))

  shrinked_android_jar = os.path.abspath(
      os.path.join(args.android_sdk_tools, 'lib', 'shrinkedAndroid.jar'))
  dx_jar = os.path.abspath(
      os.path.join(args.android_sdk_tools, 'lib', 'dx.jar'))
  rules_file = os.path.abspath(
      os.path.join(args.android_sdk_tools, 'mainDexClasses.rules'))

  proguard_cmd = [
    'java', '-jar', args.proguard_path,
    '-forceprocessing',
    '-dontwarn', '-dontoptimize', '-dontobfuscate', '-dontpreverify',
    '-libraryjars', shrinked_android_jar,
    '-include', rules_file,
  ]
  for m in args.main_dex_rules_paths:
    proguard_cmd.extend(['-include', m])

  main_dex_list_cmd = [
    'java', '-cp', dx_jar,
    'com.android.multidex.MainDexListBuilder',
  ]

  input_paths = list(args.paths)
  input_paths += [
    shrinked_android_jar,
    dx_jar,
    rules_file,
  ]
  input_paths += args.main_dex_rules_paths

  input_strings = [
    proguard_cmd,
    main_dex_list_cmd,
  ]

  output_paths = [
    args.main_dex_list_path,
  ]

  build_utils.CallAndWriteDepfileIfStale(
      lambda: _OnStaleMd5(proguard_cmd, main_dex_list_cmd, args.paths,
                          args.main_dex_list_path),
      args,
      input_paths=input_paths,
      input_strings=input_strings,
      output_paths=output_paths)

  return 0


def _OnStaleMd5(proguard_cmd, main_dex_list_cmd, paths, main_dex_list_path):
  paths_arg = ':'.join(paths)
  main_dex_list = ''
  try:
    with tempfile.NamedTemporaryFile(suffix='.jar') as temp_jar:
      proguard_cmd += [
        '-injars', paths_arg,
        '-outjars', temp_jar.name
      ]
      build_utils.CheckOutput(proguard_cmd, print_stderr=False)

      main_dex_list_cmd += [
        temp_jar.name, paths_arg
      ]
      main_dex_list = build_utils.CheckOutput(main_dex_list_cmd)
  except build_utils.CalledProcessError as e:
    if 'output jar is empty' in e.output:
      pass
    elif "input doesn't contain any classes" in e.output:
      pass
    else:
      raise

  with open(main_dex_list_path, 'w') as main_dex_list_file:
    main_dex_list_file.write(main_dex_list)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))

