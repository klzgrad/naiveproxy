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

# \todo [2017-04-10 pyry]
# * Use smarter asset copy in main build
#   * cmake -E copy_directory doesn't copy timestamps which will cause
#     assets to be always re-packaged
# * Consider adding an option for downloading SDK & NDK

import os
import re
import sys
import glob
import string
import shutil
import argparse
import tempfile
import xml.etree.ElementTree

# Import from <root>/scripts
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

from build.common import *
from build.config import *
from build.build import *

class SDKEnv:
	def __init__(self, path):
		self.path				= path
		self.buildToolsVersion	= SDKEnv.selectBuildToolsVersion(self.path)

	@staticmethod
	def getBuildToolsVersions (path):
		buildToolsPath	= os.path.join(path, "build-tools")
		versions		= []

		if os.path.exists(buildToolsPath):
			for item in os.listdir(buildToolsPath):
				m = re.match(r'^([0-9]+)\.([0-9]+)\.([0-9]+)$', item)
				if m != None:
					versions.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))

		return versions

	@staticmethod
	def selectBuildToolsVersion (path):
		preferred	= [(25, 0, 2)]
		versions	= SDKEnv.getBuildToolsVersions(path)

		if len(versions) == 0:
			return (0,0,0)

		for candidate in preferred:
			if candidate in versions:
				return candidate

		# Pick newest
		versions.sort()
		return versions[-1]

	def getPlatformLibrary (self, apiVersion):
		return os.path.join(self.path, "platforms", "android-%d" % apiVersion, "android.jar")

	def getBuildToolsPath (self):
		return os.path.join(self.path, "build-tools", "%d.%d.%d" % self.buildToolsVersion)

class NDKEnv:
	def __init__(self, path):
		self.path		= path
		self.version	= NDKEnv.detectVersion(self.path)
		self.hostOsName	= NDKEnv.detectHostOsName(self.path)

	@staticmethod
	def getKnownAbis ():
		return ["armeabi-v7a", "arm64-v8a", "x86", "x86_64"]

	@staticmethod
	def getAbiPrebuiltsName (abiName):
		prebuilts = {
			"armeabi-v7a":	'android-arm',
			"arm64-v8a":	'android-arm64',
			"x86":			'android-x86',
			"x86_64":		'android-x86_64',
		}

		if not abiName in prebuilts:
			raise Exception("Unknown ABI: " + abiName)

		return prebuilts[abiName]

	@staticmethod
	def detectVersion (path):
		propFilePath = os.path.join(path, "source.properties")
		try:
			with open(propFilePath) as propFile:
				for line in propFile:
					keyValue = list(map(lambda x: x.strip(), line.split("=")))
					if keyValue[0] == "Pkg.Revision":
						versionParts = keyValue[1].split(".")
						return tuple(map(int, versionParts[0:2]))
		except Exception as e:
			raise Exception("Failed to read source prop file '%s': %s" % (propFilePath, str(e)))
		except:
			raise Exception("Failed to read source prop file '%s': unkown error")

		raise Exception("Failed to detect NDK version (does %s/source.properties have Pkg.Revision?)" % path)

	@staticmethod
	def isHostOsSupported (hostOsName):
		os			= HostInfo.getOs()
		bits		= HostInfo.getArchBits()
		hostOsParts	= hostOsName.split('-')

		if len(hostOsParts) > 1:
			assert(len(hostOsParts) == 2)
			assert(hostOsParts[1] == "x86_64")

			if bits != 64:
				return False

		if os == HostInfo.OS_WINDOWS:
			return hostOsParts[0] == 'windows'
		elif os == HostInfo.OS_LINUX:
			return hostOsParts[0] == 'linux'
		elif os == HostInfo.OS_OSX:
			return hostOsParts[0] == 'darwin'
		else:
			raise Exception("Unhandled HostInfo.getOs() '%d'" % os)

	@staticmethod
	def detectHostOsName (path):
		hostOsNames = [
			"windows",
			"windows-x86_64",
			"darwin-x86",
			"darwin-x86_64",
			"linux-x86",
			"linux-x86_64"
		]

		for name in hostOsNames:
			if os.path.exists(os.path.join(path, "prebuilt", name)):
				return name

		raise Exception("Failed to determine NDK host OS")

