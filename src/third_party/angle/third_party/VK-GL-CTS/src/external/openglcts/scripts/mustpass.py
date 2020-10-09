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

import sys
import os
import xml.etree.cElementTree as ElementTree
import xml.dom.minidom as minidom

from build_caselists import Module, getModuleByName, getBuildConfig, genCaseList, getCaseListPath, DEFAULT_BUILD_DIR, DEFAULT_TARGET, GLCTS_BIN_NAME

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

from build.common import *
from build.config import ANY_GENERATOR
from build.build import build
from fnmatch import fnmatch
from copy import copy

GENERATED_FILE_WARNING = """\
/* WARNING: This is auto-generated file. Do not modify, since changes will
 * be lost! Modify the generating script instead.
 */"""

class Project:
	def __init__ (self, name, path, incpath, devicepath, copyright = None):
		self.name		= name
		self.path		= path
		self.incpath	= incpath
		self.devicepath	= devicepath
		self.copyright	= copyright

class Configuration:
	def __init__ (self, name, filters, glconfig = None, rotation = "unspecified", surfacetype = None, surfacewidth = None, surfaceheight = None, baseseed = None, fboconfig = None, required = False, runtime = None, os = "any", skip = "none"):
		self.name				= name
		self.glconfig			= glconfig
		self.rotation			= rotation
		self.surfacetype		= surfacetype
		self.required			= required
		self.surfacewidth		= surfacewidth
		self.surfaceheight		= surfaceheight
		self.baseseed			= baseseed
		self.fboconfig			= fboconfig
		self.filters			= filters
		self.expectedRuntime	= runtime
		self.os					= os
		self.skipPlatform		= skip

class Package:
	def __init__ (self, module, configurations, useforfirsteglconfig = True):
		self.module					= module
		self.useforfirsteglconfig	= useforfirsteglconfig
		self.configurations			= configurations

class Mustpass:
	def __init__ (self, project, version, packages, isCurrent):
		self.project		= project
		self.version		= version
		self.packages		= packages
		self.isCurrent		= isCurrent

class Filter:
	TYPE_INCLUDE = 0
	TYPE_EXCLUDE = 1

	def __init__ (self, type, filename):
		self.type		= type
		self.filename	= filename

def getSrcDir (mustpass):
	return os.path.join(mustpass.project.path, mustpass.version, "src")

def getTmpDir (mustpass):
	return os.path.join(mustpass.project.path, mustpass.version, "tmp")

def getModuleShorthand (module):
	return module.api.lower()

def getCaseListFileName (package, configuration):
	return "%s-%s.txt" % (getModuleShorthand(package.module), configuration.name)

def getDstDir(mustpass):
	return os.path.join(mustpass.project.path, mustpass.version)

def getDstCaseListPath (mustpass, package, configuration):
	return os.path.join(getDstDir(mustpass), getCaseListFileName(package, configuration))

def getCommandLine (config):
	cmdLine = ""

	if config.glconfig != None:
		cmdLine += "--deqp-gl-config-name=%s " % config.glconfig

	if config.rotation != None:
		cmdLine += "--deqp-screen-rotation=%s " % config.rotation

	if config.surfacetype != None:
		cmdLine += "--deqp-surface-type=%s " % config.surfacetype

	if config.surfacewidth != None:
		cmdLine += "--deqp-surface-width=%s " % config.surfacewidth

	if config.surfaceheight != None:
		cmdLine += "--deqp-surface-height=%s " % config.surfaceheight

	if config.baseseed != None:
		cmdLine += "--deqp-base-seed=%s " % config.baseseed

	if config.fboconfig != None:
		cmdLine += "--deqp-gl-config-name=%s --deqp-surface-type=fbo " % config.fboconfig

	cmdLine += "--deqp-watchdog=disable"

	return cmdLine

def readCaseList (filename):
	cases = []
	with open(filename, 'rt') as f:
		for line in f:
			if line[:6] == "TEST: ":
				cases.append(line[6:].strip())
	return cases

def getCaseList (buildCfg, generator, module):
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

	packageElem = ElementTree.SubElement(mustpassElem, "TestPackage", name = mustpass.project.name)

	for package in mustpass.packages:
		for config in package.configurations:
			configElem = ElementTree.SubElement(packageElem, "Configuration",
							caseListFile			= getCaseListFileName(package, config),
							commandLine				= getCommandLine(config),
							name					= config.name,
							os						= str(config.os),
							useForFirstEGLConfig	= str(package.useforfirsteglconfig)
							)

	return mustpassElem

def getIncludeGuardName (headerFile):
	return '_' + os.path.basename(headerFile).upper().replace('.', '_')

def convertToCamelcase(s):
    return ''.join(w.capitalize() or '_' for w in s.split('_'))

def getApiType(apiName):
	if apiName == "GLES2":
		return "glu::ApiType::es(2, 0)"
	if apiName == "GLES3":
		return "glu::ApiType::es(3, 0)"
	if apiName == "GLES31":
		return "glu::ApiType::es(3, 1)"
	if apiName == "GLES32":
		return "glu::ApiType::es(3, 2)"
	if apiName == "GL46":
		return "glu::ApiType::core(4, 6)"
	if apiName == "GL45":
		return "glu::ApiType::core(4, 5)"
	if apiName == "GL44":
		return "glu::ApiType::core(4, 4)"
	if apiName == "GL43":
		return "glu::ApiType::core(4, 3)"
	if apiName == "GL42":
		return "glu::ApiType::core(4, 2)"
	if apiName == "GL41":
		return "glu::ApiType::core(4, 1)"
	if apiName == "GL40":
		return "glu::ApiType::core(4, 0)"
	if apiName == "GL33":
		return "glu::ApiType::core(3, 3)"
	if apiName == "GL32":
		return "glu::ApiType::core(3, 2)"
	if apiName == "GL31":
		return "glu::ApiType::core(3, 1)"
	if apiName == "GL30":
		return "glu::ApiType::core(3, 0)"
	if apiName == "EGL":
		return "glu::ApiType()"

	raise Exception("Unknown API %s" % apiName)
	return "Unknown"

