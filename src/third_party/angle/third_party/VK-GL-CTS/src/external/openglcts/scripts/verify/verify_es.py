# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Khronos OpenGL CTS
# ------------------
#
# Copyright (c) 2017 The Khronos Group Inc.
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
import re
import subprocess

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
sys.path.append(os.path.join(ROOT_DIR, "scripts", "verify"))
sys.path.append(os.path.join(ROOT_DIR, "scripts", "build"))
sys.path.append(os.path.join(ROOT_DIR, "scripts", "log"))

from package import getPackageDescription
from verify import *
from message import *
from common import *
from log_parser import *
from summary import *

def getConfigCaseName (type):
	configs = { "es32" : ["CTS-Configs.es32", "CTS-Configs.es31", "CTS-Configs.es3", "CTS-Configs.es2"],
				"es31" : ["CTS-Configs.es31", "CTS-Configs.es3", "CTS-Configs.es2"],
				"es3"  : ["CTS-Configs.es3", "CTS-Configs.es2"],
				"es2"  : ["CTS-Configs.es2"]}
	return configs[type]

def retrieveReportedConfigs(caseName, log):
	doc				= xml.dom.minidom.parseString(log)
	sectionItems	= doc.getElementsByTagName('Section')
	sectionName		= None

	configs = []
	for sectionItem in sectionItems:
		sectionName	= sectionItem.getAttributeNode('Name').nodeValue
		if sectionName == "Configs":
			assert len(configs) == 0
			textItems = sectionItem.getElementsByTagName('Text')
			for textItem in textItems:
				configs.append(getNodeText(textItem))
	res = {caseName : configs}
	return res

def compareConfigs(filename, baseConfigs, cmpConfigs):
	messages = []
	assert len(list(baseConfigs.keys())) == 1
	assert len(list(cmpConfigs.keys())) == 1
	baseKey = list(baseConfigs.keys())[0]
	cmpKey = list(cmpConfigs.keys())[0]

	if cmp(baseConfigs[baseKey], cmpConfigs[cmpKey]) != 0:
		messages.append(error(filename, "Confomant configs reported for %s and %s do not match" % (baseKey,cmpKey)))

	return messages

def numGitLogStatusFile (releaseTag):
	KC_CTS_RELEASE = ["opengl-es-cts-3\.2\.[2-3]\.[0-9]*", "opengl-cts-4\.6\.[0-9]*\.[0-9]*"]
	for r in KC_CTS_RELEASE:
		if re.match(r, releaseTag):
			return 2

	return 1

def verifyConfigFile (filename, type):
	messages  = []
	caseNames = getConfigCaseName(type)

	parser		= BatchResultParser()
	results		= parser.parseFile(filename)
	baseConfigs	= None

	for caseName in caseNames:
		caseResult	= None
		print("Verifying %s in %s" % (caseName, filename))
		for result in results:
			if result.name == caseName:
				caseResult = result
				break;
		if caseResult == None:
			messages.append(error(filename, "Missing %s" % caseName))
		else:
			configs = retrieveReportedConfigs(caseName, result.log)
			if baseConfigs == None:
				baseConfigs = configs
			else:
				messages += compareConfigs(filename, baseConfigs, configs)
			if not caseResult.statusCode in ALLOWED_STATUS_CODES:
				messages.append(error(filename, "%s failed" % caseResult))

	return messages

def verifyMustpassCases(package, mustpassCases, type):
	messages = []
	apiToTest = { "es32" : ["gles32", "gles31", "gles3", "gles2", "egl"],
				"es31" : ["gles31", "gles3", "gles2", "egl"],
				"es3"  : ["gles3", "gles2", "egl"],
				"es2"  : ["gles2", "egl"]}

	for mustpass in mustpassCases:
		mustpassXML = os.path.join(mustpass, "mustpass.xml")
		doc = xml.dom.minidom.parse(mustpassXML)
		testConfigs = doc.getElementsByTagName("Configuration")
		# check that all configs that must be tested are present
		for testConfig in testConfigs:
			caseListFile = testConfig.getAttributeNode("caseListFile").nodeValue
			# identify APIs that must be tested for the given type
			apis = apiToTest[type]
			# identify API tested by the current config
			configAPI = caseListFile.split('-')[0]
			if configAPI in apis:
				# the API in this config is expected to be tested
				mustTest = True
			else:
				mustTest = False
			pattern = "config-" + os.path.splitext(caseListFile)[0] + "-cfg-[0-9]*"+"-run-[0-9]*"
			cmdLine = testConfig.getAttributeNode("commandLine").nodeValue
			cfgItems = {'height':None, 'width':None, 'seed':None, 'rotation':None}
			for arg in cmdLine.split():
				val = arg.split('=')[1]
				if "deqp-surface-height" in arg:
					cfgItems['height'] = val
				elif "deqp-surface-width" in arg:
					cfgItems['width'] = val
				elif "deqp-base-seed" in arg:
					cfgItems['seed'] = val
				elif "deqp-screen-rotation" in arg:
					cfgItems['rotation'] = val
			pattern += "-width-" + cfgItems['width'] + "-height-" + cfgItems['height']
			if cfgItems['seed'] != None:
				pattern += "-seed-" + cfgItems['seed']
			pattern += ".qpa"
			p = re.compile(pattern)
			matches = [m for l in mustpassCases[mustpass] for m in (p.match(l),) if m]
			if len(matches) == 0 and mustTest == True:
					conformOs = testConfig.getAttributeNode("os").nodeValue
					txt = "Configuration %s %s was not executed" % (caseListFile, cmdLine)
					if conformOs == "any" or (package.conformOs != None and conformOs in package.conformOs.lower()):
						msg = error(mustpassXML, txt)
					else:
						msg = warning(mustpassXML, txt)
					messages.append(msg)
			elif len(matches) != 0 and mustTest == False:
				messages.append(error(mustpassXML, "Configuration %s %s was not expected to be tested but present in cts-run-summary.xml" % (caseListFile, cmdLine)))

	return messages

