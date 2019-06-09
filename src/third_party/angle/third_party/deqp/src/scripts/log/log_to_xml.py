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

import os
import sys
import codecs
import xml.dom.minidom
import xml.sax
import xml.sax.handler
from log_parser import BatchResultParser, StatusCode

STYLESHEET_FILENAME = "testlog.xsl"
LOG_VERSION			= '0.3.2'

class BuildXMLLogHandler(xml.sax.handler.ContentHandler):
	def __init__ (self, doc):
		self.doc			= doc
		self.elementStack	= []
		self.rootElements	= []

	def getRootElements (self):
		return self.rootElements

	def pushElement (self, elem):
		if len(self.elementStack) == 0:
			self.rootElements.append(elem)
		else:
			self.getCurElement().appendChild(elem)
		self.elementStack.append(elem)

	def popElement (self):
		self.elementStack.pop()

	def getCurElement (self):
		if len(self.elementStack) > 0:
			return self.elementStack[-1]
		else:
			return None

	def startDocument (self):
		pass

	def endDocument (self):
		pass

	def startElement (self, name, attrs):
		elem = self.doc.createElement(name)
		for name in attrs.getNames():
			value = attrs.getValue(name)
			elem.setAttribute(name, value)
		self.pushElement(elem)

	def endElement (self, name):
		self.popElement()

	def characters (self, content):
		# Discard completely empty content
		if len(content.strip()) == 0:
			return

		# Append as text node (not pushed to stack)
		if self.getCurElement() != None:
			txt = self.doc.createTextNode(content)
			self.getCurElement().appendChild(txt)

class LogErrorHandler(xml.sax.handler.ErrorHandler):
	def __init__ (self):
		pass

	def error (self, err):
		#print "error(%s)" % str(err)
		pass

	def fatalError (self, err):
		#print "fatalError(%s)" % str(err)
		pass

	def warning (self, warn):
		#print "warning(%s)" % str(warn)
		pass

def findFirstElementByName (nodes, name):
	for node in nodes:
		if node.nodeName == name:
			return node
		chFound = findFirstElementByName(node.childNodes, name)
		if chFound != None:
			return chFound
	return None

# Normalizes potentially broken (due to crash for example) log data to XML element tree
def normalizeToXml (result, doc):
	handler		= BuildXMLLogHandler(doc)
	errHandler	= LogErrorHandler()

	xml.sax.parseString(result.log, handler, errHandler)

	rootNodes = handler.getRootElements()

	# Check if we have TestCaseResult
	testCaseResult = findFirstElementByName(rootNodes, 'TestCaseResult')
	if testCaseResult == None:
		# Create TestCaseResult element
		testCaseResult = doc.createElement('TestCaseResult')
		testCaseResult.setAttribute('CasePath', result.name)
		testCaseResult.setAttribute('CaseType', 'SelfValidate') # \todo [pyry] Not recoverable..
		testCaseResult.setAttribute('Version', LOG_VERSION)
		rootNodes.append(testCaseResult)

	# Check if we have Result element
	resultElem = findFirstElementByName(rootNodes, 'Result')
	if resultElem == None:
		# Create result element
		resultElem = doc.createElement('Result')
		resultElem.setAttribute('StatusCode', result.statusCode)
		resultElem.appendChild(doc.createTextNode(result.statusDetails))
		testCaseResult.appendChild(resultElem)

	return rootNodes

def logToXml (inFile, outFile):
	parser	= BatchResultParser()
	results	= parser.parseFile(inFile)

	dstDoc			= xml.dom.minidom.Document()
	batchResultNode	= dstDoc.createElement('BatchResult')
	batchResultNode.setAttribute("FileName", os.path.basename(inFile))

	dstDoc.appendChild(batchResultNode)

	for result in results:
		# Normalize log to valid XML
		rootNodes = normalizeToXml(result, dstDoc)
		for node in rootNodes:
			batchResultNode.appendChild(node)

	# Summary
	countByStatusCode = {}
	for code in StatusCode.STATUS_CODES:
		countByStatusCode[code] = 0

	for result in results:
		countByStatusCode[result.statusCode] += 1

	summaryElem = dstDoc.createElement('ResultTotals')
	for code in StatusCode.STATUS_CODES:
		summaryElem.setAttribute(code, "%d" % countByStatusCode[code])
	summaryElem.setAttribute('All', "%d" % len(results))
	batchResultNode.appendChild(summaryElem)

	text = dstDoc.toprettyxml()

	out = codecs.open(outFile, "wb", encoding="utf-8")

	# Write custom headers
	out.write("<?xml version=\"1.0\"?>\n")
	out.write("<?xml-stylesheet href=\"%s\" type=\"text/xsl\"?>\n" % STYLESHEET_FILENAME)

	for line in text.splitlines()[1:]:
		out.write(line)
		out.write("\n")

	out.close()

if __name__ == "__main__":
	if len(sys.argv) != 3:
		print "%s: [test log] [dst file]" % sys.argv[0]
		sys.exit(-1)

	logToXml(sys.argv[1], sys.argv[2])
