#!/usr/bin/env python3
# Copyright (C) 2026 The Android Open Source Project
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
"""Runs `protoc --java_out` over the transitive import closure of the roots.

protoc only emits output for files passed on the command line, so this
wrapper walks `import "...";` declarations to compute the closure and
hands the full list to protoc. Touches a stamp file on success so the
caller can declare a single GN action output.
"""

import argparse
import os
import re
import subprocess
import sys

_IMPORT_RE = re.compile(r'^\s*import\s+"([^"]+)"\s*;')


def transitive_imports(roots, proto_path):
  visited = set()
  stack = list(roots)
  while stack:
    rel = stack.pop()
    if rel in visited:
      continue
    visited.add(rel)
    abs_path = os.path.join(proto_path, rel)
    if not os.path.exists(abs_path):
      continue
    with open(abs_path) as f:
      for line in f:
        m = _IMPORT_RE.match(line)
        if m and m.group(1).startswith('protos/perfetto/'):
          stack.append(m.group(1))
  return sorted(visited)


def main():
  ap = argparse.ArgumentParser()
  ap.add_argument('--protoc', required=True)
  ap.add_argument('--proto-path', required=True)
  ap.add_argument('--java-out', required=True)
  ap.add_argument('--stamp', required=True)
  ap.add_argument('roots', nargs='+')
  args = ap.parse_args()

  closure = transitive_imports(args.roots, args.proto_path)
  os.makedirs(args.java_out, exist_ok=True)
  subprocess.check_call([
      args.protoc, '--proto_path=' + args.proto_path, '--java_out=' +
      args.java_out
  ] + closure)

  with open(args.stamp, 'w'):
    pass


if __name__ == '__main__':
  sys.exit(main())
