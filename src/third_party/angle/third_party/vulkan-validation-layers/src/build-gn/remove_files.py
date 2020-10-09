#!/usr/bin/python2

# Copyright (C) 2019 The ANGLE Project Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# remove_files.py:
#   This special action is used to cleanup old files from the build directory.
#   Otherwise ANGLE will pick up the old file(s), causing build or runtime errors.
#

import glob
import os
import sys

if len(sys.argv) < 3:
    print("Usage: " + sys.argv[0] + " <stamp_file> <remove_patterns>")

stamp_file = sys.argv[1]

for i in range(2, len(sys.argv)):
    remove_pattern = sys.argv[i]
    remove_files = glob.glob(remove_pattern)
    for f in remove_files:
        if os.path.isfile(f):
            os.remove(f)

# touch a dummy file to keep a timestamp
with open(stamp_file, "w") as f:
    f.write("blah")
    f.close()