class Environment:
	def __init__(self, sdk, ndk):
		self.sdk		= sdk
		self.ndk		= ndk

class Configuration:
	def __init__(self, env, buildPath, abis, nativeApi, minApi, nativeBuildType, gtfTarget, verbose, layers, angle):
		self.env				= env
		self.sourcePath			= DEQP_DIR
		self.buildPath			= buildPath
		self.abis				= abis
		self.nativeApi			= nativeApi
		self.javaApi			= 28
		self.minApi				= minApi
		self.nativeBuildType	= nativeBuildType
		self.gtfTarget			= gtfTarget
		self.verbose			= verbose
		self.layers				= layers
		self.angle				= angle
		self.cmakeGenerator		= selectFirstAvailableGenerator([NINJA_GENERATOR, MAKEFILE_GENERATOR, NMAKE_GENERATOR])

	def check (self):
		if self.cmakeGenerator == None:
			raise Exception("Failed to find build tools for CMake")

		if not os.path.exists(self.env.ndk.path):
			raise Exception("Android NDK not found at %s" % self.env.ndk.path)

		if not NDKEnv.isHostOsSupported(self.env.ndk.hostOsName):
			raise Exception("NDK '%s' is not supported on this machine" % self.env.ndk.hostOsName)

		if self.env.ndk.version[0] < 15:
			raise Exception("Android NDK version %d is not supported; build requires NDK version >= 15" % (self.env.ndk.version[0]))

		if not (self.minApi <= self.javaApi <= self.nativeApi):
			raise Exception("Requires: min-api (%d) <= java-api (%d) <= native-api (%d)" % (self.minApi, self.javaApi, self.nativeApi))

		if self.env.sdk.buildToolsVersion == (0,0,0):
			raise Exception("No build tools directory found at %s" % os.path.join(self.env.sdk.path, "build-tools"))

		androidBuildTools = ["aapt", "zipalign", "dx"]
		for tool in androidBuildTools:
			if which(tool, [self.env.sdk.getBuildToolsPath()]) == None:
				raise Exception("Missing Android build tool: %s" % toolPath)

		requiredToolsInPath = ["javac", "jar", "jarsigner", "keytool"]
		for tool in requiredToolsInPath:
			if which(tool) == None:
				raise Exception("%s not in PATH" % tool)

def log (config, msg):
	if config.verbose:
		print(msg)

def executeAndLog (config, args):
	if config.verbose:
		print(" ".join(args))
	execute(args)

# Path components

class ResolvablePathComponent:
	def __init__ (self):
		pass

class SourceRoot (ResolvablePathComponent):
	def resolve (self, config):
		return config.sourcePath

class BuildRoot (ResolvablePathComponent):
	def resolve (self, config):
		return config.buildPath

class NativeBuildPath (ResolvablePathComponent):
	def __init__ (self, abiName):
		self.abiName = abiName

	def resolve (self, config):
		return getNativeBuildPath(config, self.abiName)

class GeneratedResSourcePath (ResolvablePathComponent):
	def __init__ (self, package):
		self.package = package

	def resolve (self, config):
		packageComps	= self.package.getPackageName(config).split('.')
		packageDir		= os.path.join(*packageComps)

		return os.path.join(config.buildPath, self.package.getAppDirName(), "src", packageDir, "R.java")

def resolvePath (config, path):
	resolvedComps = []

	for component in path:
		if isinstance(component, ResolvablePathComponent):
			resolvedComps.append(component.resolve(config))
		else:
			resolvedComps.append(str(component))

	return os.path.join(*resolvedComps)

def resolvePaths (config, paths):
	return list(map(lambda p: resolvePath(config, p), paths))

