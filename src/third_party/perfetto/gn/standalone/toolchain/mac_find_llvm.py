# Copyright (C) 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import print_function

import glob
import os
import subprocess
import sys


def main():
  job = subprocess.Popen(['xcrun', '-f', 'clang++'],
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
  out, err = job.communicate()
  if job.returncode != 0:
    print(out, file=sys.stderr)
    print(err, file=sys.stderr)
    return job.returncode
  sdk_dir = os.path.dirname(os.path.dirname(out.rstrip()))
  print(sdk_dir)
  clang_dir = glob.glob(
      os.path.join(sdk_dir.decode(), 'lib', 'clang', '*', 'lib', 'darwin'))
  print(clang_dir[0] if clang_dir else 'CLANG_DIR_NOT_FOUND')


if __name__ == '__main__':
  sys.exit(main())
