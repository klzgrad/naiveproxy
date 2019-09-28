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

import os
import argparse
import tempfile
import sys

from build.common import *
from build.build import *

pythonExecutable = sys.executable or "python"

class Environment:
	def __init__ (self, srcDir, tmpDir):
		self.srcDir	= srcDir
		self.tmpDir	= tmpDir

class BuildTestStep:
	def getName (self):
		return "<unknown>"

	def isAvailable (self, env):
		return True

	def run (self, env):
		raise Exception("Not implemented")

class RunScript(BuildTestStep):
	def __init__ (self, scriptPath, getExtraArgs = None):
		self.scriptPath		= scriptPath
		self.getExtraArgs	= getExtraArgs

	def getName (self):
		return self.scriptPath

	def run (self, env):
		args = [pythonExecutable, os.path.join(env.srcDir, self.scriptPath)]

		if self.getExtraArgs != None:
			args += self.getExtraArgs(env)

		execute(args)

def makeCflagsArgs (cflags):
	cflagsStr = " ".join(cflags)
	return ["-DCMAKE_C_FLAGS=%s" % cflagsStr, "-DCMAKE_CXX_FLAGS=%s" % cflagsStr]

def makeBuildArgs (target, cc, cpp, cflags):
	return ["-DDEQP_TARGET=%s" % target, "-DCMAKE_C_COMPILER=%s" % cc, "-DCMAKE_CXX_COMPILER=%s" % cpp] + makeCflagsArgs(cflags)

class BuildConfigGen:
	def isAvailable (self, env):
		return True

class UnixConfig(BuildConfigGen):
	def __init__ (self, target, buildType, cc, cpp, cflags):
		self.target		= target
		self.buildType	= buildType
		self.cc			= cc
		self.cpp		= cpp
		self.cflags		= cflags

	def isAvailable (self, env):
		return which(self.cc) != None and which(self.cpp) != None

	def getBuildConfig (self, env, buildDir):
		args = makeBuildArgs(self.target, self.cc, self.cpp, self.cflags)
		return BuildConfig(buildDir, self.buildType, args, env.srcDir)

class VSConfig(BuildConfigGen):
	def __init__ (self, buildType):
		self.buildType = buildType

	def getBuildConfig (self, env, buildDir):
		args = ["-DCMAKE_C_FLAGS=/WX -DCMAKE_CXX_FLAGS=/WX"]
		return BuildConfig(buildDir, self.buildType, args, env.srcDir)

class Build(BuildTestStep):
	def __init__ (self, buildDir, configGen, generator):
		self.buildDir	= buildDir
		self.configGen	= configGen
		self.generator	= generator

	def getName (self):
		return self.buildDir

	def isAvailable (self, env):
		return self.configGen.isAvailable(env) and self.generator != None and self.generator.isAvailable()

	def run (self, env):
		# specialize config for env
		buildDir	= os.path.join(env.tmpDir, self.buildDir)
		curConfig	= self.configGen.getBuildConfig(env, buildDir)

		build(curConfig, self.generator)

class CheckSrcChanges(BuildTestStep):
	def getName (self):
		return "check for changes"

	def run (self, env):
		pushWorkingDir(env.srcDir)
		execute(["git", "diff", "--exit-code"])
		popWorkingDir()

def getClangVersion ():
	knownVersions = ["4.0", "3.9", "3.8", "3.7", "3.6", "3.5"]
	for version in knownVersions:
		if which("clang-" + version) != None:
			return "-" + version
	return ""

def runSteps (steps):
	for step in steps:
		if step.isAvailable(env):
			print("Run: %s" % step.getName())
			step.run(env)
		else:
			print("Skip: %s" % step.getName())

COMMON_CFLAGS		= ["-Werror", "-Wno-error=unused-function"]
COMMON_GCC_CFLAGS	= COMMON_CFLAGS + ["-Wno-implicit-fallthrough"]
COMMON_CLANG_CFLAGS	= COMMON_CFLAGS + ["-Wno-error=unused-command-line-argument"]
GCC_32BIT_CFLAGS	= COMMON_GCC_CFLAGS + ["-m32"]
CLANG_32BIT_CFLAGS	= COMMON_CLANG_CFLAGS + ["-m32"]
GCC_64BIT_CFLAGS	= COMMON_GCC_CFLAGS + ["-m64"]
CLANG_64BIT_CFLAGS	= COMMON_CLANG_CFLAGS + ["-m64"]
CLANG_VERSION		= getClangVersion()

# Always ran before any receipe
PREREQUISITES		= [
	RunScript(os.path.join("external", "fetch_sources.py"))
]

