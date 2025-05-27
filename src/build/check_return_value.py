#!/usr/bin/env python3
# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This program wraps an arbitrary command and prints "1" if the command ran
successfully."""


import os
import subprocess
import sys

devnull = open(os.devnull, 'wb')
if not subprocess.call(sys.argv[1:], stdout=devnull, stderr=devnull):
  print(1)
else:
  print(0)
