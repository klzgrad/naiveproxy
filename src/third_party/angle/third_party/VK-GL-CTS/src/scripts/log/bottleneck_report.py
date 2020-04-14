# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Quality Program utilities
# --------------------------------------
#
# Copyright 2018
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
import copy
import sys
import xml.sax
import xml.sax.handler
from log_parser import BatchResultParser, StatusCode

class TimeOfExecutionGroups() :
	def __init__(self):
		self.path				= ""
		self.numberOfTests		= 0
		self.timeOfExecution	= 0

class TimeOfExecutionTests() :
	def __init__(self):
		self.path				= ""
		self.timeOfExecution	= 0

def sortKey (element ) :
	return int(element.timeOfExecution)

def sortKeyTimePerTest (element) :
	return int(int(element.timeOfExecution)/int(element.numberOfTests))

class XMLLogHandlerTests(xml.sax.handler.ContentHandler) :
	def __init__ (self):
		self.list			= []
		self.element		= TimeOfExecutionTests()
		self.testTime		= False

	def startElement (self, name, attrs):
		if name == "TestCaseResult" :
			self.element.path = attrs.getValue("CasePath")
		if name == "Number" and "TestDuration" == attrs.getValue("Name") :
			self.testTime = True

	def characters(self, content) :
		if self.testTime :
			self.testTime = False
			self.element.timeOfExecution = content
			self.list.append(copy.deepcopy(self.element))

	def bottleneck (self, resultCount) :
		print("The biggest tests time of execution")
		print('%-4s%12s\t%12s' % ("Index", "Time", "Full name"))
		self.list.sort(key = sortKey, reverse = True)
		ndx = 1
		for test in self.list :
			print('%-4i%12i\t%12s' % (int(ndx), int(test.timeOfExecution), test.path))
			ndx+=1
			if int(ndx) > int(resultCount) :
				break

class XMLLogHandlerGroups(xml.sax.handler.ContentHandler) :
	def __init__ (self, testList) :
		self.list			= []
		self.testList		= testList
		self.element		= TimeOfExecutionGroups()
		self.addIt			= False

	def startElement (self, name, attrs) :
		self.element.numberOfTests = 0
		if name == "Number" :
			self.element.path = attrs.getValue("Name")
			if self.element.path == "dEQP-VK" :
				self.addIt = True
				self.element.numberOfTests = len(self.testList)
			else :
				for test in self.testList :
					if test.path[:test.path.rfind(".")] in self.element.path :
						self.addIt = True
						self.element.numberOfTests += 1

	def characters(self, content) :
		if self.addIt :
			self.addIt = False
			self.element.timeOfExecution = content
			self.list.append(copy.deepcopy(self.element))

	def bottleneck (self, resultCount) :
		self.list.sort(key = sortKey, reverse = True)
		print("\nGroups Statistics")
		print("Total time of execution:\t", self.list[0].timeOfExecution)
		print("Number of executed tests:\t", self.list[0].numberOfTests)
		print("\nThe biggest total time of execution")
		print('%-4s%15s%15s\t%-30s' % ("Index", "Time", "Test count", "Full name"))
		ndx = 1
		for test in self.list :
			if test.path == "dEQP-VK" :
				continue
			print('%-4s%15s%15s\t%-30s' % (ndx, test.timeOfExecution, test.numberOfTests, test.path))
			ndx+=1
			if int(ndx) > int(resultCount) :
				break
		self.list.sort(key = sortKeyTimePerTest, reverse = True)
		print("\nThe biggest time of execution per test")
		print('%-4s%15s%15s%15s\t%-30s' % ("Index", "Time", "Test count", "\tAvg. test time", "Full name"))
		ndx = 1
		for test in self.list :
			if test.path == "dEQP-VK" :
				continue
			print('%-4s%15s%15s%15i\t%-30s' % (ndx, test.timeOfExecution, test.numberOfTests, int(test.timeOfExecution)/int(test.numberOfTests), test.path))
			ndx+=1
			if int(ndx) > int(resultCount) :
				break

class LogErrorHandler(xml.sax.handler.ErrorHandler) :
	def __init__ (self) :
		pass

	def error (self, err) :
		#print("error(%s)" % str(err))
		pass

	def fatalError (self, err) :
		#print("fatalError(%s)" % str(err))
		pass

	def warning (self, warn) :
		#print("warning(%s)" % str(warn))
		pass

def findFirstElementByName (nodes, name) :
	for node in nodes:
		if node.nodeName == name :
			return node
		chFound = findFirstElementByName(node.childNodes, name)
		if chFound != None :
			return chFound
	return None

def printTimes (inFile, resultCount) :
	#Test section
	parser	= BatchResultParser()
	results	= parser.parseFile(inFile)
	handler		= XMLLogHandlerTests()
	errHandler	= LogErrorHandler()
	for result in results :
		xml.sax.parseString(result.log, handler, errHandler)
	handler.bottleneck(resultCount)

	#Group section
	startRecordLines = False
	lines = ""
	f = open(inFile, 'rb')
	for line in f :
		if "#endTestsCasesTime" in line :
			break
		if startRecordLines :
			lines += line
		if "#beginTestsCasesTime" in line :
			startRecordLines = True
	f.close()
	handlerGroups = XMLLogHandlerGroups(handler.list)
	xml.sax.parseString(lines, handlerGroups, errHandler)
	handlerGroups.bottleneck(resultCount)

if __name__ == "__main__" :
	if len(sys.argv) != 3:
		print("%s: [test log] [count of result to display]" % sys.argv[0])
		print("example: python %s TestResults.qpa 10" % sys.argv[0])
		sys.exit(-1)
	printTimes(sys.argv[1], sys.argv[2])