class BuildStep:
	def __init__ (self):
		pass

	def getInputs (self):
		return []

	def getOutputs (self):
		return []

	@staticmethod
	def expandPathsToFiles (paths):
		"""
		Expand mixed list of file and directory paths into a flattened list
		of files. Any non-existent input paths are preserved as is.
		"""

		def getFiles (dirPath):
			for root, dirs, files in os.walk(dirPath):
				for file in files:
					yield os.path.join(root, file)

		files = []
		for path in paths:
			if os.path.isdir(path):
				files += list(getFiles(path))
			else:
				files.append(path)

		return files

	def isUpToDate (self, config):
		inputs				= resolvePaths(config, self.getInputs())
		outputs				= resolvePaths(config, self.getOutputs())

		assert len(inputs) > 0 and len(outputs) > 0

		expandedInputs		= BuildStep.expandPathsToFiles(inputs)
		expandedOutputs		= BuildStep.expandPathsToFiles(outputs)

		existingInputs		= list(filter(os.path.exists, expandedInputs))
		existingOutputs		= list(filter(os.path.exists, expandedOutputs))

		if len(existingInputs) != len(expandedInputs):
			for file in expandedInputs:
				if file not in existingInputs:
					print("ERROR: Missing input file: %s" % file)
			die("Missing input files")

		if len(existingOutputs) != len(expandedOutputs):
			return False # One or more output files are missing

		lastInputChange		= max(map(os.path.getmtime, existingInputs))
		firstOutputChange	= min(map(os.path.getmtime, existingOutputs))

		return lastInputChange <= firstOutputChange

	def update (config):
		die("BuildStep.update() not implemented")

def getNativeBuildPath (config, abiName):
	return os.path.join(config.buildPath, "%s-%s-%d" % (abiName, config.nativeBuildType, config.nativeApi))

def clearCMakeCacheVariables(args):
	# New value, so clear the necessary cmake variables
	args.append('-UANGLE_LIBS')
	args.append('-UGLES1_LIBRARY')
	args.append('-UGLES2_LIBRARY')
	args.append('-UEGL_LIBRARY')

def buildNativeLibrary (config, abiName):
	def makeNDKVersionString (version):
		minorVersionString = (chr(ord('a') + version[1]) if version[1] > 0 else "")
		return "r%d%s" % (version[0], minorVersionString)

	def getBuildArgs (config, abiName):
		args = ['-DDEQP_TARGET=android',
				'-DDEQP_TARGET_TOOLCHAIN=ndk-modern',
				'-DCMAKE_C_FLAGS=-Werror',
				'-DCMAKE_CXX_FLAGS=-Werror',
				'-DANDROID_NDK_PATH=%s' % config.env.ndk.path,
				'-DANDROID_ABI=%s' % abiName,
				'-DDE_ANDROID_API=%s' % config.nativeApi,
				'-DGLCTS_GTF_TARGET=%s' % config.gtfTarget]

		if config.angle is None:
			# Find any previous builds that may have embedded ANGLE libs and clear the CMake cache
			for abi in NDKEnv.getKnownAbis():
				cMakeCachePath = os.path.join(getNativeBuildPath(config, abi), "CMakeCache.txt")
				try:
					if 'ANGLE_LIBS' in open(cMakeCachePath).read():
						clearCMakeCacheVariables(args)
				except IOError:
					pass
		else:
			cMakeCachePath = os.path.join(getNativeBuildPath(config, abiName), "CMakeCache.txt")
			angleLibsDir = os.path.join(config.angle, abiName)
			# Check if the user changed where the ANGLE libs are being loaded from
			try:
				if angleLibsDir not in open(cMakeCachePath).read():
					clearCMakeCacheVariables(args)
			except IOError:
				pass
			args.append('-DANGLE_LIBS=%s' % angleLibsDir)

		return args

	nativeBuildPath	= getNativeBuildPath(config, abiName)
	buildConfig		= BuildConfig(nativeBuildPath, config.nativeBuildType, getBuildArgs(config, abiName))

	build(buildConfig, config.cmakeGenerator, ["deqp"])

def executeSteps (config, steps):
	for step in steps:
		if not step.isUpToDate(config):
			step.update(config)

def parsePackageName (manifestPath):
	tree = xml.etree.ElementTree.parse(manifestPath)

	if not 'package' in tree.getroot().attrib:
		raise Exception("'package' attribute missing from root element in %s" % manifestPath)

	return tree.getroot().attrib['package']

