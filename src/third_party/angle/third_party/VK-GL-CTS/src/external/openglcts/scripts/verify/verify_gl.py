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
import xml.dom.minidom

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts", "log"))

from log_parser import BatchResultParser, StatusCode

from summary import *

VALID_STATUS_CODES = set([
	StatusCode.PASS,
	StatusCode.COMPATIBILITY_WARNING,
	StatusCode.QUALITY_WARNING,
	StatusCode.NOT_SUPPORTED
	])

def isStatusCodeOk (code):
	return code in VALID_STATUS_CODES

def getConfigCaseName (type):
	return "CTS-Configs.%s" % type

def verifyConfigFile (filename, type):
	caseName = getConfigCaseName(type)

	print("Verifying %s in %s" % (caseName, filename))

	parser		= BatchResultParser()
	results		= parser.parseFile(filename)
	caseResult	= None

	for result in results:
		if result.name == caseName:
			caseResult = result
			break

	if caseResult == None:
		print("FAIL: %s not found" % caseName)
		return False

	if not isStatusCodeOk(caseResult.statusCode):
		print("FAIL: %s" % caseResult)
		return False

	return True

def verifySubmission (dirname):
	summary	= parseRunSummary(os.path.join(dirname, "cts-run-summary.xml"))
	allOk	= True

	# Check Conformant attribute
	if not summary.isConformant:
		print("FAIL: Runner reported conformance failure (Conformant=\"False\" in <Summary>)")

	# Verify config list
	if not verifyConfigFile(os.path.join(dirname, summary.configLogFilename), summary.type):
		allOk = False

	# Verify that all run files passed
	for runFilename in summary.runLogFilenames:
		print("Verifying %s" % runFilename)

		logParser	= BatchResultParser()
		batchResult	= logParser.parseFile(os.path.join(dirname, runFilename))

		for result in batchResult:
			if not isStatusCodeOk(result.statusCode):
				print("FAIL: %s" % str(result))
				allOk = False

	return allOk

def verifyGLSubmission(argv):
	if len(argv) != 2:
		print("%s: [extracted submission package directory]" % argv[0])
		sys.exit(-1)

	try:
		isOk = verifySubmission(argv[1])
		print("Verification %s" % ("PASSED" if isOk else "FAILED!"))
		sys.exit(0 if isOk else 1)
	except Exception as e:
		print(str(e))
		print("Error occurred during verification")
		sys.exit(-1)

if __name__ == "__main__":
	verifyGLSubmission(sys.argv)

