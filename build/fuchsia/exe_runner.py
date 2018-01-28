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

from runner_common import AddRunnerCommandLineArguments, BuildBootfs, \
    ImageCreationData, ReadRuntimeDeps, RunFuchsia


def main():
  parser = argparse.ArgumentParser()
  AddRunnerCommandLineArguments(parser)
  parser.add_argument('--extra-file', action='append', default=[],
                      help='Extra file to add to bootfs, '
                           '<bootfs_path>=<local_path>')
  parser.add_argument('--no-autorun', action='store_true',
                      help='Disable generating an autorun file')
  args, child_args = parser.parse_known_args()

  runtime_deps = ReadRuntimeDeps(args.runtime_deps_path, args.output_directory)
  for extra_file in args.extra_file:
    parts = extra_file.split("=", 1)
    if len(parts) < 2:
      print 'Invalid --extra-file: ', extra_file
      print 'Expected format: --extra-file <bootfs_path>=<local_path>'
      return 2
    runtime_deps.append(tuple(parts))

  image_creation_data = ImageCreationData(
      output_directory=args.output_directory,
      exe_name=args.exe_name,
      runtime_deps=runtime_deps,
      target_cpu=args.target_cpu,
      dry_run=args.dry_run,
      child_args=child_args,
      use_device=args.device,
      bootdata=args.bootdata,
      wait_for_network=True,
      use_autorun=not args.no_autorun)
  bootfs = BuildBootfs(image_creation_data)
  if not bootfs:
    return 2

  return RunFuchsia(bootfs, args.device, args.kernel, args.dry_run, None)


if __name__ == '__main__':
  sys.exit(main())
