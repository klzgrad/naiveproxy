#!/usr/bin/python
# Copyright (c) 2015 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script is a faster, specialized alternative to package_version.py for
getting the toolchain revision. Speed matters when running gn gen, for example.
"""

import json
import os.path
import sys

BUILD_DIR = os.path.dirname(os.path.abspath(__file__))
NACL_DIR = os.path.dirname(BUILD_DIR)
DEFAULT_REVISIONS_DIR = os.path.join(NACL_DIR, 'toolchain_revisions')


def main():
  if len(sys.argv) < 2:
    print 'usage: %s toolchain_package_names...' % sys.argv[0]
    sys.exit(1)
  for package in sys.argv[1:]:
    revisions_file = os.path.join(DEFAULT_REVISIONS_DIR, package + '.json')
    if not os.path.exists(revisions_file):
      print 'unknown toolchain %s' % package
      sys.exit(1)
    f = open(revisions_file)
    data = json.load(f)
    f.close()
    print data['revision']


if __name__ == '__main__':
  main()
