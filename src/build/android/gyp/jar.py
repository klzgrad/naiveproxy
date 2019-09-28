#!/usr/bin/env python
#
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import optparse
import os
import sys

from util import build_utils


def Jar(class_files,
        classes_dir,
        jar_path,
        provider_configurations=None,
        additional_files=None):
  files = [(os.path.relpath(f, classes_dir), f) for f in class_files]

  if additional_files:
    for filepath, jar_filepath in additional_files:
      files.append((jar_filepath, filepath))

  if provider_configurations:
    for config in provider_configurations:
      files.append(('META-INF/services/' + os.path.basename(config), config))

  # Zeros out timestamps so that builds are hermetic.
  with build_utils.AtomicOutput(jar_path) as f:
    build_utils.DoZip(files, f)


def JarDirectory(classes_dir,
                 jar_path,
                 predicate=None,
                 provider_configurations=None,
                 additional_files=None):
  all_files = build_utils.FindInDirectory(classes_dir, '*')
  if predicate:
    all_files = [
        f for f in all_files if predicate(os.path.relpath(f, classes_dir))]
  all_files.sort()

  Jar(all_files,
      classes_dir,
      jar_path,
      provider_configurations=provider_configurations,
      additional_files=additional_files)


def _CreateFilterPredicate(excluded_classes, included_classes):
  if not excluded_classes and not included_classes:
    return None

  def predicate(f):
    # Exclude filters take precidence over include filters.
    if build_utils.MatchesGlob(f, excluded_classes):
      return False
    if included_classes and not build_utils.MatchesGlob(f, included_classes):
      return False
    return True

  return predicate


# TODO(agrieve): Change components/cronet/android/BUILD.gn to use filter_zip.py
#     and delete main().
def main():
  parser = optparse.OptionParser()
  parser.add_option('--classes-dir', help='Directory containing .class files.')
  parser.add_option('--jar-path', help='Jar output path.')
  parser.add_option('--excluded-classes',
      help='GN list of .class file patterns to exclude from the jar.')
  parser.add_option('--included-classes',
      help='GN list of .class file patterns to include in the jar.')

  args = build_utils.ExpandFileArgs(sys.argv[1:])
  options, _ = parser.parse_args(args)

  excluded_classes = []
  if options.excluded_classes:
    excluded_classes = build_utils.ParseGnList(options.excluded_classes)
  included_classes = []
  if options.included_classes:
    included_classes = build_utils.ParseGnList(options.included_classes)

  predicate = _CreateFilterPredicate(excluded_classes, included_classes)
  JarDirectory(options.classes_dir, options.jar_path, predicate=predicate)


if __name__ == '__main__':
  sys.exit(main())
