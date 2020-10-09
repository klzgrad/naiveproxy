# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Khronos OpenGL CTS
# ------------------
#
# Copyright (c) 2016 The Khronos Group Inc.
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
import sys
import shutil
import argparse
import subprocess

from fetch_sources import *
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "scripts"))

from build.common import *

EXTERNAL_DIR	= os.path.realpath(os.path.normpath(os.path.dirname(__file__)))
SHA1 = "d937540ff74441ac9b5cefbcf1e82d0f97ffdebf"

PACKAGES = [
	GitRepo(
		"https://gitlab.khronos.org/opengl/kc-cts.git",
		"git@gitlab.khronos.org:opengl/kc-cts.git",
		SHA1,
		"kc-cts"),
]

if __name__ == "__main__":
	args = parseArgs()

	for pkg in PACKAGES:
		if args.clean:
			pkg.clean()
		else:
			pkg.update(args.protocol)