class PackageDescription:
	def __init__ (self, appDirName, appName, hasResources = True):
		self.appDirName		= appDirName
		self.appName		= appName
		self.hasResources	= hasResources

	def getAppName (self):
		return self.appName

	def getAppDirName (self):
		return self.appDirName

	def getPackageName (self, config):
		manifestPath	= resolvePath(config, self.getManifestPath())

		return parsePackageName(manifestPath)

	def getManifestPath (self):
		return [SourceRoot(), "android", self.appDirName, "AndroidManifest.xml"]

	def getResPath (self):
		return [SourceRoot(), "android", self.appDirName, "res"]

	def getSourcePaths (self):
		return [
				[SourceRoot(), "android", self.appDirName, "src"]
			]

	def getAssetsPath (self):
		return [BuildRoot(), self.appDirName, "assets"]

	def getClassesJarPath (self):
		return [BuildRoot(), self.appDirName, "bin", "classes.jar"]

	def getClassesDexPath (self):
		return [BuildRoot(), self.appDirName, "bin", "classes.dex"]

	def getAPKPath (self):
		return [BuildRoot(), self.appDirName, "bin", self.appName + ".apk"]

# Build step implementations

class BuildNativeLibrary (BuildStep):
	def __init__ (self, abi):
		self.abi = abi

	def isUpToDate (self, config):
		return False

	def update (self, config):
		log(config, "BuildNativeLibrary: %s" % self.abi)
		buildNativeLibrary(config, self.abi)

class GenResourcesSrc (BuildStep):
	def __init__ (self, package):
		self.package = package

	def getInputs (self):
		return [self.package.getResPath(), self.package.getManifestPath()]

	def getOutputs (self):
		return [[GeneratedResSourcePath(self.package)]]

	def update (self, config):
		aaptPath	= which("aapt", [config.env.sdk.getBuildToolsPath()])
		dstDir		= os.path.dirname(resolvePath(config, [GeneratedResSourcePath(self.package)]))

		if not os.path.exists(dstDir):
			os.makedirs(dstDir)

		executeAndLog(config, [
				aaptPath,
				"package",
				"-f",
				"-m",
				"-S", resolvePath(config, self.package.getResPath()),
				"-M", resolvePath(config, self.package.getManifestPath()),
				"-J", resolvePath(config, [BuildRoot(), self.package.getAppDirName(), "src"]),
				"-I", config.env.sdk.getPlatformLibrary(config.javaApi)
			])

# Builds classes.jar from *.java files
class BuildJavaSource (BuildStep):
	def __init__ (self, package, libraries = []):
		self.package	= package
		self.libraries	= libraries

	def getSourcePaths (self):
		srcPaths = self.package.getSourcePaths()

		if self.package.hasResources:
			srcPaths.append([BuildRoot(), self.package.getAppDirName(), "src"]) # Generated sources

		return srcPaths

	def getInputs (self):
		inputs = self.getSourcePaths()

		for lib in self.libraries:
			inputs.append(lib.getClassesJarPath())

		return inputs

	def getOutputs (self):
		return [self.package.getClassesJarPath()]

	def update (self, config):
		srcPaths	= resolvePaths(config, self.getSourcePaths())
		srcFiles	= BuildStep.expandPathsToFiles(srcPaths)
		jarPath		= resolvePath(config, self.package.getClassesJarPath())
		objPath		= resolvePath(config, [BuildRoot(), self.package.getAppDirName(), "obj"])
		classPaths	= [objPath] + [resolvePath(config, lib.getClassesJarPath()) for lib in self.libraries]
		pathSep		= ";" if HostInfo.getOs() == HostInfo.OS_WINDOWS else ":"

		if os.path.exists(objPath):
			shutil.rmtree(objPath)

		os.makedirs(objPath)

		for srcFile in srcFiles:
			executeAndLog(config, [
					"javac",
					"-source", "1.7",
					"-target", "1.7",
					"-d", objPath,
					"-bootclasspath", config.env.sdk.getPlatformLibrary(config.javaApi),
					"-classpath", pathSep.join(classPaths),
					"-sourcepath", pathSep.join(srcPaths),
					srcFile
				])

		if not os.path.exists(os.path.dirname(jarPath)):
			os.makedirs(os.path.dirname(jarPath))

		try:
			pushWorkingDir(objPath)
			executeAndLog(config, [
					"jar",
					"cf",
					jarPath,
					"."
				])
		finally:
			popWorkingDir()

