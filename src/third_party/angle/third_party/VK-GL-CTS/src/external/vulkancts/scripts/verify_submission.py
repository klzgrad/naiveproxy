# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Vulkan CTS
# ----------
#
# Copyright (c) 2016 Google Inc.
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

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts", "verify"))

from package import getPackageDescription
from verify import *
from message import *

def verifyGitStatusFiles (package):
	messages = []

	if len(package.gitStatus) > 1:
		messages.append(error(package.basePath, "Exactly one git status file must be present, found %s" % len(package.gitStatus)))

	messages += verifyGitStatus(package)

	return messages

def verifyGitLogFiles (package):
	messages = []

	if len(package.gitLog) > 1:
		messages.append(error(package.basePath, "Exactly one git log file must be present, found %s" % len(package.gitLog)))

	messages += verifyGitLog(package)

	return messages

def verifyTestLogs (package, mustpass):
	messages	= []

	for testLogFile in package.testLogs:
		messages += verifyTestLog(os.path.join(package.basePath, testLogFile), mustpass)

	if len(package.testLogs) == 0:
		messages.append(error(package.basePath, "No test log files found"))

	return messages

def verifyPackage (package, mustpass):
	messages = []

	messages += verifyStatement(package)
	messages += verifyGitStatusFiles(package)
	messages += verifyGitLogFiles(package)
	messages += verifyPatches(package)
	messages += verifyTestLogs(package, mustpass)

	for item in package.otherItems:
		messages.append(warning(os.path.join(package.basePath, item), "Unknown file"))

	return messages

if __name__ == "__main__":
	if len(sys.argv) != 3:
		print("%s: [extracted submission package] [mustpass]" % sys.argv[0])
		sys.exit(-1)

	packagePath		= os.path.normpath(sys.argv[1])
	mustpassPath	= sys.argv[2]
	package			= getPackageDescription(packagePath)
	mustpass		= readMustpass(mustpassPath)
	messages		= verifyPackage(package, mustpass)

	errors			= [m for m in messages if m.type == ValidationMessage.TYPE_ERROR]
	warnings		= [m for m in messages if m.type == ValidationMessage.TYPE_WARNING]

	for message in messages:
		print(str(message))

	print("")

	if len(errors) > 0:
		print("Found %d validation errors and %d warnings!" % (len(errors), len(warnings)))
		sys.exit(-2)
	elif len(warnings) > 0:
		print("Found %d warnings, manual review required" % len(warnings))
		sys.exit(-1)
	else:
		print("All validation checks passed")
