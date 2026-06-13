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
r"""
Finds and prints MSVC and Windows SDK paths.

It outpus:
Line 1: the base path of the Windows SDK.
Line 2: the most recent version of the Windows SDK.
Line 3: the path of the most recent MSVC.

Example:
C:\Program Files (x86)\Windows Kits\10
10.0.19041.0
C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.28.29333
"""

import os
import itertools
import subprocess
import sys


def ver_to_tuple(ver_str):
  """Turns '10.1.2' into [10,1,2] so it can be compared using > """
  parts = [int(x) for x in ver_str.split('.')]
  return parts


def find_max_subdir(base_dir, filter=lambda x: True):
  """Finds the max subdirectory in base_dir by comparing semantic versions."""
  max_ver = None
  for ver in os.listdir(base_dir) if os.path.exists(base_dir) else []:
    cur = os.path.join(base_dir, ver)
    if not filter(cur):
      continue
    if max_ver is None or ver_to_tuple(ver) > ver_to_tuple(max_ver):
      max_ver = ver
  return max_ver


def main():
  out = [
      '',
      '',
      '',
  ]
  winsdk_base = 'C:\\Program Files (x86)\\Windows Kits\\10'
  if os.path.exists(winsdk_base):
    out[0] = winsdk_base
    lib_base = winsdk_base + '\\Lib'
    filt = lambda x: os.path.exists(os.path.join(x, 'ucrt', 'x64', 'ucrt.lib'))
    out[1] = find_max_subdir(lib_base, filt)

  for try_dir in itertools.product(
      ['2022', '2021', '2020', '2019'],
      ['BuildTools', 'Community', 'Professional', 'Enterprise', 'Preview'],
      ['Program Files', 'Program Files (x86)']):
    msvc_base = (f'C:\\{try_dir[2]}\\Microsoft Visual Studio\\'
                 f'{try_dir[0]}\\{try_dir[1]}\\VC\\Tools\\MSVC')
    if os.path.exists(msvc_base):
      filt = lambda x: os.path.exists(
          os.path.join(x, 'lib', 'x64', 'libcmt.lib'))
      max_msvc = find_max_subdir(msvc_base, filt)
      if max_msvc is not None:
        out[2] = os.path.join(msvc_base, max_msvc)
      break

  # Don't error in case of failure, GN scripts are supposed to deal with
  # failures and allow the user to override the dirs.

  print('\n'.join(out))
  return 0


if __name__ == '__main__':
  sys.exit(main())