class BuildDex (BuildStep):
	def __init__ (self, package, libraries):
		self.package	= package
		self.libraries	= libraries

	def getInputs (self):
		return [self.package.getClassesJarPath()] + [lib.getClassesJarPath() for lib in self.libraries]

	def getOutputs (self):
		return [self.package.getClassesDexPath()]

	def update (self, config):
		dxPath		= which("dx", [config.env.sdk.getBuildToolsPath()])
		srcPaths	= resolvePaths(config, self.getInputs())
		dexPath		= resolvePath(config, self.package.getClassesDexPath())
		jarPaths	= [resolvePath(config, self.package.getClassesJarPath())]

		for lib in self.libraries:
			jarPaths.append(resolvePath(config, lib.getClassesJarPath()))

		executeAndLog(config, [
				dxPath,
				"--dex",
				"--output", dexPath
			] + jarPaths)

class CreateKeystore (BuildStep):
	def __init__ (self):
		self.keystorePath	= [BuildRoot(), "debug.keystore"]

	def getOutputs (self):
		return [self.keystorePath]

	def isUpToDate (self, config):
		return os.path.exists(resolvePath(config, self.keystorePath))

	def update (self, config):
		executeAndLog(config, [
				"keytool",
				"-genkey",
				"-keystore", resolvePath(config, self.keystorePath),
				"-storepass", "android",
				"-alias", "androiddebugkey",
				"-keypass", "android",
				"-keyalg", "RSA",
				"-keysize", "2048",
				"-validity", "10000",
				"-dname", "CN=, OU=, O=, L=, S=, C=",
			])

# Builds APK without code
class BuildBaseAPK (BuildStep):
	def __init__ (self, package, libraries = []):
		self.package	= package
		self.libraries	= libraries
		self.dstPath	= [BuildRoot(), self.package.getAppDirName(), "tmp", "base.apk"]

	def getResPaths (self):
		paths = []
		for pkg in [self.package] + self.libraries:
			if pkg.hasResources:
				paths.append(pkg.getResPath())
		return paths

	def getInputs (self):
		return [self.package.getManifestPath()] + self.getResPaths()

	def getOutputs (self):
		return [self.dstPath]

	def update (self, config):
		aaptPath	= which("aapt", [config.env.sdk.getBuildToolsPath()])
		dstPath		= resolvePath(config, self.dstPath)

		if not os.path.exists(os.path.dirname(dstPath)):
			os.makedirs(os.path.dirname(dstPath))

		args = [
			aaptPath,
			"package",
			"-f",
			"--min-sdk-version", str(config.minApi),
			"--target-sdk-version", str(config.javaApi),
			"-M", resolvePath(config, self.package.getManifestPath()),
			"-I", config.env.sdk.getPlatformLibrary(config.javaApi),
			"-F", dstPath,
		]

		for resPath in self.getResPaths():
			args += ["-S", resolvePath(config, resPath)]

		if config.verbose:
			args.append("-v")

		executeAndLog(config, args)

def addFilesToAPK (config, apkPath, baseDir, relFilePaths):
	aaptPath		= which("aapt", [config.env.sdk.getBuildToolsPath()])
	maxBatchSize	= 25

	pushWorkingDir(baseDir)
	try:
		workQueue = list(relFilePaths)

		while len(workQueue) > 0:
			batchSize	= min(len(workQueue), maxBatchSize)
			items		= workQueue[0:batchSize]

			executeAndLog(config, [
					aaptPath,
					"add",
					"-f", apkPath,
				] + items)

			del workQueue[0:batchSize]
	finally:
		popWorkingDir()

def addFileToAPK (config, apkPath, baseDir, relFilePath):
	addFilesToAPK(config, apkPath, baseDir, [relFilePath])

