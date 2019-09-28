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
import re
import sys
import copy
import zlib
import time
import shlex
import shutil
import fnmatch
import tarfile
import argparse
import platform
import datetime
import tempfile
import posixpath
import subprocess

from build.common import *
from build.config import *
from build.build import *

pythonExecutable = sys.executable or "python"

def die (msg):
	print(msg)
	sys.exit(-1)

def removeLeadingPath (path, basePath):
	# Both inputs must be normalized already
	assert os.path.normpath(path) == path
	assert os.path.normpath(basePath) == basePath
	return path[len(basePath) + 1:]

def findFile (candidates):
	for file in candidates:
		if os.path.exists(file):
			return file
	return None

def getFileList (basePath):
	allFiles	= []
	basePath	= os.path.normpath(basePath)
	for root, dirs, files in os.walk(basePath):
		for file in files:
			relPath = removeLeadingPath(os.path.normpath(os.path.join(root, file)), basePath)
			allFiles.append(relPath)
	return allFiles

def toDatetime (dateTuple):
	Y, M, D = dateTuple
	return datetime.datetime(Y, M, D)

class PackageBuildInfo:
	def __init__ (self, releaseConfig, srcBasePath, dstBasePath, tmpBasePath):
		self.releaseConfig	= releaseConfig
		self.srcBasePath	= srcBasePath
		self.dstBasePath	= dstBasePath
		self.tmpBasePath	= tmpBasePath

	def getReleaseConfig (self):
		return self.releaseConfig

	def getReleaseVersion (self):
		return self.releaseConfig.getVersion()

	def getReleaseId (self):
		# Release id is crc32(releaseConfig + release)
		return zlib.crc32(self.releaseConfig.getName() + self.releaseConfig.getVersion()) & 0xffffffff

	def getSrcBasePath (self):
		return self.srcBasePath

	def getTmpBasePath (self):
		return self.tmpBasePath

class DstFile (object):
	def __init__ (self, dstFile):
		self.dstFile = dstFile

	def makeDir (self):
		dirName = os.path.dirname(self.dstFile)
		if not os.path.exists(dirName):
			os.makedirs(dirName)

	def make (self, packageBuildInfo):
		assert False # Should not be called

class CopyFile (DstFile):
	def __init__ (self, srcFile, dstFile):
		super(CopyFile, self).__init__(dstFile)
		self.srcFile = srcFile

	def make (self, packageBuildInfo):
		self.makeDir()
		if os.path.exists(self.dstFile):
			die("%s already exists" % self.dstFile)
		shutil.copyfile(self.srcFile, self.dstFile)

class GenReleaseInfoFileTarget (DstFile):
	def __init__ (self, dstFile):
		super(GenReleaseInfoFileTarget, self).__init__(dstFile)

	def make (self, packageBuildInfo):
		self.makeDir()

		scriptPath = os.path.normpath(os.path.join(packageBuildInfo.srcBasePath, "framework", "qphelper", "gen_release_info.py"))
		execute([
				pythonExecutable,
				"-B", # no .py[co]
				scriptPath,
				"--name=%s" % packageBuildInfo.getReleaseVersion(),
				"--id=0x%08x" % packageBuildInfo.getReleaseId(),
				"--out=%s" % self.dstFile
			])

class GenCMake (DstFile):
	def __init__ (self, srcFile, dstFile, replaceVars):
		super(GenCMake, self).__init__(dstFile)
		self.srcFile		= srcFile
		self.replaceVars	= replaceVars

	def make (self, packageBuildInfo):
		self.makeDir()
		print("    GenCMake: %s" % removeLeadingPath(self.dstFile, packageBuildInfo.dstBasePath))
		src = readFile(self.srcFile)
		for var, value in self.replaceVars:
			src = re.sub('set\(%s\s+"[^"]*"' % re.escape(var),
						 'set(%s "%s"' % (var, value), src)
		writeFile(self.dstFile, src)

def createFileTargets (srcBasePath, dstBasePath, files, filters):
	usedFiles	= set() # Files that are already included by other filters
	targets		= []

	for isMatch, createFileObj in filters:
		# Build list of files that match filter
		matchingFiles = []
		for file in files:
			if not file in usedFiles and isMatch(file):
				matchingFiles.append(file)

		# Build file objects, add to used set
		for file in matchingFiles:
			usedFiles.add(file)
			targets.append(createFileObj(os.path.join(srcBasePath, file), os.path.join(dstBasePath, file)))

	return targets

