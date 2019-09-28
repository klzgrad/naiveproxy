# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2017 The Android Open Source Project
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
import posixpath
from fnmatch import fnmatch

from build.common import DEQP_DIR, writeFile

SRC_ROOTS = [
	"execserver",
	"executor",
	"external/vulkancts",
	"framework/common",
	"framework/delibs",
	"framework/egl",
	"framework/opengl",
	"framework/platform/android",
	"framework/qphelper",
	"framework/randomshaders",
	"framework/referencerenderer",
	"modules",
]

INCLUDE_PATTERNS = [
	"*.cpp",
	"*.c",
]

EXCLUDE_PATTERNS = [
	"execserver/xsWin32TestProcess.cpp",
	"external/vulkancts/modules/vulkan/vktBuildPrograms.cpp",
	"framework/delibs/dethread/standalone_test.c",
	"framework/randomshaders/rsgTest.cpp",
	"executor/tools/*",
	"execserver/tools/*",
]

TEMPLATE = """
# WARNING: This is auto-generated file. Do not modify, since changes will
# be lost! Modify scripts/gen_android_mk.py instead.

LOCAL_SRC_FILES :={SRC_FILES}

LOCAL_C_INCLUDES :={INCLUDES}

"""[1:-1]

def matchesAny (filename, patterns):
	for ptrn in patterns:
		if fnmatch(filename, ptrn):
			return True
	return False

def isSourceFile (filename):
	return matchesAny(filename, INCLUDE_PATTERNS) and not matchesAny(filename, EXCLUDE_PATTERNS)

def toPortablePath (nativePath):
	# os.path is so convenient...
	head, tail	= os.path.split(nativePath)
	components	= [tail]

	while head != None and head != '':
		head, tail = os.path.split(head)
		components.append(tail)

	components.reverse()

	portablePath = ""
	for component in components:
		portablePath = posixpath.join(portablePath, component)

	return portablePath

def getSourceFiles ():
	sources = []

	for srcRoot in SRC_ROOTS:
		baseDir = os.path.join(DEQP_DIR, srcRoot)
		for root, dirs, files in os.walk(baseDir):
			for file in files:
				absPath			= os.path.join(root, file)
				nativeRelPath	= os.path.relpath(absPath, DEQP_DIR)
				portablePath	= toPortablePath(nativeRelPath)

				if isSourceFile(portablePath):
					sources.append(portablePath)

	sources.sort()

	return sources

def getSourceDirs (sourceFiles):
	seenDirs	= set()
	sourceDirs	= []

	for sourceFile in sourceFiles:
		sourceDir = posixpath.dirname(sourceFile)

		if not sourceDir in seenDirs:
			sourceDirs.append(sourceDir)
			seenDirs.add(sourceDir)

	return sourceDirs

def genMkStringList (items):
	src = ""

	for item in items:
		src += " \\\n\t%s" % item

	return src

def genAndroidMk (sourceDirs, sourceFiles):
	src = TEMPLATE
	src = src.replace("{INCLUDES}", genMkStringList(["$(deqp_dir)/%s" % s for s in sourceDirs]))
	src = src.replace("{SRC_FILES}", genMkStringList(sourceFiles))

	return src

if __name__ == "__main__":
	sourceFiles		= getSourceFiles()
	sourceDirs		= getSourceDirs(sourceFiles)
	androidMkText	= genAndroidMk(sourceDirs, sourceFiles)

	writeFile(os.path.join(DEQP_DIR, "AndroidGen.mk"), androidMkText)
