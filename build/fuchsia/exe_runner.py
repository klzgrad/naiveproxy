#!/usr/bin/env python
#
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Packages a user.bootfs for a Fuchsia boot image, pulling in the runtime
dependencies of a binary, and then uses either QEMU from the Fuchsia SDK
to run, or starts the bootserver to allow running on a hardware device."""

import argparse
import os
import sys

from runner_common import RunFuchsia, BuildBootfs, ReadRuntimeDeps


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--dry-run', '-n', action='store_true', default=False,
                      help='Just print commands, don\'t execute them.')
  parser.add_argument('--output-directory',
                      type=os.path.realpath,
                      help=('Path to the directory in which build files are'
                            ' located (must include build type).'))
  parser.add_argument('--runtime-deps-path',
                      type=os.path.realpath,
                      help='Runtime data dependency file from GN.')
  parser.add_argument('--target-cpu',
                      help='GN target_cpu setting for the build.')
  parser.add_argument('--exe-name',
                      type=os.path.realpath,
                      help='Name of the the binary executable.')
  parser.add_argument('-d', '--device', action='store_true', default=False,
                      help='Run on hardware device instead of QEMU.')
  args, child_args = parser.parse_known_args()

  bootfs = BuildBootfs(
      args.output_directory,
      ReadRuntimeDeps(args.runtime_deps_path, args.output_directory),
      args.exe_name, child_args, args.dry_run, summary_output=None,
      power_off=False, target_cpu=args.target_cpu)
  if not bootfs:
    return 2

  return RunFuchsia(bootfs, args.device, args.dry_run, None)


if __name__ == '__main__':
  sys.exit(main())