def getConfigName(cfgName):
	if cfgName == None:
		return "DE_NULL"
	else:
		return '"' + cfgName + '"'

def getIntBaseSeed(baseSeed):
	if baseSeed == None:
		return "-1"
	else:
		return baseSeed

def genSpecCPPIncludeFile (specFilename, mustpass):
	fileBody = ""

	includeGuard = getIncludeGuardName(specFilename)
	fileBody += "#ifndef %s\n" % includeGuard
	fileBody += "#define %s\n" % includeGuard
	fileBody += mustpass.project.copyright
	fileBody += "\n\n"
	fileBody += GENERATED_FILE_WARNING
	fileBody += "\n\n"
	fileBody += 'const char* mustpassDir = "' + mustpass.project.devicepath + '/' + mustpass.version + '/";\n\n'

	gtf_wrapper_open = "#if defined(DEQP_GTF_AVAILABLE)\n"
	gtf_wrapper_close = "#endif // defined(DEQP_GTF_AVAILABLE)\n"
	android_wrapper_open = "#if DE_OS == DE_OS_ANDROID\n"
	android_wrapper_close = "#endif // DE_OS == DE_OS_ANDROID\n"
	skip_x11_wrapper_open = "#ifndef DEQP_SUPPORT_X11\n"
	skip_x11_wrapper_close = "#endif // DEQP_SUPPORT_X11\n"
	TABLE_ELEM_PATTERN	= "{apiType} {configName} {glConfigName} {screenRotation} {baseSeed} {fboConfig} {surfaceWidth} {surfaceHeight}"

	emitOtherCfgTbl = False
	firstCfgDecl = "static const RunParams %s_first_cfg[] = " % mustpass.project.name.lower().replace(' ','_')
	firstCfgTbl = "{\n"

	otherCfgDecl = "static const RunParams %s_other_cfg[] = " % mustpass.project.name.lower().replace(' ','_')
	otherCfgTbl = "{\n"

	for package in mustpass.packages:
		for config in package.configurations:
			pApiType = getApiType(package.module.api) + ','
			pConfigName = '"' + config.name + '",'
			pGLConfig = getConfigName(config.glconfig) + ','
			pRotation = '"' + config.rotation + '",'
			pSeed =  getIntBaseSeed(config.baseseed) + ','
			pFBOConfig = getConfigName(config.fboconfig) + ','
			pWidth = config.surfacewidth + ','
			pHeight = config.surfaceheight
			elemFinal = ""
			elemContent = TABLE_ELEM_PATTERN.format(apiType = pApiType, configName = pConfigName, glConfigName = pGLConfig, screenRotation = pRotation, baseSeed = pSeed, fboConfig = pFBOConfig, surfaceWidth = pWidth, surfaceHeight = pHeight)
			elem = "\t{ " + elemContent + " },\n"
			if package.module.name[:3] == "GTF":
				elemFinal += gtf_wrapper_open

			if config.os == "android":
				elemFinal += android_wrapper_open

			if config.skipPlatform == "x11":
				elemFinal += skip_x11_wrapper_open

			elemFinal += elem

			if config.skipPlatform == "x11":
				elemFinal += skip_x11_wrapper_close

			if config.os == "android":
				elemFinal += android_wrapper_close

			if package.module.name[:3] == "GTF":
				elemFinal += gtf_wrapper_close

			if package.useforfirsteglconfig == True:
				firstCfgTbl += elemFinal
			else:
				otherCfgTbl += elemFinal
				emitOtherCfgTbl = True

	firstCfgTbl += "};\n"
	otherCfgTbl += "};\n"

	fileBody += firstCfgDecl
	fileBody += firstCfgTbl

	if emitOtherCfgTbl == True:
		fileBody += "\n"
		fileBody += otherCfgDecl
		fileBody += otherCfgTbl

	fileBody += "\n"
	fileBody += "#endif // %s\n" % includeGuard
	return fileBody


def genSpecCPPIncludes (mustpassLists):
	for mustpass in mustpassLists:
		if mustpass.isCurrent == True:
			specFilename	= os.path.join(mustpass.project.incpath, "glc%s.hpp" % convertToCamelcase(mustpass.project.name.lower().replace(' ','_')))
			hpp = genSpecCPPIncludeFile(specFilename, mustpass)

			print("  Writing spec: " + specFilename)
			writeFile(specFilename, hpp)
			print("Done!")

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

	print("Done!")

def genMustpassLists (mustpassLists, generator, buildCfg):
	moduleCaseLists = {}

	# Getting case lists involves invoking build, so we want to cache the results
	build(buildCfg, generator, [GLCTS_BIN_NAME])
	genCaseList(buildCfg, generator, "txt")
	for mustpass in mustpassLists:
		for package in mustpass.packages:
			if not package.module in moduleCaseLists:
				moduleCaseLists[package.module] = getCaseList(buildCfg, generator, package.module)

	for mustpass in mustpassLists:
		genMustpass(mustpass, moduleCaseLists)


	genSpecCPPIncludes(mustpassLists)
