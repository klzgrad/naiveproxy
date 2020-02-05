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

import	sys
from	argparse				import	ArgumentParser
from	common					import	getChangedFiles, getAllProjectFiles
from	check_include_guards	import	checkIncludeGuards
from	check_encoding			import	checkEncoding
from	check_whitespace		import	checkWhitespace
from	check_license			import	checkLicense
from	check_boms				import	checkBOMs
from	check_invalid_literals	import	checkInvalidLiterals

if __name__ == "__main__":
	parser = ArgumentParser()
	parser.add_argument("-e",	"--only-errors",	action="store_true",	dest="onlyErrors",		default=False,	help="Print only on error")
	parser.add_argument("-i",	"--only-changed",	action="store_true",	dest="useGitIndex",		default=False,	help="Check only modified files. Uses git.")
	parser.add_argument("-b",	"--fix-bom",		action="store_true",	dest="fixBOMs",			default=False,	help="Attempt to fix BOMs")

	args = parser.parse_args()

	if args.useGitIndex:
		files = getChangedFiles()
	else:
		files = getAllProjectFiles()

	# filter out original Vulkan header sources
	files = [f for f in files if "vulkancts/scripts/src" not in f.replace("\\", "/")]

	error = not all([
		checkBOMs(files, args.fixBOMs),
		checkEncoding(files),
		checkWhitespace(files),
		checkIncludeGuards(files),
		checkLicense(files),
		checkInvalidLiterals(files),
		#todo checkRedundantIncludeGuards(files),
		])

	if	error:
		print("One or more checks failed")
		sys.exit(1)
	if	not	args.onlyErrors:
		print("All checks passed")
