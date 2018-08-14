#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import logging
import optparse
import os
import shutil
import sys
import zipfile

from util import build_utils


def _CheckFilePathEndsWithJar(parser, file_path):
  if not file_path.endswith(".jar"):
    parser.error("%s does not end in .jar" % file_path)


def _CheckFilePathsEndWithJar(parser, file_paths):
  for file_path in file_paths:
    _CheckFilePathEndsWithJar(parser, file_path)


def _ParseArgs(args):
  args = build_utils.ExpandFileArgs(args)

  parser = optparse.OptionParser()
  build_utils.AddDepfileOption(parser)

  parser.add_option('--output-directory',
                    default=os.getcwd(),
                    help='Path to the output build directory.')
  parser.add_option('--dex-path', help='Dex output path.')
  parser.add_option('--configuration-name',
                    help='The build CONFIGURATION_NAME.')
  parser.add_option('--proguard-enabled',
                    help='"true" if proguard is enabled.')
  parser.add_option('--debug-build-proguard-enabled',
                    help='"true" if proguard is enabled for debug build.')
  parser.add_option('--proguard-enabled-input-path',
                    help=('Path to dex in Release mode when proguard '
                          'is enabled.'))
  parser.add_option('--inputs', help='A list of additional input paths.')
  parser.add_option('--excluded-paths',
                    help='A list of paths to exclude from the dex file.')
  parser.add_option('--main-dex-list-path',
                    help='A file containing a list of the classes to '
                         'include in the main dex.')
  parser.add_option('--multidex-configuration-path',
                    help='A JSON file containing multidex build configuration.')
  parser.add_option('--multi-dex', default=False, action='store_true',
                    help='Generate multiple dex files.')
  parser.add_option('--d8-jar-path',
                    help='Path to D8 jar.')

  options, paths = parser.parse_args(args)

  required_options = ('d8_jar_path',)
  build_utils.CheckOptions(options, parser, required=required_options)

  if options.multidex_configuration_path:
    with open(options.multidex_configuration_path) as multidex_config_file:
      multidex_config = json.loads(multidex_config_file.read())
    options.multi_dex = multidex_config.get('enabled', False)

  if options.multi_dex and not options.main_dex_list_path:
    logging.warning('multidex cannot be enabled without --main-dex-list-path')
    options.multi_dex = False
  elif options.main_dex_list_path and not options.multi_dex:
    logging.warning('--main-dex-list-path is unused if multidex is not enabled')

  if options.inputs:
    options.inputs = build_utils.ParseGnList(options.inputs)
    _CheckFilePathsEndWithJar(parser, options.inputs)
  if options.excluded_paths:
    options.excluded_paths = build_utils.ParseGnList(options.excluded_paths)

  if options.proguard_enabled_input_path:
    _CheckFilePathEndsWithJar(parser, options.proguard_enabled_input_path)
  _CheckFilePathsEndWithJar(parser, paths)

  return options, paths


def _MoveTempDexFile(tmp_dex_dir, dex_path):
  """Move the temp dex file out of |tmp_dex_dir|.

  Args:
    tmp_dex_dir: Path to temporary directory created with tempfile.mkdtemp().
      The directory should have just a single file.
    dex_path: Target path to move dex file to.

  Raises:
    Exception if there are multiple files in |tmp_dex_dir|.
  """
  tempfiles = os.listdir(tmp_dex_dir)
  if len(tempfiles) > 1:
    raise Exception('%d files created, expected 1' % len(tempfiles))

  tmp_dex_path = os.path.join(tmp_dex_dir, tempfiles[0])
  shutil.move(tmp_dex_path, dex_path)


def _NoClassFiles(jar_paths):
  """Returns True if there are no .class files in the given JARs.

  Args:
    jar_paths: list of strings representing JAR file paths.

  Returns:
    (bool) True if no .class files are found.
  """
  for jar_path in jar_paths:
    with zipfile.ZipFile(jar_path) as jar:
      if any(name.endswith('.class') for name in jar.namelist()):
        return False
  return True


def _RunD8(dex_cmd, input_paths, output_path):
  dex_cmd += ['--output', output_path]
  dex_cmd += input_paths
  build_utils.CheckOutput(dex_cmd, print_stderr=False)


def main(args):
  options, paths = _ParseArgs(args)
  if ((options.proguard_enabled == 'true'
          and options.configuration_name == 'Release')
      or (options.debug_build_proguard_enabled == 'true'
          and options.configuration_name == 'Debug')):
    paths = [options.proguard_enabled_input_path]

  if options.inputs:
    paths += options.inputs

  if options.excluded_paths:
    # Excluded paths are relative to the output directory.
    exclude_paths = options.excluded_paths
    paths = [p for p in paths if not
             os.path.relpath(p, options.output_directory) in exclude_paths]

  input_paths = list(paths)
  if options.multi_dex:
    input_paths.append(options.main_dex_list_path)

  dex_cmd = ['java', '-jar', options.d8_jar_path]
  if options.multi_dex:
    dex_cmd += ['--main-dex-list', options.main_dex_list_path]

  is_dex = options.dex_path.endswith('.dex')
  is_jar = options.dex_path.endswith('.jar')

  if is_jar and _NoClassFiles(paths):
    # Handle case where no classfiles are specified in inputs
    # by creating an empty JAR
    with zipfile.ZipFile(options.dex_path, 'w') as outfile:
      outfile.comment = 'empty'
  elif is_dex:
    # .dex files can't specify a name for D8. Instead, we output them to a
    # temp directory then move them after the command has finished running
    # (see _MoveTempDexFile). For other files, tmp_dex_dir is None.
    with build_utils.TempDir() as tmp_dex_dir:
      _RunD8(dex_cmd, paths, tmp_dex_dir)
      _MoveTempDexFile(tmp_dex_dir, options.dex_path)
  else:
    _RunD8(dex_cmd, paths, options.dex_path)

  build_utils.WriteDepfile(
      options.depfile, options.dex_path, input_paths, add_pydeps=False)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
