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
import zipfile

from util import build_utils
from util import proguard_util


def main(args):
  parser = argparse.ArgumentParser()
  build_utils.AddDepfileOption(parser)
  parser.add_argument('--shrinked-android-path', required=True,
                      help='Path to shrinkedAndroid.jar')
  parser.add_argument('--dx-path', required=True,
                      help='Path to dx.jar')
  parser.add_argument('--main-dex-rules-path', action='append', default=[],
                      dest='main_dex_rules_paths',
                      help='A file containing a list of proguard rules to use '
                           'in determining the class to include in the '
                           'main dex.')
  parser.add_argument('--main-dex-list-path', required=True,
                      help='The main dex list file to generate.')
  parser.add_argument('--inputs',
                      help='JARs for which a main dex list should be '
                           'generated.')
  parser.add_argument(
      '--r8-path', required=True, help='Path to the r8 executable.')
  parser.add_argument('--negative-main-dex-globs',
      help='GN-list of globs of .class names (e.g. org/chromium/foo/Bar.class) '
           'that will fail the build if they match files in the main dex.')

  parser.add_argument('paths', nargs='*', default=[],
                      help='JARs for which a main dex list should be '
                           'generated.')

  args = parser.parse_args(build_utils.ExpandFileArgs(args))

  depfile_deps = []
  if args.inputs:
    args.inputs = build_utils.ParseGnList(args.inputs)
    depfile_deps = args.inputs
    args.paths.extend(args.inputs)

  if args.negative_main_dex_globs:
    args.negative_main_dex_globs = build_utils.ParseGnList(
        args.negative_main_dex_globs)

  proguard_cmd = [
      'java',
      '-jar',
      args.r8_path,
      '--classfile',
      '--lib',
      args.shrinked_android_path,
  ]

  for m in args.main_dex_rules_paths:
    proguard_cmd.extend(['--pg-conf', m])

  proguard_flags = [
      '-forceprocessing',
      '-dontwarn',
      '-dontoptimize',
      '-dontobfuscate',
      '-dontpreverify',
  ]

  main_dex_list_cmd = [
    'java', '-cp', args.dx_path,
    'com.android.multidex.MainDexListBuilder',
    # This workaround significantly increases main dex size and doesn't seem to
    # be needed by Chrome. See comment in the source:
    # https://android.googlesource.com/platform/dalvik/+/master/dx/src/com/android/multidex/MainDexListBuilder.java
    '--disable-annotation-resolution-workaround',
  ]

  input_paths = list(args.paths)
  input_paths += [
    args.shrinked_android_path,
    args.dx_path,
  ]
  input_paths += args.main_dex_rules_paths

  input_strings = [
    proguard_cmd,
    main_dex_list_cmd,
  ]

  if args.negative_main_dex_globs:
    input_strings += args.negative_main_dex_globs
    for glob in args.negative_main_dex_globs:
      # Globs come with 1 asterix, but we want 2 to match subpackages.
      proguard_flags.append('-checkdiscard class ' +
                            glob.replace('*', '**').replace('/', '.'))

  output_paths = [
    args.main_dex_list_path,
  ]

  def _LineLengthHelperForOnStaleMd5():
    _OnStaleMd5(proguard_cmd, proguard_flags, main_dex_list_cmd, args.paths,
                args.main_dex_list_path)

  build_utils.CallAndWriteDepfileIfStale(
      _LineLengthHelperForOnStaleMd5,
      args,
      input_paths=input_paths,
      input_strings=input_strings,
      output_paths=output_paths,
      depfile_deps=depfile_deps,
      add_pydeps=False)

  return 0


def _OnStaleMd5(proguard_cmd, proguard_flags, main_dex_list_cmd, paths,
                main_dex_list_path):
  main_dex_list = ''
  try:
    with tempfile.NamedTemporaryFile(suffix='.jar') as temp_jar:
      # Step 1: Use ProGuard to find all @MainDex code, and all code reachable
      # from @MainDex code (recursive).
      proguard_cmd += ['--output', temp_jar.name]
      with tempfile.NamedTemporaryFile() as proguard_flags_file:
        for flag in proguard_flags:
          proguard_flags_file.write(flag + '\n')
        proguard_flags_file.flush()
        proguard_cmd += ['--pg-conf', proguard_flags_file.name]
        for injar in paths:
          proguard_cmd.append(injar)
        build_utils.CheckOutput(proguard_cmd, print_stderr=False)

      # Record the classes kept by ProGuard. Not used by the build, but useful
      # for debugging what classes are kept by ProGuard vs. MainDexListBuilder.
      with zipfile.ZipFile(temp_jar.name) as z:
        kept_classes = [p for p in z.namelist() if p.endswith('.class')]
      with open(main_dex_list_path + '.partial', 'w') as f:
        f.write('\n'.join(kept_classes) + '\n')

      # Step 2: Expand inclusion list to all classes referenced by the .class
      # files of kept classes (non-recursive).
      main_dex_list_cmd += [temp_jar.name, ':'.join(paths)]
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