# Always ran after any receipe
POST_CHECKS			= [
	CheckSrcChanges()
]

BUILD_TARGETS		= [
	Build("clang-64-debug",
		  UnixConfig("null",
					 "Debug",
					 "clang" + CLANG_VERSION,
					 "clang++" + CLANG_VERSION,
					 CLANG_64BIT_CFLAGS),
		  ANY_UNIX_GENERATOR),
	Build("gcc-32-debug",
		  UnixConfig("null",
					 "Debug",
					 "gcc",
					 "g++",
					 GCC_32BIT_CFLAGS),
		  ANY_UNIX_GENERATOR),
	Build("gcc-64-release",
		  UnixConfig("null",
					 "Release",
					 "gcc",
					 "g++",
					 GCC_64BIT_CFLAGS),
		  ANY_UNIX_GENERATOR),
	Build("vs-64-debug",
		  VSConfig("Debug"),
		  ANY_VS_X64_GENERATOR),
]

EARLY_SPECIAL_RECIPES	= [
	('gen-inl-files', [
			RunScript(os.path.join("scripts", "gen_egl.py")),
			RunScript(os.path.join("scripts", "opengl", "gen_all.py")),
			RunScript(os.path.join("external", "vulkancts", "scripts", "gen_framework.py")),
			RunScript(os.path.join("external", "vulkancts", "scripts", "gen_framework_c.py")),
			RunScript(os.path.join("scripts", "gen_android_mk.py")),
		]),
	('gen-ext-deps', [
			RunScript(os.path.join("external", "vulkancts", "scripts", "gen_ext_deps.py"))
		]),
]

LATE_SPECIAL_RECIPES	= [
	('android-mustpass', [
			RunScript(os.path.join("scripts", "build_android_mustpass.py"),
					  lambda env: ["--build-dir", os.path.join(env.tmpDir, "android-mustpass")]),
		]),
	('vulkan-mustpass', [
			RunScript(os.path.join("external", "vulkancts", "scripts", "build_mustpass.py"),
					  lambda env: ["--build-dir", os.path.join(env.tmpDir, "vulkan-mustpass")]),
		]),
	('spirv-binaries', [
			RunScript(os.path.join("external", "vulkancts", "scripts", "build_spirv_binaries.py"),
					  lambda env: ["--build-dir", os.path.join(env.tmpDir, "spirv-binaries"),
									"--dst-path", os.path.join(env.tmpDir, "spirv-binaries")]),
		]),
	('check-all', [
			RunScript(os.path.join("scripts", "src_util", "check_all.py")),
		])
]

def getBuildRecipes ():
	return [(b.getName(), [b]) for b in BUILD_TARGETS]

def getAllRecipe (recipes):
	allSteps = []
	for name, steps in recipes:
		allSteps += steps
	return ("all", allSteps)

def getRecipes ():
	recipes = EARLY_SPECIAL_RECIPES + getBuildRecipes() + LATE_SPECIAL_RECIPES
	return recipes

def getRecipe (recipes, recipeName):
	for curName, steps in recipes:
		if curName == recipeName:
			return (curName, steps)
	return None

RECIPES			= getRecipes()

def parseArgs ():
	parser = argparse.ArgumentParser(description = "Build and test source",
									 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
	parser.add_argument("-s",
						"--src-dir",
						dest="srcDir",
						default=DEQP_DIR,
						help="Source directory")
	parser.add_argument("-t",
						"--tmp-dir",
						dest="tmpDir",
						default=os.path.join(tempfile.gettempdir(), "deqp-build-test"),
						help="Temporary directory")
	parser.add_argument("-r",
						"--recipe",
						dest="recipe",
						choices=[n for n, s in RECIPES] + ["all"],
						default="all",
						help="Build / test recipe")
	parser.add_argument("-d",
						"--dump-recipes",
						dest="dumpRecipes",
						action="store_true",
						help="Print out recipes that have any available actions")
	parser.add_argument("--skip-prerequisites",
						dest="skipPrerequisites",
						action="store_true",
						help="Skip external dependency fetch")

	return parser.parse_args()

if __name__ == "__main__":
	args	= parseArgs()
	env		= Environment(args.srcDir, args.tmpDir)

	if args.dumpRecipes:
		for name, steps in RECIPES:
			for step in steps:
				if step.isAvailable(env):
					print(name)
					break
	else:
		name, steps	= getAllRecipe(RECIPES) if args.recipe == "all" \
					  else getRecipe(RECIPES, args.recipe)

		print("Running %s" % name)

		allSteps = (PREREQUISITES if (args.skipPrerequisites == False) else []) + steps + POST_CHECKS
		runSteps(allSteps)

		print("All steps completed successfully")