# Generates multiple file targets based on filters
class FileTargetGroup:
	def __init__ (self, srcBasePath, dstBasePath, filters, srcBasePathFunc=PackageBuildInfo.getSrcBasePath):
		self.srcBasePath	= srcBasePath
		self.dstBasePath	= dstBasePath
		self.filters		= filters
		self.getSrcBasePath	= srcBasePathFunc

	def make (self, packageBuildInfo):
		fullSrcPath		= os.path.normpath(os.path.join(self.getSrcBasePath(packageBuildInfo), self.srcBasePath))
		fullDstPath		= os.path.normpath(os.path.join(packageBuildInfo.dstBasePath, self.dstBasePath))

		allFiles		= getFileList(fullSrcPath)
		targets			= createFileTargets(fullSrcPath, fullDstPath, allFiles, self.filters)

		# Make all file targets
		for file in targets:
			file.make(packageBuildInfo)

# Single file target
class SingleFileTarget:
	def __init__ (self, srcFile, dstFile, makeTarget):
		self.srcFile	= srcFile
		self.dstFile	= dstFile
		self.makeTarget	= makeTarget

	def make (self, packageBuildInfo):
		fullSrcPath		= os.path.normpath(os.path.join(packageBuildInfo.srcBasePath, self.srcFile))
		fullDstPath		= os.path.normpath(os.path.join(packageBuildInfo.dstBasePath, self.dstFile))

		target = self.makeTarget(fullSrcPath, fullDstPath)
		target.make(packageBuildInfo)

class BuildTarget:
	def __init__ (self, baseConfig, generator, targets = None):
		self.baseConfig	= baseConfig
		self.generator	= generator
		self.targets	= targets

	def make (self, packageBuildInfo):
		print("    Building %s" % self.baseConfig.getBuildDir())

		# Create config with full build dir path
		config = BuildConfig(os.path.join(packageBuildInfo.getTmpBasePath(), self.baseConfig.getBuildDir()),
							 self.baseConfig.getBuildType(),
							 self.baseConfig.getArgs(),
							 srcPath = os.path.join(packageBuildInfo.dstBasePath, "src"))

		assert not os.path.exists(config.getBuildDir())
		build(config, self.generator, self.targets)

class BuildAndroidTarget:
	def __init__ (self, dstFile):
		self.dstFile = dstFile

	def make (self, packageBuildInfo):
		print("    Building Android binary")

		buildRoot = os.path.join(packageBuildInfo.tmpBasePath, "android-build")

		assert not os.path.exists(buildRoot)
		os.makedirs(buildRoot)

		# Execute build script
		scriptPath = os.path.normpath(os.path.join(packageBuildInfo.dstBasePath, "src", "android", "scripts", "build.py"))
		execute([
				pythonExecutable,
				"-B", # no .py[co]
				scriptPath,
				"--build-root=%s" % buildRoot,
			])

		srcFile		= os.path.normpath(os.path.join(buildRoot, "package", "bin", "dEQP-debug.apk"))
		dstFile		= os.path.normpath(os.path.join(packageBuildInfo.dstBasePath, self.dstFile))

		CopyFile(srcFile, dstFile).make(packageBuildInfo)

class FetchExternalSourcesTarget:
	def __init__ (self):
		pass

	def make (self, packageBuildInfo):
		scriptPath = os.path.normpath(os.path.join(packageBuildInfo.dstBasePath, "src", "external", "fetch_sources.py"))
		execute([
				pythonExecutable,
				"-B", # no .py[co]
				scriptPath,
			])

class RemoveSourcesTarget:
	def __init__ (self):
		pass

	def make (self, packageBuildInfo):
		shutil.rmtree(os.path.join(packageBuildInfo.dstBasePath, "src"), ignore_errors=False)

class Module:
	def __init__ (self, name, targets):
		self.name		= name
		self.targets	= targets

	def make (self, packageBuildInfo):
		for target in self.targets:
			target.make(packageBuildInfo)

