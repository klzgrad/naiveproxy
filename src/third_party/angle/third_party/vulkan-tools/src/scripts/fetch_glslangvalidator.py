#!/usr/bin/env python3
#
# Copyright (c) 2018 The Khronos Group Inc.
# Copyright (c) 2018 Valve Corporation
# Copyright (c) 2018 LunarG, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Author: Mark Lobodzinski <mark@lunarg.com>


# This script will download the latest glslang release binary and extract the
# glslangValidator binary needed by the vkcube and vkcubepp applications.
#
# It takes as its lone argument the filname (no path) describing the release
# binary name from the glslang github releases page.

import sys
import os
import shutil
import ssl
import subprocess
import urllib.request
import zipfile

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.join(SCRIPTS_DIR, '..')
GLSLANG_URL = "https://github.com/KhronosGroup/glslang/releases/download/7.9.2888"

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("ERROR -- must include a single glslang release zipfile name argument")
        sys.exit();

    GLSLANG_FILENAME = sys.argv[1]
    GLSLANG_COMPLETE_URL = GLSLANG_URL + "/" + GLSLANG_FILENAME
    GLSLANG_OUTFILENAME = os.path.join(REPO_DIR, "glslang", GLSLANG_FILENAME)
    GLSLANG_VALIDATOR_PATH = os.path.join(REPO_DIR, "glslang", "bin")
    GLSLANG_VALIDATOR_FULL_PATH = os.path.join(REPO_DIR, "glslang", "bin", "glslangValidator")
    GLSLANG_DIR = os.path.join(REPO_DIR, "glslang")

    if os.path.isdir(GLSLANG_DIR):
        if os.path.isdir(GLSLANG_VALIDATOR_PATH):
            dir_contents = os.listdir(GLSLANG_VALIDATOR_PATH)
            for afile in dir_contents:
                if "glslangValidator" in afile:
                    print("   Using glslangValidator at %s" % GLSLANG_VALIDATOR_PATH)
                    sys.exit();
    else:
        os.mkdir(GLSLANG_DIR)
    print("   Downloading glslangValidator binary from glslang releases dir")
    sys.stdout.flush()

    # Download release zip file from glslang github releases site
    with urllib.request.urlopen(GLSLANG_COMPLETE_URL, context=ssl._create_unverified_context()) as response, open(GLSLANG_OUTFILENAME, 'wb') as out_file:
        shutil.copyfileobj(response, out_file)
    # Unzip the glslang binary archive
    zipped_file = zipfile.ZipFile(GLSLANG_OUTFILENAME, 'r')
    namelist = zipped_file.namelist()
    for afile in namelist:
        if "glslangValidator" in afile:
            EXE_FILE_PATH = os.path.join(GLSLANG_DIR, afile)
            zipped_file.extract(afile, GLSLANG_DIR)
            os.chmod(EXE_FILE_PATH, 0o775)
            break
    zipped_file.close()
    sys.exit();
