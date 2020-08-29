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
from fnmatch import fnmatch

STATEMENT_PATTERN	= "STATEMENT-*"
TEST_LOG_PATTERN	= "*.qpa"
GIT_STATUS_PATTERN	= "*git-status.txt"
GIT_LOG_PATTERN		= "*git-log.txt"
PATCH_PATTERN		= "*.patch"
SUMMARY_PATTERN		= "cts-run-summary.xml"

class PackageDescription:
	def __init__ (self, basePath, statement, testLogs, gitStatus, gitLog, patches, summary, conformVersion, conformOs, otherItems):
		self.basePath		= basePath
		self.statement		= statement
		self.testLogs		= testLogs
		self.gitStatus		= gitStatus
		self.gitLog			= gitLog
		self.patches		= patches
		self.summary		= summary
		self.otherItems		= otherItems
		self.conformVersion	= conformVersion
		self.conformOs		= conformOs

def getPackageDescription (packagePath):
	allItems	= os.listdir(packagePath)
	statement	= None
	testLogs	= []
	gitStatus	= []
	gitLog		= []
	patches		= []
	summary		= None
	otherItems	= []
	conformVersion	= None
	conformOs		= None

	for item in allItems:
		if fnmatch(item, STATEMENT_PATTERN):
			assert statement == None
			statement = item
		elif fnmatch(item, TEST_LOG_PATTERN):
			testLogs.append(item)
		elif fnmatch(item, GIT_STATUS_PATTERN):
			gitStatus.append(item)
		elif fnmatch(item, GIT_LOG_PATTERN):
			gitLog.append((item, '.'))
		elif fnmatch(item, PATCH_PATTERN):
			patches.append(item)
		elif fnmatch(item, SUMMARY_PATTERN):
			assert summary == None
			summary = item
		else:
			otherItems.append(item)

	return PackageDescription(packagePath, statement, testLogs, gitStatus, gitLog, patches, summary, conformVersion, conformOs, otherItems)
