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

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "log"))
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "build"))

from common import readFile
from message import *
from log_parser import StatusCode, BatchResultParser

ALLOWED_STATUS_CODES = set([
		StatusCode.PASS,
		StatusCode.NOT_SUPPORTED,
		StatusCode.QUALITY_WARNING,
		StatusCode.COMPATIBILITY_WARNING
	])

def readMustpass (filename):
	f = open(filename, 'rt')
	cases = []
	for line in f:
		s = line.strip()
		if len(s) > 0:
			cases.append(s)
	return cases

def readTestLog (filename):
	parser = BatchResultParser()
	return parser.parseFile(filename)

def verifyTestLog (filename, mustpass):
	results			= readTestLog(filename)
	messages			= []
	resultOrderOk	= True

	# Mustpass case names must be unique
	assert len(mustpass) == len(set(mustpass))

	# Verify number of results
	if len(results) != len(mustpass):
		messages.append(error(filename, "Wrong number of test results, expected %d, found %d" % (len(mustpass), len(results))))

	caseNameToResultNdx = {}
	for ndx in range(len(results)):
		result = results[ndx]
		if not result in caseNameToResultNdx:
			caseNameToResultNdx[result.name] = ndx
		else:
			messages.append(error(filename, "Multiple results for " + result.name))

	# Verify that all results are present and valid
	for ndx in range(len(mustpass)):
		caseName = mustpass[ndx]

		if caseName in caseNameToResultNdx:
			resultNdx	= caseNameToResultNdx[caseName]
			result		= results[resultNdx]

			if resultNdx != ndx:
				resultOrderOk = False

			if not result.statusCode in ALLOWED_STATUS_CODES:
				messages.append(error(filename, result.name + ": " + result.statusCode))
		else:
			messages.append(error(filename, "Missing result for " + caseName))

	if len(results) == len(mustpass) and not resultOrderOk:
		messages.append(error(filename, "Results are not in the expected order"))

	return messages

def beginsWith (str, prefix):
	return str[:len(prefix)] == prefix

def verifyStatement (package):
	messages	= []

	if package.statement != None:
		statementPath	= os.path.join(package.basePath, package.statement)
		statement		= readFile(statementPath)
		hasVersion		= False
		hasProduct		= False
		hasCpu			= False
		hasOs			= False

		for line in statement.splitlines():
			if beginsWith(line, "CONFORM_VERSION:"):
				if hasVersion:
					messages.append(error(statementPath, "Multiple CONFORM_VERSIONs"))
				else:
					assert len(line.split()) >= 2
					package.conformVersion = line.split()[1]
					hasVersion = True
			elif beginsWith(line, "PRODUCT:"):
				hasProduct = True # Multiple products allowed
			elif beginsWith(line, "CPU:"):
				if hasCpu:
					messages.append(error(statementPath, "Multiple PRODUCTs"))
				else:
					hasCpu = True
			elif beginsWith(line, "OS:"):
				if hasOs:
					messages.append(error(statementPath, "Multiple OSes"))
				else:
					assert len(line.split()) >= 2
					package.conformOs = line.split()[1]
					hasOs = True

		if not hasVersion:
			messages.append(error(statementPath, "No CONFORM_VERSION"))
		if not hasProduct:
			messages.append(error(statementPath, "No PRODUCT"))
		if not hasCpu:
			messages.append(error(statementPath, "No CPU"))
		if not hasOs:
			messages.append(error(statementPath, "No OS"))
	else:
		messages.append(error(package.basePath, "Missing conformance statement file"))

	return messages

def verifyGitStatus (package):
	messages = []

	if len(package.gitStatus) > 0:
		for s in package.gitStatus:
			statusPath	= os.path.join(package.basePath, s)
			status		= readFile(statusPath)

			if status.find("nothing to commit, working directory clean") < 0 and status.find("nothing to commit, working tree clean") < 0:
				messages.append(error(package.basePath, "Working directory is not clean"))
	else:
		messages.append(error(package.basePath, "Missing git status files"))

	return messages

def isGitLogEmpty (package, gitLog):
	logPath	= os.path.join(package.basePath, gitLog)
	log		= readFile(logPath)

	return len(log.strip()) == 0

def verifyGitLog (package):
	messages = []

	if len(package.gitLog) > 0:
		for log, path in package.gitLog:
			if not isGitLogEmpty(package, log):
				messages.append(warning(os.path.join(package.basePath, log), "Log is not empty"))
	else:
		messages.append(error(package.basePath, "Missing git log files"))

	return messages

def verifyPatches (package):
	messages	= []
	hasPatches	= len(package.patches)
	logEmpty	= True
	for log, path in package.gitLog:
		logEmpty &= isGitLogEmpty(package, log)

	if hasPatches and logEmpty:
		messages.append(error(package.basePath, "Package includes patches but log is empty"))
	elif not hasPatches and not logEmpty:
		messages.append(error(package.basePath, "Test log is not empty but package doesn't contain patches"))

	return messages