class AddJavaToAPK (BuildStep):
	def __init__ (self, package):
		self.package	= package
		self.srcPath	= BuildBaseAPK(self.package).getOutputs()[0]
		self.dstPath	= [BuildRoot(), self.package.getAppDirName(), "tmp", "with-java.apk"]

	def getInputs (self):
		return [
				self.srcPath,
				self.package.getClassesDexPath(),
			]

	def getOutputs (self):
		return [self.dstPath]

	def update (self, config):
		srcPath		= resolvePath(config, self.srcPath)
		dstPath		= resolvePath(config, self.getOutputs()[0])
		dexPath		= resolvePath(config, self.package.getClassesDexPath())

		shutil.copyfile(srcPath, dstPath)
		addFileToAPK(config, dstPath, os.path.dirname(dexPath), os.path.basename(dexPath))

class AddAssetsToAPK (BuildStep):
	def __init__ (self, package, abi):
		self.package	= package
		self.buildPath	= [NativeBuildPath(abi)]
		self.srcPath	= AddJavaToAPK(self.package).getOutputs()[0]
		self.dstPath	= [BuildRoot(), self.package.getAppDirName(), "tmp", "with-assets.apk"]

	def getInputs (self):
		return [
				self.srcPath,
				self.buildPath + ["assets"]
			]

	def getOutputs (self):
		return [self.dstPath]

	@staticmethod
	def getAssetFiles (buildPath):
		allFiles = BuildStep.expandPathsToFiles([os.path.join(buildPath, "assets")])
		return [os.path.relpath(p, buildPath) for p in allFiles]

	def update (self, config):
		srcPath		= resolvePath(config, self.srcPath)
		dstPath		= resolvePath(config, self.getOutputs()[0])
		buildPath	= resolvePath(config, self.buildPath)
		assetFiles	= AddAssetsToAPK.getAssetFiles(buildPath)

		shutil.copyfile(srcPath, dstPath)

		addFilesToAPK(config, dstPath, buildPath, assetFiles)

class AddNativeLibsToAPK (BuildStep):
	def __init__ (self, package, abis):
		self.package	= package
		self.abis		= abis
		self.srcPath	= AddAssetsToAPK(self.package, "").getOutputs()[0]
		self.dstPath	= [BuildRoot(), self.package.getAppDirName(), "tmp", "with-native-libs.apk"]

	def getInputs (self):
		paths = [self.srcPath]
		for abi in self.abis:
			paths.append([NativeBuildPath(abi), "libdeqp.so"])
		return paths

	def getOutputs (self):
		return [self.dstPath]

	def update (self, config):
		srcPath		= resolvePath(config, self.srcPath)
		dstPath		= resolvePath(config, self.getOutputs()[0])
		pkgPath		= resolvePath(config, [BuildRoot(), self.package.getAppDirName()])
		libFiles	= []

		# Create right directory structure first
		for abi in self.abis:
			libSrcPath	= resolvePath(config, [NativeBuildPath(abi), "libdeqp.so"])
			libRelPath	= os.path.join("lib", abi, "libdeqp.so")
			libAbsPath	= os.path.join(pkgPath, libRelPath)

			if not os.path.exists(os.path.dirname(libAbsPath)):
				os.makedirs(os.path.dirname(libAbsPath))

			shutil.copyfile(libSrcPath, libAbsPath)
			libFiles.append(libRelPath)

			if config.layers:
				layersGlob = os.path.join(config.layers, abi, "libVkLayer_*.so")
				libVkLayers = glob.glob(layersGlob)
				for layer in libVkLayers:
					layerFilename = os.path.basename(layer)
					layerRelPath = os.path.join("lib", abi, layerFilename)
					layerAbsPath = os.path.join(pkgPath, layerRelPath)
					shutil.copyfile(layer, layerAbsPath)
					libFiles.append(layerRelPath)
					print("Adding layer binary: %s" % (layer,))

			if config.angle:
				angleGlob = os.path.join(config.angle, abi, "lib*_angle.so")
				libAngle = glob.glob(angleGlob)
				for lib in libAngle:
					libFilename = os.path.basename(lib)
					libRelPath = os.path.join("lib", abi, libFilename)
					libAbsPath = os.path.join(pkgPath, libRelPath)
					shutil.copyfile(lib, libAbsPath)
					libFiles.append(libRelPath)
					print("Adding ANGLE binary: %s" % (lib,))

		shutil.copyfile(srcPath, dstPath)
		addFilesToAPK(config, dstPath, pkgPath, libFiles)

