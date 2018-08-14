# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


import os
import sys


_SCRIPT_DIR = os.path.dirname(__file__)
_OUTPUT_FILE = os.path.join(_SCRIPT_DIR, 'apkanalyzer.output')


def main():
  # Without a proguard mapping file, the last argument is the apk_path.
  apk_path = sys.argv[-1]
  assert os.path.exists(apk_path), 'Apk does not exist: {}'.format(apk_path)
  with open(_OUTPUT_FILE, 'r') as f:
    sys.stdout.write(f.read())


if __name__ == '__main__':
  main()
