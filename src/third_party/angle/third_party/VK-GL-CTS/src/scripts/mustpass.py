# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2016 The Android Open Source Project
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

from build.common import *
from build.config import ANY_GENERATOR
from build.build import build
from build_caselists import Module, getModuleByName, getBuildConfig, genCaseList, getCaseListPath, DEFAULT_BUILD_DIR, DEFAULT_TARGET
from fnmatch import fnmatch
from copy import copy

import argparse
import xml.etree.cElementTree as ElementTree
import xml.dom.minidom as minidom

APK_NAME		= "com.drawelements.deqp.apk"

GENERATED_FILE_WARNING = """
     This file has been automatically generated. Edit with caution.
     """

class Project:
	def __init__ (self, path, copyright = None):
		self.path		= path
		self.copyright	= copyright

class Configuration:
	def __init__ (self, name, filters, glconfig = None, rotation = None, surfacetype = None, required = False, runtime = None, runByDefault = True):
		self.name				= name
		self.glconfig			= glconfig
		self.rotation			= rotation
		self.surfacetype		= surfacetype
		self.required			= required
		self.filters			= filters
		self.expectedRuntime	= runtime
		self.runByDefault		= runByDefault

class Package:
	def __init__ (self, module, configurations):
		self.module			= module
		self.configurations	= configurations

class Mustpass:
	def __init__ (self, project, version, packages):
		self.project	= project
		self.version	= version
		self.packages	= packages

class Filter:
	TYPE_INCLUDE = 0
	TYPE_EXCLUDE = 1

	def __init__ (self, type, filename):
		self.type		= type
		self.filename	= filename

class TestRoot:
	def __init__ (self):
		self.children	= []

class TestGroup:
	def __init__ (self, name):
		self.name		= name
		self.children	= []

class TestCase:
	def __init__ (self, name):
		self.name			= name
		self.configurations	= []

class GLESVersion:
	def __init__(self, major, minor):
		self.major = major
		self.minor = minor

	def encode (self):
		return (self.major << 16) | (self.minor)

def getModuleGLESVersion (module):
	versions = {
		'dEQP-EGL':		GLESVersion(2,0),
		'dEQP-GLES2':	GLESVersion(2,0),
		'dEQP-GLES3':	GLESVersion(3,0),
		'dEQP-GLES31':	GLESVersion(3,1)
	}
	return versions[module.name] if module.name in versions else None

def getSrcDir (mustpass):
	return os.path.join(mustpass.project.path, mustpass.version, "src")

def getTmpDir (mustpass):
	return os.path.join(mustpass.project.path, mustpass.version, "tmp")

def getModuleShorthand (module):
	assert module.name[:5] == "dEQP-"
	return module.name[5:].lower()

def getCaseListFileName (package, configuration):
	return "%s-%s.txt" % (getModuleShorthand(package.module), configuration.name)

def getDstCaseListPath (mustpass, package, configuration):
	return os.path.join(mustpass.project.path, mustpass.version, getCaseListFileName(package, configuration))

def getCTSPackageName (package):
	return "com.drawelements.deqp." + getModuleShorthand(package.module)

def getCommandLine (config):
	cmdLine = ""

	if config.glconfig != None:
		cmdLine += "--deqp-gl-config-name=%s " % config.glconfig

	if config.rotation != None:
		cmdLine += "--deqp-screen-rotation=%s " % config.rotation

	if config.surfacetype != None:
		cmdLine += "--deqp-surface-type=%s " % config.surfacetype

	cmdLine += "--deqp-watchdog=enable"

	return cmdLine

def readCaseList (filename):
	cases = []
	with open(filename, 'rt') as f:
		for line in f:
			if line[:6] == "TEST: ":
				cases.append(line[6:].strip())
	return cases

def getCaseList (buildCfg, generator, module):
	build(buildCfg, generator, [module.binName])
	genCaseList(buildCfg, generator, module, "txt")
	return readCaseList(getCaseListPath(buildCfg, module, "txt"))

def readPatternList (filename):
	ptrns = []
	with open(filename, 'rt') as f:
		for line in f:
			line = line.strip()
			if len(line) > 0 and line[0] != '#':
				ptrns.append(line)
	return ptrns

