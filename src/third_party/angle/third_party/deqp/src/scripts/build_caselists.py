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

from build.common import *
from build.config import *
from build.build import *

import os
import sys
import string
import argparse
import tempfile
import shutil

class Module:
	def __init__ (self, name, dirName, binName):
		self.name		= name
		self.dirName	= dirName
		self.binName	= binName

MODULES = [
	Module("dE-IT",			"internal",								"de-internal-tests"),
	Module("dEQP-EGL",		"egl",									"deqp-egl"),
	Module("dEQP-GLES2",	"gles2",								"deqp-gles2"),
	Module("dEQP-GLES3",	"gles3",								"deqp-gles3"),
	Module("dEQP-GLES31",	"gles31",								"deqp-gles31"),
	Module("dEQP-VK",		"../external/vulkancts/modules/vulkan",	"deqp-vk"),
]

DEFAULT_BUILD_DIR	= os.path.join(tempfile.gettempdir(), "deqp-caselists", "{targetName}-{buildType}")
DEFAULT_TARGET		= "null"

def getModuleByName (name):
	for module in MODULES:
		if module.name == name:
			return module
	else:
		raise Exception("Unknown module %s" % name)

def getBuildConfig (buildPathPtrn, targetName, buildType):
	buildPath = buildPathPtrn.format(
		targetName	= targetName,
		buildType	= buildType)

	return BuildConfig(buildPath, buildType, ["-DDEQP_TARGET=%s" % targetName])

def getModulesPath (buildCfg):
	return os.path.join(buildCfg.getBuildDir(), "modules")

def getBuiltModules (buildCfg):
	modules		= []
	modulesDir	= getModulesPath(buildCfg)

	for module in MODULES:
		fullPath = os.path.join(modulesDir, module.dirName)
		if os.path.exists(fullPath) and os.path.isdir(fullPath):
			modules.append(module)

	return modules

def getCaseListFileName (module, caseListType):
	return "%s-cases.%s" % (module.name, caseListType)

def getCaseListPath (buildCfg, module, caseListType):
	return os.path.join(getModulesPath(buildCfg), module.dirName, getCaseListFileName(module, caseListType))

def genCaseList (buildCfg, generator, module, caseListType):
	workDir = os.path.join(getModulesPath(buildCfg), module.dirName)

	pushWorkingDir(workDir)

	try:
		binPath = generator.getBinaryPath(buildCfg.getBuildType(), os.path.join(".", module.binName))
		execute([binPath, "--deqp-runmode=%s-caselist" % caseListType])
	finally:
		popWorkingDir()

def genAndCopyCaseList (buildCfg, generator, module, dstDir, caseListType):
	caseListFile	= getCaseListFileName(module, caseListType)
	srcPath			= getCaseListPath(buildCfg, module, caseListType)
	dstPath			= os.path.join(dstDir, caseListFile)

	if os.path.exists(srcPath):
		os.remove(srcPath)

	genCaseList(buildCfg, generator, module, caseListType)

	if not os.path.exists(srcPath):
		raise Exception("%s not generated" % srcPath)

	shutil.copyfile(srcPath, dstPath)

def parseArgs ():
	parser = argparse.ArgumentParser(description = "Build test case lists",
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
	parser.add_argument("--case-list-type",
						dest="caseListType",
						default="xml",
						help="Case list type (xml, txt)")
	parser.add_argument("-m",
						"--modules",
						dest="modules",
						help="Comma-separated list of modules to update")
	parser.add_argument("dst",
						help="Destination directory for test case lists")
	return parser.parse_args()

if __name__ == "__main__":
	args = parseArgs()

	generator	= ANY_GENERATOR
	buildCfg	= getBuildConfig(args.buildDir, args.targetName, args.buildType)
	modules		= None

	if args.modules:
		modules = []
		for m in args.modules.split(","):
			modules.append(getModuleByName(m))

	if modules:
		build(buildCfg, generator, [m.binName for m in modules])
	else:
		build(buildCfg, generator)
		modules = getBuiltModules(buildCfg)

	for module in modules:
		print "Generating test case list for %s" % module.name
		genAndCopyCaseList(buildCfg, generator, module, args.dst, args.caseListType)