class ReleaseConfig:
	def __init__ (self, name, version, modules, sources = True):
		self.name			= name
		self.version		= version
		self.modules		= modules
		self.sources		= sources

	def getName (self):
		return self.name

	def getVersion (self):
		return self.version

	def getModules (self):
		return self.modules

	def packageWithSources (self):
		return self.sources

def matchIncludeExclude (includePatterns, excludePatterns, filename):
	components = os.path.normpath(filename).split(os.sep)
	for pattern in excludePatterns:
		for component in components:
			if fnmatch.fnmatch(component, pattern):
				return False

	for pattern in includePatterns:
		for component in components:
			if fnmatch.fnmatch(component, pattern):
				return True

	return False

def copyFileFilter (includePatterns, excludePatterns=[]):
	return (lambda f: matchIncludeExclude(includePatterns, excludePatterns, f),
			lambda s, d: CopyFile(s, d))

def makeFileCopyGroup (srcDir, dstDir, includePatterns, excludePatterns=[]):
	return FileTargetGroup(srcDir, dstDir, [copyFileFilter(includePatterns, excludePatterns)])

def makeTmpFileCopyGroup (srcDir, dstDir, includePatterns, excludePatterns=[]):
	return FileTargetGroup(srcDir, dstDir, [copyFileFilter(includePatterns, excludePatterns)], PackageBuildInfo.getTmpBasePath)

def makeFileCopy (srcFile, dstFile):
	return SingleFileTarget(srcFile, dstFile, lambda s, d: CopyFile(s, d))

def getReleaseFileName (configName, releaseName):
	today = datetime.date.today()
	return "dEQP-%s-%04d-%02d-%02d-%s" % (releaseName, today.year, today.month, today.day, configName)

def getTempDir ():
	dirName = os.path.join(tempfile.gettempdir(), "dEQP-Releases")
	if not os.path.exists(dirName):
		os.makedirs(dirName)
	return dirName

def makeRelease (releaseConfig):
	releaseName			= getReleaseFileName(releaseConfig.getName(), releaseConfig.getVersion())
	tmpPath				= getTempDir()
	srcBasePath			= DEQP_DIR
	dstBasePath			= os.path.join(tmpPath, releaseName)
	tmpBasePath			= os.path.join(tmpPath, releaseName + "-tmp")
	packageBuildInfo	= PackageBuildInfo(releaseConfig, srcBasePath, dstBasePath, tmpBasePath)
	dstArchiveName		= releaseName + ".tar.bz2"

	print("Creating release %s to %s" % (releaseName, tmpPath))

	# Remove old temporary dirs
	for path in [dstBasePath, tmpBasePath]:
		if os.path.exists(path):
			shutil.rmtree(path, ignore_errors=False)

	# Make all modules
	for module in releaseConfig.getModules():
		print("  Processing module %s" % module.name)
		module.make(packageBuildInfo)

	# Remove sources?
	if not releaseConfig.packageWithSources():
		shutil.rmtree(os.path.join(dstBasePath, "src"), ignore_errors=False)

	# Create archive
	print("Creating %s" % dstArchiveName)
	archive	= tarfile.open(dstArchiveName, 'w:bz2')
	archive.add(dstBasePath, arcname=releaseName)
	archive.close()

	# Remove tmp dirs
	for path in [dstBasePath, tmpBasePath]:
		if os.path.exists(path):
			shutil.rmtree(path, ignore_errors=False)

	print("Done!")

# Module declarations

SRC_FILE_PATTERNS	= ["*.h", "*.hpp", "*.c", "*.cpp", "*.m", "*.mm", "*.inl", "*.java", "*.aidl", "CMakeLists.txt", "LICENSE.txt", "*.cmake"]
TARGET_PATTERNS		= ["*.cmake", "*.h", "*.lib", "*.dll", "*.so", "*.txt"]