def applyPatterns (caseList, patterns, filename, op):
	matched			= set()
	errors			= []
	curList			= copy(caseList)
	trivialPtrns	= [p for p in patterns if p.find('*') < 0]
	regularPtrns	= [p for p in patterns if p.find('*') >= 0]

	# Apply trivial (just case paths)
	allCasesSet		= set(caseList)
	for path in trivialPtrns:
		if path in allCasesSet:
			if path in matched:
				errors.append((path, "Same case specified more than once"))
			matched.add(path)
		else:
			errors.append((path, "Test case not found"))

	curList = [c for c in curList if c not in matched]

	for pattern in regularPtrns:
		matchedThisPtrn = set()

		for case in curList:
			if fnmatch(case, pattern):
				matchedThisPtrn.add(case)

		if len(matchedThisPtrn) == 0:
			errors.append((pattern, "Pattern didn't match any cases"))

		matched	= matched | matchedThisPtrn
		curList = [c for c in curList if c not in matched]

	for pattern, reason in errors:
		print("ERROR: %s: %s" % (reason, pattern))

	if len(errors) > 0:
		die("Found %s invalid patterns while processing file %s" % (len(errors), filename))

	return [c for c in caseList if op(c in matched)]

def applyInclude (caseList, patterns, filename):
	return applyPatterns(caseList, patterns, filename, lambda b: b)

def applyExclude (caseList, patterns, filename):
	return applyPatterns(caseList, patterns, filename, lambda b: not b)

def readPatternLists (mustpass):
	lists = {}
	for package in mustpass.packages:
		for cfg in package.configurations:
			for filter in cfg.filters:
				if not filter.filename in lists:
					lists[filter.filename] = readPatternList(os.path.join(getSrcDir(mustpass), filter.filename))
	return lists

def applyFilters (caseList, patternLists, filters):
	res = copy(caseList)
	for filter in filters:
		ptrnList = patternLists[filter.filename]
		if filter.type == Filter.TYPE_INCLUDE:
			res = applyInclude(res, ptrnList, filter.filename)
		else:
			assert filter.type == Filter.TYPE_EXCLUDE
			res = applyExclude(res, ptrnList, filter.filename)
	return res

def appendToHierarchy (root, casePath):
	def findChild (node, name):
		for child in node.children:
			if child.name == name:
				return child
		return None

	curNode		= root
	components	= casePath.split('.')

	for component in components[:-1]:
		nextNode = findChild(curNode, component)
		if not nextNode:
			nextNode = TestGroup(component)
			curNode.children.append(nextNode)
		curNode = nextNode

	if not findChild(curNode, components[-1]):
		curNode.children.append(TestCase(components[-1]))

def buildTestHierachy (caseList):
	root = TestRoot()
	for case in caseList:
		appendToHierarchy(root, case)
	return root

def buildTestCaseMap (root):
	caseMap = {}

	def recursiveBuild (curNode, prefix):
		curPath = prefix + curNode.name
		if isinstance(curNode, TestCase):
			caseMap[curPath] = curNode
		else:
			for child in curNode.children:
				recursiveBuild(child, curPath + '.')

	for child in root.children:
		recursiveBuild(child, '')

	return caseMap

def include (filename):
	return Filter(Filter.TYPE_INCLUDE, filename)

def exclude (filename):
	return Filter(Filter.TYPE_EXCLUDE, filename)

def insertXMLHeaders (mustpass, doc):
	if mustpass.project.copyright != None:
		doc.insert(0, ElementTree.Comment(mustpass.project.copyright))
	doc.insert(1, ElementTree.Comment(GENERATED_FILE_WARNING))

def prettifyXML (doc):
	uglyString	= ElementTree.tostring(doc, 'utf-8')
	reparsed	= minidom.parseString(uglyString)
	return reparsed.toprettyxml(indent='\t', encoding='utf-8')

def genSpecXML (mustpass):
	mustpassElem = ElementTree.Element("Mustpass", version = mustpass.version)
	insertXMLHeaders(mustpass, mustpassElem)

	for package in mustpass.packages:
		packageElem = ElementTree.SubElement(mustpassElem, "TestPackage", name = package.module.name)

		for config in package.configurations:
			configElem = ElementTree.SubElement(packageElem, "Configuration",
												caseListFile	= getCaseListFileName(package, config),
												commandLine		= getCommandLine(config),
												name			= config.name)

	return mustpassElem

def addOptionElement (parent, optionName, optionValue):
	ElementTree.SubElement(parent, "option", name=optionName, value=optionValue)