class SignAPK (BuildStep):
	def __init__ (self, package):
		self.package		= package
		self.srcPath		= AddNativeLibsToAPK(self.package, []).getOutputs()[0]
		self.dstPath		= [BuildRoot(), self.package.getAppDirName(), "tmp", "signed.apk"]
		self.keystorePath	= CreateKeystore().getOutputs()[0]

	def getInputs (self):
		return [self.srcPath, self.keystorePath]

	def getOutputs (self):
		return [self.dstPath]

	def update (self, config):
		srcPath		= resolvePath(config, self.srcPath)
		dstPath		= resolvePath(config, self.dstPath)

		executeAndLog(config, [
				"jarsigner",
				"-keystore", resolvePath(config, self.keystorePath),
				"-storepass", "android",
				"-keypass", "android",
				"-signedjar", dstPath,
				srcPath,
				"androiddebugkey"
			])

def getBuildRootRelativeAPKPath (package):
	return os.path.join(package.getAppDirName(), package.getAppName() + ".apk")

class FinalizeAPK (BuildStep):
	def __init__ (self, package):
		self.package		= package
		self.srcPath		= SignAPK(self.package).getOutputs()[0]
		self.dstPath		= [BuildRoot(), getBuildRootRelativeAPKPath(self.package)]
		self.keystorePath	= CreateKeystore().getOutputs()[0]

	def getInputs (self):
		return [self.srcPath]

	def getOutputs (self):
		return [self.dstPath]

	def update (self, config):
		srcPath			= resolvePath(config, self.srcPath)
		dstPath			= resolvePath(config, self.dstPath)
		zipalignPath	= os.path.join(config.env.sdk.getBuildToolsPath(), "zipalign")

		executeAndLog(config, [
				zipalignPath,
				"-f", "4",
				srcPath,
				dstPath
			])

def getBuildStepsForPackage (abis, package, libraries = []):
	steps = []

	assert len(abis) > 0

	# Build native code first
	for abi in abis:
		steps += [BuildNativeLibrary(abi)]

	# Build library packages
	for library in libraries:
		if library.hasResources:
			steps.append(GenResourcesSrc(library))
		steps.append(BuildJavaSource(library))

	# Build main package .java sources
	if package.hasResources:
		steps.append(GenResourcesSrc(package))
	steps.append(BuildJavaSource(package, libraries))
	steps.append(BuildDex(package, libraries))

	# Build base APK
	steps.append(BuildBaseAPK(package, libraries))
	steps.append(AddJavaToAPK(package))

	# Add assets from first ABI
	steps.append(AddAssetsToAPK(package, abis[0]))

	# Add native libs to APK
	steps.append(AddNativeLibsToAPK(package, abis))

	# Finalize APK
	steps.append(CreateKeystore())
	steps.append(SignAPK(package))
	steps.append(FinalizeAPK(package))

	return steps

def getPackageAndLibrariesForTarget (target):
	deqpPackage	= PackageDescription("package", "dEQP")
	ctsPackage	= PackageDescription("openglcts", "Khronos-CTS", hasResources = False)

	if target == 'deqp':
		return (deqpPackage, [])
	elif target == 'openglcts':
		return (ctsPackage, [deqpPackage])
	else:
		raise Exception("Uknown target '%s'" % target)

def findNDK ():
	ndkBuildPath = which('ndk-build')
	if ndkBuildPath != None:
		return os.path.dirname(ndkBuildPath)
	else:
		return None

def findSDK ():
	sdkBuildPath = which('android')
	if sdkBuildPath != None:
		return os.path.dirname(os.path.dirname(sdkBuildPath))
	else:
		return None