BASE = Module("Base", [
	makeFileCopy		("LICENSE",									"src/LICENSE"),
	makeFileCopy		("CMakeLists.txt",							"src/CMakeLists.txt"),
	makeFileCopyGroup	("targets",									"src/targets",							TARGET_PATTERNS),
	makeFileCopyGroup	("execserver",								"src/execserver",						SRC_FILE_PATTERNS),
	makeFileCopyGroup	("executor",								"src/executor",							SRC_FILE_PATTERNS),
	makeFileCopy		("modules/CMakeLists.txt",					"src/modules/CMakeLists.txt"),
	makeFileCopyGroup	("external",								"src/external",							["CMakeLists.txt", "*.py"]),

	# Stylesheet for displaying test logs on browser
	makeFileCopyGroup	("doc/testlog-stylesheet",					"doc/testlog-stylesheet",				["*"]),

	# Non-optional parts of framework
	makeFileCopy		("framework/CMakeLists.txt",				"src/framework/CMakeLists.txt"),
	makeFileCopyGroup	("framework/delibs",						"src/framework/delibs",					SRC_FILE_PATTERNS),
	makeFileCopyGroup	("framework/common",						"src/framework/common",					SRC_FILE_PATTERNS),
	makeFileCopyGroup	("framework/qphelper",						"src/framework/qphelper",				SRC_FILE_PATTERNS),
	makeFileCopyGroup	("framework/platform",						"src/framework/platform",				SRC_FILE_PATTERNS),
	makeFileCopyGroup	("framework/opengl",						"src/framework/opengl",					SRC_FILE_PATTERNS, ["simplereference"]),
	makeFileCopyGroup	("framework/egl",							"src/framework/egl",					SRC_FILE_PATTERNS),

	# android sources
	makeFileCopyGroup	("android/package/src",						"src/android/package/src",				SRC_FILE_PATTERNS),
	makeFileCopy		("android/package/AndroidManifest.xml",		"src/android/package/AndroidManifest.xml"),
	makeFileCopyGroup	("android/package/res",						"src/android/package/res",				["*.png", "*.xml"]),
	makeFileCopyGroup	("android/scripts",							"src/android/scripts", [
		"common.py",
		"build.py",
		"resources.py",
		"install.py",
		"launch.py",
		"debug.py"
		]),

	# Release info
	GenReleaseInfoFileTarget("src/framework/qphelper/qpReleaseInfo.inl")
])

DOCUMENTATION = Module("Documentation", [
	makeFileCopyGroup	("doc/pdf",									"doc",									["*.pdf"]),
	makeFileCopyGroup	("doc",										"doc",									["porting_layer_changes_*.txt"]),
])

GLSHARED = Module("Shared GL Tests", [
	# Optional framework components
	makeFileCopyGroup	("framework/randomshaders",					"src/framework/randomshaders",			SRC_FILE_PATTERNS),
	makeFileCopyGroup	("framework/opengl/simplereference",		"src/framework/opengl/simplereference",	SRC_FILE_PATTERNS),
	makeFileCopyGroup	("framework/referencerenderer",				"src/framework/referencerenderer",		SRC_FILE_PATTERNS),

	makeFileCopyGroup	("modules/glshared",						"src/modules/glshared",					SRC_FILE_PATTERNS),
])

GLES2 = Module("GLES2", [
	makeFileCopyGroup	("modules/gles2",							"src/modules/gles2",					SRC_FILE_PATTERNS),
	makeFileCopyGroup	("data/gles2",								"src/data/gles2",						["*.*"]),
	makeFileCopyGroup	("doc/testspecs/GLES2",						"doc/testspecs/GLES2",					["*.txt"])
])

GLES3 = Module("GLES3", [
	makeFileCopyGroup	("modules/gles3",							"src/modules/gles3",					SRC_FILE_PATTERNS),
	makeFileCopyGroup	("data/gles3",								"src/data/gles3",						["*.*"]),
	makeFileCopyGroup	("doc/testspecs/GLES3",						"doc/testspecs/GLES3",					["*.txt"])
])

GLES31 = Module("GLES31", [
	makeFileCopyGroup	("modules/gles31",							"src/modules/gles31",					SRC_FILE_PATTERNS),
	makeFileCopyGroup	("data/gles31",								"src/data/gles31",						["*.*"]),
	makeFileCopyGroup	("doc/testspecs/GLES31",					"doc/testspecs/GLES31",					["*.txt"])
])

EGL = Module("EGL", [
	makeFileCopyGroup	("modules/egl",								"src/modules/egl",						SRC_FILE_PATTERNS)
])

INTERNAL = Module("Internal", [
	makeFileCopyGroup	("modules/internal",						"src/modules/internal",					SRC_FILE_PATTERNS),
	makeFileCopyGroup	("data/internal",							"src/data/internal",					["*.*"]),
])

