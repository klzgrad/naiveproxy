#!/usr/bin/env python3
#
# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Finds files in directories.
"""


import fnmatch
import optparse
import os
import sys


def main(argv):
  parser = optparse.OptionParser()
  parser.add_option('--pattern', default='*', help='File pattern to match.')
  options, directories = parser.parse_args(argv)

  for d in directories:
    if not os.path.exists(d):
      print('%s does not exist' % d, file=sys.stderr)
      return 1
    for root, _, filenames in os.walk(d):
      for f in fnmatch.filter(filenames, options.pattern):
        print(os.path.join(root, f))
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
