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

import glob
import os
import shutil
import subprocess
import sys


def candidate_clangs():
  # Try the unversioned binary first, then any versioned clang-N found on PATH.
  # (Ubuntu's apt clang package installs only clang-NN, without the unversioned
  # symlink, so we need to discover whatever version is actually present.)
  seen = set()
  ordered = ['clang']
  for path_dir in os.environ.get('PATH', '').split(os.pathsep):
    if not path_dir:
      continue
    for match in sorted(glob.glob(os.path.join(path_dir, 'clang-*'))):
      name = os.path.basename(match)
      # Skip things like clang-cpp, clang-format, clang-tidy: the suffix must
      # be a version number.
      suffix = name[len('clang-'):]
      if not suffix.replace('.', '').isdigit():
        continue
      if name not in seen:
        seen.add(name)
        ordered.append(name)
  return ordered


def main():
  for clang in candidate_clangs():
    if shutil.which(clang) is None:
      continue
    res = subprocess.check_output([clang, '-print-search-dirs']).decode("utf-8")
    for line in res.splitlines():
      if not line.startswith('libraries:'):
        continue
      libs = line.split('=', 1)[1].split(':')
      for lib in libs:
        if '/clang/' not in lib or not os.path.isdir(lib + '/lib'):
          continue
        print(os.path.abspath(lib))
        print(clang)
        print(clang.replace('clang', 'clang++'))
        return 0
  print('Could not find the LLVM lib dir')
  return 1


if __name__ == '__main__':
  sys.exit(main())
