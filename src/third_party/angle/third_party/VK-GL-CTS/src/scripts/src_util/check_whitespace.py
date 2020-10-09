# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2015 The Android Open Source Project
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
#
#-------------------------------------------------------------------------

import sys
from argparse import ArgumentParser
from common import getChangedFiles, getAllProjectFiles, isTextFile

def checkFileWhitespace (file):
    if (sys.version_info < (3, 0)):
        f = open(file, 'rt')
    else:
        f = open(file, 'rt', encoding="ascii", errors='replace', newline='')
    error = False
    for lineNum, line in enumerate(f):
        if line.endswith(" \n") or line.endswith("\t\n"):
            error = True
            print("%s:%i trailing whitespace" % (file, lineNum+1))
        if " \t" in line:
            error = True
            print("%s:%i merged <space><tab>" % (file, lineNum+1))
        if line.endswith("\r") or line.endswith("\r\n"):
            error = True
            print("%s:%i incorrect line ending" % (file, lineNum+1))
    f.close()

    return not error

def checkWhitespace (files):
    error = False
    for file in files:
        if isTextFile(file):
            if not checkFileWhitespace(file):
                error = True

    return not error

if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("-e", "--only-errors",  action="store_true", dest="onlyErrors",   default=False, help="Print only on error")
    parser.add_argument("-i", "--only-changed", action="store_true", dest="useGitIndex",  default=False, help="Check only modified files. Uses git.")

    args = parser.parse_args()

    if args.useGitIndex:
        files = getChangedFiles()
    else:
        files = getAllProjectFiles()

    error = not checkWhitespace(files)

    if error:
        print("One or more checks failed")
        sys.exit(1)
    if not args.onlyErrors:
        print("All checks passed")
