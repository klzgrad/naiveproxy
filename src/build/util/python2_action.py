# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Script for ensuring that a python action runs under Python2, not Python3."""

import subprocess
import sys

if sys.version_info.major == 2:
  # If we get here, we're already Python2, so just re-execute the
  # command without the wrapper.
  exe = sys.executable
elif sys.executable.endswith('.exe'):
  # If we get here, we're a Python3 executable likely running on
  # Windows, so look for the Python2 wrapper in depot_tools.
  exe = 'python.bat'
else:
  # If we get here, we are a Python3 executable. Hope that we can find
  # a `python2.7` in path somewhere.
  exe = 'python2.7'

sys.exit(subprocess.call([exe] + sys.argv[1:]))
