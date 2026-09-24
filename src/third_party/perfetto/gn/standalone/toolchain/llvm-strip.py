#!/usr/bin/env python3
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
"""This is really a wrapper around exec() for llvm-objcopy.

It is used to execute llvm-objcopy passing "llvm-strip" as argv0. This is
because llvm-objcopy frontend behaves differently and emulates the "strip"
binary when its argv0 is set as llvm-strip.
"""

import os
import sys


def main():
  if (len(sys.argv) < 3):
    sys.stderr.write('Usage: %s /path/to/llvm-objcopy [args]\n' % (__file__))
    return 1
  os.execlp(sys.argv[1], "llvm-strip", *sys.argv[2:])


if __name__ == '__main__':
  sys.exit(main())
