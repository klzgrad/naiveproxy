#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import logging
import optparse
import os
import sys
import tempfile
import zipfile

from util import build_utils


def _CheckFilePathEndsWithJar(parser, file_path):
  if not file_path.endswith(".jar"):
    # dx ignores non .jar files.
    parser.error("%s does not end in .jar" % file_path)


def _CheckFilePathsEndWithJar(parser, file_paths):
  for file_path in file_paths:
    _CheckFilePathEndsWithJar(parser, file_path)


def _RemoveUnwantedFilesFromZip(dex_path):
  iz = zipfile.ZipFile(dex_path, 'r')
  tmp_dex_path = '%s.tmp.zip' % dex_path
  oz = zipfile.ZipFile(tmp_dex_path, 'w', zipfile.ZIP_DEFLATED)
  for i in iz.namelist():
    if i.endswith('.dex'):
      oz.writestr(i, iz.read(i))
  os.remove(dex_path)
  os.rename(tmp_dex_path, dex_path)


def _ParseArgs(args):
  args = build_utils.ExpandFileArgs(args)

  parser = optparse.OptionParser()
  build_utils.AddDepfileOption(parser)

  parser.add_option('--android-sdk-tools',
                    help='Android sdk build tools directory.')
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
  parser.add_option('--no-locals', default='0',
                    help='Exclude locals list from the dex file.')
  parser.add_option('--incremental',
                    action='store_true',
                    help='Enable incremental builds when possible.')
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

  options, paths = parser.parse_args(args)

  required_options = ('android_sdk_tools',)
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


def _AllSubpathsAreClassFiles(paths, changes):
  for path in paths:
    if any(not p.endswith('.class') for p in changes.IterChangedSubpaths(path)):
      return False
  return True


def _DexWasEmpty(paths, changes):
  for path in paths:
    if any(p.endswith('.class')
           for p in changes.old_metadata.IterSubpaths(path)):
      return False
  return True


def _IterAllClassFiles(changes):
  for path in changes.IterAllPaths():
    for subpath in changes.IterAllSubpaths(path):
      if subpath.endswith('.class'):
        yield path


def _MightHitDxBug(changes):
  # We've seen dx --incremental fail for small libraries. It's unlikely a
  # speed-up anyways in this case.
  num_classes = sum(1 for x in _IterAllClassFiles(changes))
  if num_classes < 10:
    return True

  # We've also been able to consistently produce a failure by adding an empty
  # line to the top of the first .java file of a library.
  # https://crbug.com/617935
  first_file = next(_IterAllClassFiles(changes))
  for path in changes.IterChangedPaths():
    for subpath in changes.IterChangedSubpaths(path):
      if first_file == subpath:
        return True
  return False


def _RunDx(changes, options, dex_cmd, paths):
  with build_utils.TempDir() as classes_temp_dir:
    # --multi-dex is incompatible with --incremental.
    if options.multi_dex:
      dex_cmd.append('--main-dex-list=%s' % options.main_dex_list_path)
    else:
      # --incremental tells dx to merge all newly dex'ed .class files with
      # what that already exist in the output dex file (existing classes are
      # replaced).
      # Use --incremental when .class files are added or modified, but not when
      # any are removed (since it won't know to remove them).
      if (options.incremental
          and not _MightHitDxBug(changes)
          and changes.AddedOrModifiedOnly()):
        changed_inputs = set(changes.IterChangedPaths())
        changed_paths = [p for p in paths if p in changed_inputs]
        if not changed_paths:
          return
        # When merging in other dex files, there's no easy way to know if
        # classes were removed from them.
        if (_AllSubpathsAreClassFiles(changed_paths, changes)
            and not _DexWasEmpty(changed_paths, changes)):
          dex_cmd.append('--incremental')
          for path in changed_paths:
            changed_subpaths = set(changes.IterChangedSubpaths(path))
            # Note: |changed_subpaths| may be empty if nothing changed.
            if changed_subpaths:
              build_utils.ExtractAll(path, path=classes_temp_dir,
                                     predicate=lambda p: p in changed_subpaths)
          paths = [classes_temp_dir]

    dex_cmd += paths
    build_utils.CheckOutput(dex_cmd, print_stderr=False)

  if options.dex_path.endswith('.zip'):
    _RemoveUnwantedFilesFromZip(options.dex_path)


def _OnStaleMd5(changes, options, dex_cmd, paths):
  _RunDx(changes, options, dex_cmd, paths)
  build_utils.WriteJson(
      [os.path.relpath(p, options.output_directory) for p in paths],
      options.dex_path + '.inputs')


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

  dx_binary = os.path.join(options.android_sdk_tools, 'dx')
  # See http://crbug.com/272064 for context on --force-jumbo.
  # See https://github.com/android/platform_dalvik/commit/dd140a22d for
  # --num-threads.
  # See http://crbug.com/658782 for why -JXmx2G was added.
  dex_cmd = [dx_binary, '-JXmx2G', '--num-threads=8', '--dex', '--force-jumbo',
             '--output', options.dex_path]
  if options.no_locals != '0':
    dex_cmd.append('--no-locals')

  if options.multi_dex:
    input_paths.append(options.main_dex_list_path)
    dex_cmd += [
      '--multi-dex',
      '--minimal-main-dex',
    ]

  output_paths = [
    options.dex_path,
    options.dex_path + '.inputs',
  ]

  # An escape hatch to be able to check if incremental dexing is causing
  # problems.
  force = int(os.environ.get('DISABLE_INCREMENTAL_DX', 0))

  build_utils.CallAndWriteDepfileIfStale(
      lambda changes: _OnStaleMd5(changes, options, dex_cmd, paths),
      options,
      input_paths=input_paths,
      input_strings=dex_cmd,
      output_paths=output_paths,
      force=force,
      pass_changes=True)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