def genAndroidTestXml (mustpass):
	RUNNER_CLASS = "com.drawelements.deqp.runner.DeqpTestRunner"
	configElement = ElementTree.Element("configuration")

	# have the deqp package installed on the device for us
	preparerElement = ElementTree.SubElement(configElement, "target_preparer")
	preparerElement.set("class", "com.android.tradefed.targetprep.suite.SuiteApkInstaller")
	addOptionElement(preparerElement, "cleanup-apks", "true")
	addOptionElement(preparerElement, "test-file-name", "com.drawelements.deqp.apk")

	# add in metadata option for component name
	ElementTree.SubElement(configElement, "option", name="test-suite-tag", value="cts")
	ElementTree.SubElement(configElement, "option", key="component", name="config-descriptor:metadata", value="deqp")
	ElementTree.SubElement(configElement, "option", key="parameter", name="config-descriptor:metadata", value="not_instant_app")
	ElementTree.SubElement(configElement, "option", key="parameter", name="config-descriptor:metadata", value="multi_abi")
	ElementTree.SubElement(configElement, "option", key="parameter", name="config-descriptor:metadata", value="secondary_user")
	controllerElement = ElementTree.SubElement(configElement, "object")
	controllerElement.set("class", "com.android.tradefed.testtype.suite.module.TestFailureModuleController")
	controllerElement.set("type", "module_controller")
	addOptionElement(controllerElement, "screenshot-on-failure", "false")

	for package in mustpass.packages:
		for config in package.configurations:
			if not config.runByDefault:
				continue

			testElement = ElementTree.SubElement(configElement, "test")
			testElement.set("class", RUNNER_CLASS)
			addOptionElement(testElement, "deqp-package", package.module.name)
			addOptionElement(testElement, "deqp-caselist-file", getCaseListFileName(package,config))
			# \todo [2015-10-16 kalle]: Replace with just command line? - requires simplifications in the runner/tests as well.
			if config.glconfig != None:
				addOptionElement(testElement, "deqp-gl-config-name", config.glconfig)

			if config.surfacetype != None:
				addOptionElement(testElement, "deqp-surface-type", config.surfacetype)

			if config.rotation != None:
				addOptionElement(testElement, "deqp-screen-rotation", config.rotation)

			if config.expectedRuntime != None:
				addOptionElement(testElement, "runtime-hint", config.expectedRuntime)

			if config.required:
				addOptionElement(testElement, "deqp-config-required", "true")

	insertXMLHeaders(mustpass, configElement)

	return configElement

def genMustpass (mustpass, moduleCaseLists):
	print("Generating mustpass '%s'" % mustpass.version)

	patternLists = readPatternLists(mustpass)

	for package in mustpass.packages:
		allCasesInPkg	= moduleCaseLists[package.module]

		for config in package.configurations:
			filtered	= applyFilters(allCasesInPkg, patternLists, config.filters)
			dstFile		= getDstCaseListPath(mustpass, package, config)

			print("  Writing deqp caselist: " + dstFile)
			writeFile(dstFile, "\n".join(filtered) + "\n")

	specXML			= genSpecXML(mustpass)
	specFilename	= os.path.join(mustpass.project.path, mustpass.version, "mustpass.xml")

	print("  Writing spec: " + specFilename)
	writeFile(specFilename, prettifyXML(specXML).decode())

	# TODO: Which is the best selector mechanism?
	if (mustpass.version == "master"):
		androidTestXML		= genAndroidTestXml(mustpass)
		androidTestFilename	= os.path.join(mustpass.project.path, "AndroidTest.xml")

		print("  Writing AndroidTest.xml: " + androidTestFilename)
		writeFile(androidTestFilename, prettifyXML(androidTestXML).decode())

	print("Done!")

def genMustpassLists (mustpassLists, generator, buildCfg):
	moduleCaseLists = {}

	# Getting case lists involves invoking build, so we want to cache the results
	for mustpass in mustpassLists:
		for package in mustpass.packages:
			if not package.module in moduleCaseLists:
				moduleCaseLists[package.module] = getCaseList(buildCfg, generator, package.module)

	for mustpass in mustpassLists:
		genMustpass(mustpass, moduleCaseLists)

def parseCmdLineArgs ():
	parser = argparse.ArgumentParser(description = "Build Android CTS mustpass",
									 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
	parser.add_argument("-b",
						"--build-dir",
						dest="buildDir",
						default=DEFAULT_BUILD_DIR,
						help="Temporary build directory")
	parser.add_argument("-t",
						"--build-type",
						dest="buildType",
						default="Debug",
						help="Build type")
	parser.add_argument("-c",
						"--deqp-target",
						dest="targetName",
						default=DEFAULT_TARGET,
						help="dEQP build target")
	return parser.parse_args()

def parseBuildConfigFromCmdLineArgs ():
	args = parseCmdLineArgs()
	return getBuildConfig(args.buildDir, args.targetName, args.buildType)
