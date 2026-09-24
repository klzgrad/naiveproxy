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

# Prints a short hex digest of the given file. Used from GN to fold a file's
# contents into a compile flag, so that ccache (which doesn't follow files
# referenced by flags like -fsanitize-ignorelist=) invalidates when the file
# changes.

import hashlib
import sys


def main():
  with open(sys.argv[1], 'rb') as f:
    print(hashlib.sha256(f.read()).hexdigest()[:16])


if __name__ == '__main__':
  sys.exit(main())
