# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright (c) 2017 The Khronos Group Inc.
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
import re
import sys
from argparse import ArgumentParser
from common import getChangedFiles, getAllProjectFiles, isTextFile

CHECK_LITERAL_PATTERNS = [
	r'\b[us]*int[0-9]+_t\b',
	r'\b[U]*INT(_LEAST|_FAST|)[0-9]+_MAX\b',
	r'\b0b',
]

CHECK_LIST = [
	".cpp",
	".hpp",
	".c",
	".h",
]

EXCLUSION_LIST = [
	"framework/delibs/debase/deDefs.h",
	"framework/platform/android/tcuAndroidPlatform.cpp",
	"framework/platform/android/tcuAndroidWindow.hpp",
	"framework/platform/android/tcuAndroidWindow.cpp",
	"framework/platform/lnx/X11/tcuLnxX11Xcb.cpp",
	"framework/platform/lnx/wayland/tcuLnxWayland.hpp",
	"framework/platform/lnx/wayland/tcuLnxWayland.cpp",
	"framework/delibs/debase/deFloat16.c",
]

def checkEnds(line, ends):
	return any(line.endswith(end) for end in ends)

def checkFileInvalidLiterals (file):
	error = False

	if checkEnds(file.replace("\\", "/"), CHECK_LIST) and not checkEnds(file.replace("\\", "/"), EXCLUSION_LIST):
		f = open(file, 'rt')
		for lineNum, line in enumerate(f):
			# Remove inline comments
			idx = line.find("//")
			if idx > 0:
				line = line[:idx]
			# Remove text in quoted literals
			if line.find("\"") > 0:
				list = line.split('"')
				del list[1::2]
				line = ' '
				line = line.join(list)
			for pattern in CHECK_LITERAL_PATTERNS:
				found = re.search(pattern, line)
				if found is not None:
					error = True
					print("%s:%i Unacceptable type found (pattern:%s)" % (file, lineNum+1, pattern))
		f.close()

	return not error

def checkInvalidLiterals (files):
	error = False
	for file in files:
		if isTextFile(file):
			if not checkFileInvalidLiterals(file):
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

	# filter out original Vulkan header sources
	files = [f for f in files if "vulkancts/scripts/src" not in f]

	error = not checkInvalidLiterals(files)

	if error:
		print("One or more checks failed")
		sys.exit(1)
	if not args.onlyErrors:
		print("All checks passed")
