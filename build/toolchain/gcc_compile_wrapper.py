#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs a compilation command.

This script exists to avoid using complex shell commands in
gcc_toolchain.gni's tool("cxx") and tool("cc") in case the host running the
compiler does not have a POSIX-like shell (e.g. Windows).
"""

import argparse
import sys

import wrapper_utils


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--resource-whitelist',
                      help='Generate a resource whitelist for this target.',
                      metavar='PATH')
  parser.add_argument('command', nargs=argparse.REMAINDER,
                      help='Compilation command')
  args = parser.parse_args()

  returncode, stderr = wrapper_utils.CaptureCommandStderr(
      wrapper_utils.CommandToRun(args.command))

  used_resources = wrapper_utils.ExtractResourceIdsFromPragmaWarnings(stderr)
  sys.stderr.write(stderr)

  if args.resource_whitelist:
    with open(args.resource_whitelist, 'w') as f:
      if used_resources:
        f.write('\n'.join(str(resource) for resource in used_resources))
        f.write('\n')

  return returncode

if __name__ == "__main__":
  sys.exit(main())
