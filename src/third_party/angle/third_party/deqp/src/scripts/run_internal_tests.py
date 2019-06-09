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
import shutil
import random
import subprocess

def die (msg):
	print msg
	exit(-1)

def shellquote(s):
	return '"%s"' % s.replace('\\', '\\\\').replace('"', '\"').replace('$', '\$').replace('`', '\`')

def execute (args, workDir = None):
	curPath = os.getcwd()
	if workDir != None:
		os.chdir(workDir)
	retcode	= subprocess.call(args)
	os.chdir(curPath)
	if retcode != 0:
		raise Exception("Failed to execute %s, got %d" % (str(args), retcode))

class Config:
	def __init__ (self, name, srcPath, buildPath, genParams, buildParams, testBinaryName, executor = 'executor', execserver = 'execserver', junitTool = 'testlog-to-junit'):
		self.name				= name
		self.srcPath			= srcPath
		self.buildPath			= buildPath
		self.genParams			= genParams
		self.buildParams		= buildParams
		self.testBinaryName		= testBinaryName
		self.executor			= executor
		self.execserver			= execserver
		self.junitTool			= junitTool

def initBuildDir (config):
	if os.path.exists(config.buildPath):
		shutil.rmtree(config.buildPath)

	os.makedirs(config.buildPath)
	execute(["cmake", os.path.realpath(config.srcPath)] + config.genParams, workDir = config.buildPath)

def prepareBuildDir (config):
	# If build dir exists, try to refresh
	if os.path.exists(config.buildPath):
		try:
			execute(["cmake", "."], workDir = config.buildPath)
		except:
			print "WARNING: Failed to refresh build dir, recreating"
			initBuildDir(config)
	else:
		initBuildDir(config)

def build (config):
	prepareBuildDir(config)
	execute(["cmake", "--build", "."] + config.buildParams, workDir = config.buildPath)

def runInternalTests (config):
	batchResultFile	= config.name + ".qpa"
	infoLogFile		= config.name + ".txt"
	junitFile		= config.name + ".xml"

	testWorkDir		= os.path.join(config.buildPath, "modules", "internal")
	junitToolPath	= os.path.join(config.buildPath, 'executor', config.junitTool)

	# Remove old files
	for file in [batchResultFile, junitFile]:
		if os.path.exists(file):
			os.remove(file)

	build(config)

	# Dump case list
	execute([config.testBinaryName, "--deqp-runmode=xml-caselist"], workDir = testWorkDir)

	# Run test binary using executor
	args = [
		os.path.join(config.buildPath, 'executor', config.executor),
		'--port=%d' % random.randint(50000, 60000),
		'--start-server=%s' % os.path.join(config.buildPath, 'execserver', config.execserver),
		'--binaryname=%s' % config.testBinaryName,
		'--cmdline=--deqp-crashhandler=enable --deqp-watchdog=enable',
		'--workdir=%s' % testWorkDir,
		'--caselistdir=%s' % os.path.join(testWorkDir),
		'--testset=dE-IT.*',
		'--out=%s' % batchResultFile,
		'--info=%s' % infoLogFile
	]
	execute(args)

	# Convert log to junit format
	execute([junitToolPath, batchResultFile, junitFile])

SRC_PATH		= os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
BASE_BUILD_PATH	= os.path.normpath(os.path.join(SRC_PATH, "..", "de-internal-tests"))

