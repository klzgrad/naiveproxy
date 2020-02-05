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

import shlex
import xml.dom.minidom

class StatusCode:
	PASS					= 'Pass'
	FAIL					= 'Fail'
	QUALITY_WARNING			= 'QualityWarning'
	COMPATIBILITY_WARNING	= 'CompatibilityWarning'
	PENDING					= 'Pending'
	NOT_SUPPORTED			= 'NotSupported'
	RESOURCE_ERROR			= 'ResourceError'
	INTERNAL_ERROR			= 'InternalError'
	CRASH					= 'Crash'
	TIMEOUT					= 'Timeout'

	STATUS_CODES			= [
		PASS,
		FAIL,
		QUALITY_WARNING,
		COMPATIBILITY_WARNING,
		PENDING,
		NOT_SUPPORTED,
		RESOURCE_ERROR,
		INTERNAL_ERROR,
		CRASH,
		TIMEOUT
		]
	STATUS_CODE_SET			= set(STATUS_CODES)

	@staticmethod
	def isValid (code):
		return code in StatusCode.STATUS_CODE_SET

class TestCaseResult:
	def __init__ (self, name, statusCode, statusDetails, log):
		self.name			= name
		self.statusCode		= statusCode
		self.statusDetails	= statusDetails
		self.log			= log

	def __str__ (self):
		return "%s: %s (%s)" % (self.name, self.statusCode, self.statusDetails)

class ParseError(Exception):
	def __init__ (self, filename, line, message):
		self.filename	= filename
		self.line		= line
		self.message	= message

	def __str__ (self):
		return "%s:%d: %s" % (self.filename, self.line, self.message)

def splitContainerLine (line):
	return shlex.split(line)

def getNodeText (node):
	rc = []
	for node in node.childNodes:
		if node.nodeType == node.TEXT_NODE:
			rc.append(node.data)
	return ''.join(rc)

class BatchResultParser:
	def __init__ (self):
		pass

	def parseFile (self, filename):
		self.init(filename)

		f = open(filename, 'rb')
		for line in f:
			self.parseLine(line)
			self.curLine += 1
		f.close()

		return self.testCaseResults

	def getNextTestCaseResult (self, file):
		try:
			del self.testCaseResults[:]
			self.curResultText = None

			isNextResult = self.parseLine(file.next())
			while not isNextResult:
				isNextResult = self.parseLine(file.next())

			# Return the next TestCaseResult
			return self.testCaseResults.pop()

		except StopIteration:
			# If end of file was reached and there is no log left, the parsing finished successful (return None).
			# Otherwise, if there is still log to be parsed, it means that there was a crash.
			if self.curResultText:
				return TestCaseResult(self.curCaseName, StatusCode.CRASH, StatusCode.CRASH, self.curResultText)
			else:
				return None

	def init (self, filename):
		# Results
		self.sessionInfo		= []
		self.testCaseResults	= []

		# State
		self.curResultText		= None
		self.curCaseName		= None

		# Error context
		self.curLine			= 1
		self.filename			= filename

	def parseLine (self, line):
		if len(line) > 0 and line[0] == '#':
			return self.parseContainerLine(line)
		elif self.curResultText != None:
			self.curResultText += line
			return None
		# else: just ignored

	def parseContainerLine (self, line):
		isTestCaseResult = False
		args = splitContainerLine(line)
		if args[0] == "#sessionInfo":
			if len(args) < 3:
				print(args)
				self.parseError("Invalid #sessionInfo")
			self.sessionInfo.append((args[1], ' '.join(args[2:])))
		elif args[0] == "#beginSession" or args[0] == "#endSession":
			pass # \todo [pyry] Validate
		elif args[0] == "#beginTestCaseResult":
			if len(args) != 2 or self.curCaseName != None:
				self.parseError("Invalid #beginTestCaseResult")
			self.curCaseName	= args[1]
			self.curResultText	= ""
		elif args[0] == "#endTestCaseResult":
			if len(args) != 1 or self.curCaseName == None:
				self.parseError("Invalid #endTestCaseResult")
			self.parseTestCaseResult(self.curCaseName, self.curResultText)
			self.curCaseName	= None
			self.curResultText	= None
			isTestCaseResult	= True
		elif args[0] == "#terminateTestCaseResult":
			if len(args) < 2 or self.curCaseName == None:
				self.parseError("Invalid #terminateTestCaseResult")
			statusCode		= ' '.join(args[1:])
			statusDetails	= statusCode

			if not StatusCode.isValid(statusCode):
				# Legacy format
				if statusCode == "Watchdog timeout occurred.":
					statusCode = StatusCode.TIMEOUT
				else:
					statusCode = StatusCode.CRASH

			# Do not try to parse at all since XML is likely broken
			self.testCaseResults.append(TestCaseResult(self.curCaseName, statusCode, statusDetails, self.curResultText))

			self.curCaseName	= None
			self.curResultText	= None
			isTestCaseResult	= True
		else:
			# Assume this is result text
			if self.curResultText != None:
				self.curResultText += line

		return isTestCaseResult

	def parseTestCaseResult (self, name, log):
		try:
			# The XML parser has troubles with invalid characters deliberately included in the shaders.
			# This line removes such characters before calling the parser
			log = log.decode('utf-8','ignore').encode("utf-8")
			doc = xml.dom.minidom.parseString(log)
			resultItems = doc.getElementsByTagName('Result')
			if len(resultItems) != 1:
				self.parseError("Expected 1 <Result>, found %d" % len(resultItems))

			statusCode		= resultItems[0].getAttributeNode('StatusCode').nodeValue
			statusDetails	= getNodeText(resultItems[0])
		except Exception as e:
			statusCode		= StatusCode.INTERNAL_ERROR
			statusDetails	= "XML parsing failed: %s" % str(e)

		self.testCaseResults.append(TestCaseResult(name, statusCode, statusDetails, log))

	def parseError (self, message):
		raise ParseError(self.filename, self.curLine, message)