def verifyTestLogs (package):
	messages = []

	try:
		execute(['git', 'checkout', '--quiet', package.conformVersion])
	except Exception as e:
		print(str(e))
		print("Failed to checkout release tag %s." % package.conformVersion)
		return messages

	messages = []
	summary	= parseRunSummary(os.path.join(package.basePath, package.summary))
	mustpassDirs = []

	# Check Conformant attribute
	if not summary.isConformant:
		messages.append(error(package.summary, "Runner reported conformance failure (Conformant=\"False\" in <Summary>)"))

	# Verify config list
	messages += verifyConfigFile(os.path.join(package.basePath, summary.configLogFilename), summary.type)

	mustpassCases = {}
	# Verify that all run files passed
	for runLog in summary.runLogAndCaselist:
		sys.stdout.write("Verifying %s -" % runLog)
		sys.stdout.flush()

		mustpassFile = os.path.join(ROOT_DIR, "external", "openglcts", summary.runLogAndCaselist[runLog])
		key = os.path.dirname(mustpassFile)
		if key in mustpassCases:
			mpCase = mustpassCases[key]
		else:
			mpCase = []
		mpCase.append(runLog)
		mustpassCases[os.path.dirname(mustpassFile)] = mpCase
		mustpass = readMustpass(mustpassFile)
		messages_log = verifyTestLog(os.path.join(package.basePath, runLog), mustpass)

		errors	= [m for m in messages_log if m.type == ValidationMessage.TYPE_ERROR]
		warnings	= [m for m in messages_log if m.type == ValidationMessage.TYPE_WARNING]
		if len(errors) > 0:
			sys.stdout.write(" finished with ERRRORS")
		if len(warnings) > 0:
			sys.stdout.write(" finished with WARNINGS")
		if len(errors) == 0 and len(warnings) == 0:
			sys.stdout.write(" OK")
		sys.stdout.write("\n")
		sys.stdout.flush()

		messages += messages_log

	messages += verifyMustpassCases(package, mustpassCases, summary.type)

	return messages

def verifyGitStatusFiles (package):
	messages = []

	errorDict = {1 : 'one git status file', 2 : 'two git status files'}
	numFiles = numGitLogStatusFile(package.conformVersion)

	if len(package.gitStatus) != numFiles:
		messages.append(error(package.basePath, "Exactly %s must be present, found %s" % (errorDict[numFiles], len(package.gitStatus))))

	messages += verifyGitStatus(package)

	return messages

def isGitLogFileEmpty (package, modulePath, gitLog):
	logPath	= os.path.join(package.basePath, gitLog)
	log		= readFile(logPath)

	args = ['git', 'log', '-1', package.conformVersion]
	process = subprocess.Popen(args, cwd=modulePath, stdout=subprocess.PIPE)
	output = process.communicate()[0]
	if process.returncode != 0:
		raise Exception("Failed to execute '%s', got %d" % (str(args), process.returncode))

	return log == output

def verifyGitLogFile (package):
	messages = []

	if len(package.gitLog) > 0:
		for log, path in package.gitLog:
			try:
				isEmpty = isGitLogFileEmpty(package, path, log)
			except Exception as e:
				print(str(e))
				isEmpty = False

			if not isEmpty:
				messages.append(warning(os.path.join(package.basePath, log), "Log is not empty"))
	else:
		messages.append(error(package.basePath, "Missing git log files"))

	return messages

def verifyPatchFiles (package):
	messages	= []
	hasPatches	= len(package.patches)
	logEmpty	= True
	for log, path in package.gitLog:
		logEmpty &= isGitLogFileEmpty(package, path, log)

	if hasPatches and logEmpty:
		messages.append(error(package.basePath, "Package includes patches but log is empty"))
	elif not hasPatches and not logEmpty:
		messages.append(error(package.basePath, "Test log is not empty but package doesn't contain patches"))

	return messages

def verifyGitLogFiles (package):
	messages = []

	errorDict = {1 : 'one git log file', 2 : 'two git log files'}
	numFiles = numGitLogStatusFile(package.conformVersion)

	if len(package.gitLog) != numFiles:
		messages.append(error(package.basePath, "Exactly %s must be present, found %s" % (errorDict[numFiles], len(package.gitLog))))

	for i, gitLog in enumerate(package.gitLog):
		if "kc-cts" in gitLog[0] and numFiles > 1:
			package.gitLog[i] = gitLog[:1] + ("external/kc-cts/src",) + gitLog[2:]

	messages += verifyGitLogFile(package)

	return messages

def verifyPackage (package):
	messages = []

	messages += verifyStatement(package)
	messages += verifyGitStatusFiles(package)
	messages += verifyGitLogFiles(package)
	messages += verifyPatchFiles(package)

	for item in package.otherItems:
		messages.append(warning(os.path.join(package.basePath, item), "Unknown file"))

	return messages

def verifyESSubmission(argv):
	if len(argv) != 2:
		print("%s: [extracted submission package directory]" % sys.argv[0])
		sys.exit(-1)
	try:
		execute(['git', 'ls-remote', 'origin', '--quiet'])
	except Exception as e:
		print(str(e))
		print("This script must be executed inside VK-GL-CTS directory.")
		sys.exit(-1)

	packagePath		=  os.path.normpath(sys.argv[1])
	package			=  getPackageDescription(packagePath)
	messages		=  verifyPackage(package)
	messages		+= verifyTestLogs(package)

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

if __name__ == "__main__":
	verifyESSubmission(sys.argv)