EXTERNAL_SRCS = Module("External sources", [
	FetchExternalSourcesTarget()
])

ANDROID_BINARIES = Module("Android Binaries", [
	BuildAndroidTarget	("bin/android/dEQP.apk"),
	makeFileCopyGroup	("targets/android",							"bin/android",							["*.bat", "*.sh"]),
])

COMMON_BUILD_ARGS	= ['-DPNG_SRC_PATH=%s' % os.path.realpath(os.path.join(DEQP_DIR, '..', 'libpng'))]
NULL_X32_CONFIG		= BuildConfig('null-x32',	'Release', ['-DDEQP_TARGET=null', '-DCMAKE_C_FLAGS=-m32', '-DCMAKE_CXX_FLAGS=-m32'] + COMMON_BUILD_ARGS)
NULL_X64_CONFIG		= BuildConfig('null-x64',	'Release', ['-DDEQP_TARGET=null', '-DCMAKE_C_FLAGS=-m64', '-DCMAKE_CXX_FLAGS=-m64'] + COMMON_BUILD_ARGS)
GLX_X32_CONFIG		= BuildConfig('glx-x32',	'Release', ['-DDEQP_TARGET=x11_glx', '-DCMAKE_C_FLAGS=-m32', '-DCMAKE_CXX_FLAGS=-m32'] + COMMON_BUILD_ARGS)
GLX_X64_CONFIG		= BuildConfig('glx-x64',	'Release', ['-DDEQP_TARGET=x11_glx', '-DCMAKE_C_FLAGS=-m64', '-DCMAKE_CXX_FLAGS=-m64'] + COMMON_BUILD_ARGS)

EXCLUDE_BUILD_FILES = ["CMakeFiles", "*.a", "*.cmake"]

LINUX_X32_COMMON_BINARIES = Module("Linux x32 Common Binaries", [
	BuildTarget			(NULL_X32_CONFIG, ANY_UNIX_GENERATOR),
	makeTmpFileCopyGroup(NULL_X32_CONFIG.getBuildDir() + "/execserver",		"bin/linux32",					["*"],	EXCLUDE_BUILD_FILES),
	makeTmpFileCopyGroup(NULL_X32_CONFIG.getBuildDir() + "/executor",		"bin/linux32",					["*"],	EXCLUDE_BUILD_FILES),
])

LINUX_X64_COMMON_BINARIES = Module("Linux x64 Common Binaries", [
	BuildTarget			(NULL_X64_CONFIG, ANY_UNIX_GENERATOR),
	makeTmpFileCopyGroup(NULL_X64_CONFIG.getBuildDir() + "/execserver",		"bin/linux64",					["*"],	EXCLUDE_BUILD_FILES),
	makeTmpFileCopyGroup(NULL_X64_CONFIG.getBuildDir() + "/executor",		"bin/linux64",					["*"],	EXCLUDE_BUILD_FILES),
])

# Special module to remove src dir, for example after binary build
REMOVE_SOURCES = Module("Remove sources from package", [
	RemoveSourcesTarget()
])

# Release configuration

ALL_MODULES		= [
	BASE,
	DOCUMENTATION,
	GLSHARED,
	GLES2,
	GLES3,
	GLES31,
	EGL,
	INTERNAL,
	EXTERNAL_SRCS,
]

ALL_BINARIES	= [
	LINUX_X64_COMMON_BINARIES,
	ANDROID_BINARIES,
]

RELEASE_CONFIGS	= {
	"src":		ALL_MODULES,
	"src-bin":	ALL_MODULES + ALL_BINARIES,
	"bin":		ALL_MODULES + ALL_BINARIES + [REMOVE_SOURCES],
}

def parseArgs ():
	parser = argparse.ArgumentParser(description = "Build release package")
	parser.add_argument("-c",
						"--config",
						dest="config",
						choices=RELEASE_CONFIGS.keys(),
						required=True,
						help="Release configuration")
	parser.add_argument("-n",
						"--name",
						dest="name",
						required=True,
						help="Package-specific name")
	parser.add_argument("-v",
						"--version",
						dest="version",
						required=True,
						help="Version code")
	return parser.parse_args()

if __name__ == "__main__":
	args	= parseArgs()
	config	= ReleaseConfig(args.name, args.version, RELEASE_CONFIGS[args.config])
	makeRelease(config)
