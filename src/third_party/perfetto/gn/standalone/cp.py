#!/usr/bin/env python3
# Copyright (C) 2020 The Android Open Source Project
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

import os
import shutil
import sys


def main():
  src, dst = sys.argv[1:]

  if os.path.exists(dst):
    if os.path.isdir(dst):
      shutil.rmtree(dst)
    else:
      os.remove(dst)

  if os.path.isdir(src):
    shutil.copytree(src, dst)
  else:
    shutil.copy2(src, dst)
    #work around https://github.com/ninja-build/ninja/issues/1554
    os.utime(dst, None)


if __name__ == '__main__':
  sys.exit(main())