CONFIGS = [
	Config(
		"win32-vs10-debug",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "win32-vs10-debug"),
		['-GVisual Studio 10', '-DDEQP_TARGET=no_modules'],
		['--config', 'Debug', '--', '/m'],
		'Debug\\de-internal-tests.exe',
		'Debug\\executor.exe',
		'Debug\\execserver.exe',
		'Debug\\testlog-to-junit.exe'
	),
	Config(
		"win32-vs10-release",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "win32-vs10-release"),
		['-GVisual Studio 10', '-DDEQP_TARGET=no_modules'],
		['--config', 'Release', '--', '/m'],
		'Release\\de-internal-tests.exe',
		'Release\\executor.exe',
		'Release\\execserver.exe',
		'Release\\testlog-to-junit.exe'
	),
	Config(
		"win64-vs10-debug",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "win64-vs10-debug"),
		['-GVisual Studio 10 Win64', '-DDEQP_TARGET=no_modules'],
		['--config', 'Debug', '--', '/m'],
		'Debug\\de-internal-tests.exe',
		'Debug\\executor.exe',
		'Debug\\execserver.exe',
		'Debug\\testlog-to-junit.exe'
	),
	Config(
		"win64-vs10-release",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "win64-vs10-release"),
		['-GVisual Studio 10 Win64', '-DDEQP_TARGET=no_modules'],
		['--config', 'Release', '--', '/m'],
		'Release\\de-internal-tests.exe',
		'Release\\executor.exe',
		'Release\\execserver.exe',
		'Release\\testlog-to-junit.exe'
	),

	# GCC configs
	Config(
		"linux32-gcc-debug",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux32-gcc-debug"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_C_FLAGS=-m32', '-DCMAKE_CXX_FLAGS=-m32', '-DCMAKE_LIBRARY_PATH=/usr/lib32;usr/lib/i386-linux-gnu'],
		['--', '-j', '2'],
		'./de-internal-tests'
	),
	Config(
		"linux32-gcc-release",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux32-gcc-release"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS=-m32', '-DCMAKE_CXX_FLAGS=-m32', '-DCMAKE_LIBRARY_PATH=/usr/lib32;usr/lib/i386-linux-gnu'],
		['--', '-j', '2'],
		'./de-internal-tests'
	),
	Config(
		"linux64-gcc-debug",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux64-gcc-debug"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_C_FLAGS=-m64', '-DCMAKE_CXX_FLAGS=-m64'],
		['--', '-j', '2'],
		'./de-internal-tests'
	),
	Config(
		"linux64-gcc-release",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux64-gcc-release"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS=-m64', '-DCMAKE_CXX_FLAGS=-m64'],
		['--', '-j', '2'],
		'./de-internal-tests'
	),

	# Clang configs
	Config(
		"linux32-clang-debug",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux32-clang-debug"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_C_FLAGS=-m32', '-DCMAKE_CXX_FLAGS=-m32', '-DCMAKE_LIBRARY_PATH=/usr/lib32;usr/lib/i386-linux-gnu', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++', '-DDE_COMPILER=DE_COMPILER_CLANG'],
		['--', '-j', '2'],
		'./de-internal-tests'
	),
	Config(
		"linux32-clang-release",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux32-clang-release"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS=-m32', '-DCMAKE_CXX_FLAGS=-m32', '-DCMAKE_LIBRARY_PATH=/usr/lib32;usr/lib/i386-linux-gnu', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++', '-DDE_COMPILER=DE_COMPILER_CLANG'],
		['--', '-j', '2'],
		'./de-internal-tests'
	),
	Config(
		"linux64-clang-debug",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux64-clang-debug"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_C_FLAGS=-m64', '-DCMAKE_CXX_FLAGS=-m64', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++', '-DDE_COMPILER=DE_COMPILER_CLANG'],
		['--', '-j', '2'],
		'./de-internal-tests'
	),
	Config(
		"linux64-clang-release",
		SRC_PATH,
		os.path.join(BASE_BUILD_PATH, "linux64-clang-release"),
		['-DDEQP_TARGET=no_modules', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS=-m64', '-DCMAKE_CXX_FLAGS=-m64', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++', '-DDE_COMPILER=DE_COMPILER_CLANG'],
		['--', '-j', '2'],
		'./de-internal-tests'
	)
]

def findConfig (name):
	for config in CONFIGS:
		if config.name == name:
			return config
	return None

if __name__ == "__main__":
	if len(sys.argv) != 2:
		die("%s: [config]" % sys.argv[0])

	config = findConfig(sys.argv[1])
	if config == None:
		die("Config '%s' not found" % sys.argv[1])

	random.seed()
	runInternalTests(config)
