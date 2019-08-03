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

import re
import sys
from fnmatch import fnmatch

def fail (msg):
	print "ERROR: " + msg
	sys.exit(-1)

# filename -> [case name]
def readCaseList (filename):
	f = open(filename, 'rb')
	cases = []
	for line in f:
		if line[0:6] == "TEST: ":
			cases.append(line[6:].strip())
	f.close()
	return cases

# filename -> [(filter, min, recommended)]
def readResolutionList (filename):
	f = open(filename, 'rb')
	resList = []
	for line in f:
		line = line.strip()
		params = line.split('\t')
		if len(params) == 3:
			resList.append((params[0], params[1], params[2]))
		elif len(params) != 0:
			fail("Invalid line in resolution list: '%s'" % line)
	f.close()
	return resList

def getMatchingCases (cases, pattern):
	matching = []
	for case in cases:
		if fnmatch(case, pattern):
			matching.append(case)
	return matching

def isResolutionOk (res):
	return re.match('^[1-9][0-9]*x[1-9][0-9]*$', res) != None

if __name__ == "__main__":
	if len(sys.argv) != 3:
		print "%s: [caselist] [resolution list]" % sys.argv[0]
		sys.exit(-1)

	caseList	= readCaseList(sys.argv[1])
	resList		= readResolutionList(sys.argv[2])

	# Pass 1: sanity check for resolution values
	for pattern, minRes, recRes in resList:
		if not isResolutionOk(minRes) or not isResolutionOk(recRes):
			fail("Invalid resolution: '%s %s %s'" % (pattern, minRes, recRes))

	# Pass 2: check that each case is specified by one rule
	markedCases = set()
	for pattern, minRes, recRes in resList:
		matchingCases = getMatchingCases(caseList, pattern)

		if len(matchingCases) == 0:
			fail("Pattern '%s' does not match any cases" % pattern)

		for case in matchingCases:
			if case in markedCases:
				fail("Case '%s' specified multiple times (when processing '%s')" % (case, pattern))
			markedCases.add(case)

	for case in caseList:
		if not case in markedCases:
			fail("Case '%s' not specified by any rule" % case)
