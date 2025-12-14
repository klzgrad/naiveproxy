#!/usr/bin/env python3
# Copyright (C) 2023 The Android Open Source Project
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
"""Wrapper of pkg-config command line to format output for gn.

Parses the pkg-config output and format it into json,
so that it can be used in GN files easily.

Usage:
  pkg-config_wrapper.py pkg-config pkg1 pkg2 ...

Specifically, this script does not expect any additional flags.
"""

import json
import shlex
import subprocess
import sys


def get_shell_output(cmd):
  """Run |cmd| and return output as a list."""
  result = subprocess.run(
      cmd, encoding="utf-8", stdout=subprocess.PIPE, check=False)
  if result.returncode:
    sys.exit(result.returncode)
  return shlex.split(result.stdout)


def main(argv):
  if len(argv) < 2:
    sys.exit(f"Usage: {sys.argv[0]} <pkg-config> <modules>")

  cflags = get_shell_output(argv + ["--cflags"])
  libs = []
  lib_dirs = []
  ldflags = []
  for ldflag in get_shell_output(argv + ["--libs"]):
    if ldflag.startswith("-l"):
      # Strip -l.
      libs.append(ldflag[2:])
    elif ldflag.startswith("-L"):
      # Strip -L.
      lib_dirs.append(ldflag[2:])
    else:
      ldflags.append(ldflag)

  # Set sort_keys=True for stabilization.
  result = {
      "cflags": cflags,
      "libs": libs,
      "lib_dirs": lib_dirs,
      "ldflags": ldflags,
  }
  json.dump(result, sys.stdout, sort_keys=True)


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
