#!/usr/bin/env python
#
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Packages a tar.gz archive of a binary along with its dependencies. This
contains the Chromium parts of what would normally be added to the bootfs
used to boot QEMU or a device."""

import argparse
import os
import sys

from runner_common import AddCommonCommandLineArguments, BuildArchive, \
    ReadRuntimeDeps, ImageCreationData


def main():
  parser = argparse.ArgumentParser()
  AddCommonCommandLineArguments(parser)
  args, child_args = parser.parse_known_args()

  data = ImageCreationData(output_directory=args.output_directory,
                           exe_name=args.exe_name,
                           runtime_deps=ReadRuntimeDeps(
                               args.runtime_deps_path, args.output_directory),
                           target_cpu=args.target_cpu)
  BuildArchive(data, '%s_archive_%s.tar.gz' %
                         (os.path.basename(args.exe_name), args.target_cpu))


if __name__ == '__main__':
  sys.exit(main())
