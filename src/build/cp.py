#!/usr/bin/env python3
# Copyright 2012 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Copy a file.

This module works much like the cp posix command - it takes 2 arguments:
(src, dst) and copies the file with path |src| to |dst|.
"""

import os
import shutil
import sys


def Main(src, dst):
  # Use copy instead of copyfile to ensure the executable bit is copied.
  return shutil.copy(src, os.path.normpath(dst))


if __name__ == '__main__':
  sys.exit(Main(sys.argv[1], sys.argv[2]))