def getDefaultBuildRoot ():
	return os.path.join(tempfile.gettempdir(), "deqp-android-build")

def parseArgs ():
	nativeBuildTypes	= ['Release', 'Debug', 'MinSizeRel', 'RelWithAsserts', 'RelWithDebInfo']
	defaultNDKPath		= findNDK()
	defaultSDKPath		= findSDK()
	defaultBuildRoot	= getDefaultBuildRoot()

	parser = argparse.ArgumentParser(os.path.basename(__file__),
		formatter_class=argparse.ArgumentDefaultsHelpFormatter)
	parser.add_argument('--native-build-type',
		dest='nativeBuildType',
		default="RelWithAsserts",
		choices=nativeBuildTypes,
		help="Native code build type")
	parser.add_argument('--build-root',
		dest='buildRoot',
		default=defaultBuildRoot,
		help="Root build directory")
	parser.add_argument('--abis',
		dest='abis',
		default=",".join(NDKEnv.getKnownAbis()),
		help="ABIs to build")
	parser.add_argument('--native-api',
		type=int,
		dest='nativeApi',
		default=28,
		help="Android API level to target in native code")
	parser.add_argument('--min-api',
		type=int,
		dest='minApi',
		default=22,
		help="Minimum Android API level for which the APK can be installed")
	parser.add_argument('--sdk',
		dest='sdkPath',
		default=defaultSDKPath,
		help="Android SDK path",
		required=(True if defaultSDKPath == None else False))
	parser.add_argument('--ndk',
		dest='ndkPath',
		default=defaultNDKPath,
		help="Android NDK path",
		required=(True if defaultNDKPath == None else False))
	parser.add_argument('-v', '--verbose',
		dest='verbose',
		help="Verbose output",
		default=False,
		action='store_true')
	parser.add_argument('--target',
		dest='target',
		help='Build target',
		choices=['deqp', 'openglcts'],
		default='deqp')
	parser.add_argument('--kc-cts-target',
		dest='gtfTarget',
		default='gles32',
		choices=['gles32', 'gles31', 'gles3', 'gles2', 'gl'],
		help="KC-CTS (GTF) target API (only used in openglcts target)")
	parser.add_argument('--layers-path',
		dest='layers',
		default=None,
		required=False)
	parser.add_argument('--angle-path',
		dest='angle',
		default=None,
		required=False)

	args = parser.parse_args()

	def parseAbis (abisStr):
		knownAbis	= set(NDKEnv.getKnownAbis())
		abis		= []

		for abi in abisStr.split(','):
			abi = abi.strip()
			if not abi in knownAbis:
				raise Exception("Unknown ABI: %s" % abi)
			abis.append(abi)

		return abis

	# Custom parsing & checks
	try:
		args.abis = parseAbis(args.abis)
		if len(args.abis) == 0:
			raise Exception("--abis can't be empty")
	except Exception as e:
		print("ERROR: %s" % str(e))
		parser.print_help()
		sys.exit(-1)

	return args

if __name__ == "__main__":
	args		= parseArgs()

	ndk			= NDKEnv(os.path.realpath(args.ndkPath))
	sdk			= SDKEnv(os.path.realpath(args.sdkPath))
	buildPath	= os.path.realpath(args.buildRoot)
	env			= Environment(sdk, ndk)
	config		= Configuration(env, buildPath, abis=args.abis, nativeApi=args.nativeApi, minApi=args.minApi, nativeBuildType=args.nativeBuildType, gtfTarget=args.gtfTarget,
						 verbose=args.verbose, layers=args.layers, angle=args.angle)

	try:
		config.check()
	except Exception as e:
		print("ERROR: %s" % str(e))
		print("")
		print("Please check your configuration:")
		print("  --sdk=%s" % args.sdkPath)
		print("  --ndk=%s" % args.ndkPath)
		sys.exit(-1)

	pkg, libs	= getPackageAndLibrariesForTarget(args.target)
	steps		= getBuildStepsForPackage(config.abis, pkg, libs)

	executeSteps(config, steps)

	print("")
	print("Built %s" % os.path.join(buildPath, getBuildRootRelativeAPKPath(pkg)))
