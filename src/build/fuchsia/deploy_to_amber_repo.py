#!/usr/bin/env python
#
# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Deploys Fuchsia packages to an Amber repository in a Fuchsia
build output directory."""

import argparse
import os
import sys

from common import PublishPackage


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--package', action='append', required=True,
                      help='Paths to packages to install.')
  parser.add_argument('--fuchsia-out-dir', nargs='+',
                      help='Path to a Fuchsia build output directory. '
                           'If more than one outdir is supplied, the last one '
                           'in the sequence will be used.')
  args = parser.parse_args()
  assert args.package

  if not args.fuchsia_out_dir or len(args.fuchsia_out_dir) == 0:
    sys.stderr.write('No Fuchsia build output directory was specified.\n' +
                     'To resolve this, Use the commandline argument ' +
                     '--fuchsia-out-dir\nor set the GN arg ' +
                     '"default_fuchsia_build_dir_for_installation".\n')
    return 1

  fuchsia_out_dir = args.fuchsia_out_dir.pop()
  tuf_root = os.path.join(fuchsia_out_dir, 'amber-files')
  print('Installing packages in Amber repo %s...' % tuf_root)
  for package in args.package:
    PublishPackage(package, os.path.expanduser(tuf_root))

  print('Installation success.')

  return 0


if __name__ == '__main__':
  sys.exit(main())
