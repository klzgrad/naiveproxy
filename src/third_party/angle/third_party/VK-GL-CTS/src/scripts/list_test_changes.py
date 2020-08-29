#!/usr/bin/env python3

# VK-GL-CTS log scrubber
# ----------------------
#
# Copyright (c) 2019 The Khronos Group Inc.
# Copyright (c) 2019 Google LLC
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

# This script attempts to find out which tests have changed since a
# certain time, release or changelist. The commit messages are scrubbed
# for dEQP test names, and these are merged to find a suitable set.
#
# The changelists that claim to change all tests are ignored.

import subprocess
import sys
import fnmatch
import re

assert sys.version_info >= (3, 0)

if len(sys.argv) == 1:
	print("""
VK-GL-CTS log scrubber
----------------------
This script attempts to list changed tests since certain time or
git revision. It does this by looking at git log.

Caveat: git log messages are written by humans, so there may be
errors. Overly broad changes are ignored (e.g, dEQP-VK.*).

Usage: Give the git log parameters

Examples:""")
	print(sys.argv[0], '--since="two months ago"')
	print(sys.argv[0], '--since="7.7.2019"')
	print(sys.argv[0], 'vulkan-cts-1.1.3.1..HEAD')
	quit()

params = ""
first = True
for x in sys.argv[1:]:
	if not first:
		params = params + " "
	params = params + x
	first = False

res = []

rawlogoutput = subprocess.check_output(['git', 'log', params, '--pretty=format:"%B"'])
logoutput = rawlogoutput.decode().split()
for x in logoutput:
	xs = x.strip()
	# regexp matches various over-large test masks like "dEQP-*", "dEQP-VK*", "dEQP-VK.*",
	# but not "dEQP-VK.a" or "dEQP-VK.*a"
	if xs.startswith('dEQP-') and not re.search('dEQP-\w*\**\.*\**$',xs):
		found = False
		killlist = []
		for y in res:
			if fnmatch.fnmatch(xs, y):
				found = True
			if fnmatch.fnmatch(y, xs):
				killlist.append(y)
		for y in killlist:
			res.remove(y)
		if not found:
			res.append(xs)
for x in sorted(res):
	print(x)
print(len(res), 'total')
