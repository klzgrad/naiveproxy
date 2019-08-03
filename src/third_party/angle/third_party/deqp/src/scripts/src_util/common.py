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

import os
import subprocess

TEXT_FILE_EXTENSION = [
    ".bat",
    ".c",
    ".cfg",
    ".cmake",
    ".cpp",
    ".css",
    ".h",
    ".hh",
    ".hpp",
    ".html",
    ".inl",
    ".java",
    ".js",
    ".m",
    ".mk",
    ".mm",
    ".py",
    ".rule",
    ".sh",
    ".test",
    ".txt",
    ".xml",
    ".xsl",
    ]

BINARY_FILE_EXTENSION = [
    ".png",
    ".pkm",
    ".xcf",
    ]

def isTextFile (filePath):
    ext = os.path.splitext(filePath)[1]
    if ext in TEXT_FILE_EXTENSION:
        return True
    if ext in BINARY_FILE_EXTENSION:
        return False

    # Analyze file contents, zero byte is the marker for a binary file
    f = open(filePath, "rb")

    TEST_LIMIT = 1024
    nullFound = False
    numBytesTested = 0

    byte = f.read(1)
    while byte and numBytesTested < TEST_LIMIT:
        if byte == "\0":
            nullFound = True
            break

        byte = f.read(1)
        numBytesTested += 1

    f.close()
    return not nullFound

def getProjectPath ():
    # File system hierarchy is fixed
    scriptDir = os.path.dirname(os.path.abspath(__file__))
    projectDir = os.path.normpath(os.path.join(scriptDir, "../.."))
    return projectDir

def git (*args):
    process = subprocess.Popen(['git'] + list(args), cwd=getProjectPath(), stdout=subprocess.PIPE)
    output = process.communicate()[0]
    if process.returncode != 0:
        raise Exception("Failed to execute '%s', got %d" % (str(args), process.returncode))
    return output

def getAbsolutePathPathFromProjectRelativePath (projectRelativePath):
    return os.path.normpath(os.path.join(getProjectPath(), projectRelativePath))

def getChangedFiles ():
    # Added, Copied, Moved, Renamed
    output = git('diff', '--cached', '--name-only', '-z', '--diff-filter=ACMR')
    relativePaths = output.split('\0')[:-1] # remove trailing ''
    return [getAbsolutePathPathFromProjectRelativePath(path) for path in relativePaths]

def getAllProjectFiles ():
    output = git('ls-files', '--cached', '-z')
    relativePaths = output.split('\0')[:-1] # remove trailing ''
    return [getAbsolutePathPathFromProjectRelativePath(path) for path in relativePaths]
