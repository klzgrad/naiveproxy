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
import re
import sys
import argparse
import threading
import subprocess

from build_apk import findSDK
from build_apk import getDefaultBuildRoot
from build_apk import getPackageAndLibrariesForTarget
from build_apk import getBuildRootRelativeAPKPath
from build_apk import parsePackageName

# Import from <root>/scripts
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

from build.common import *

class Device:
	def __init__(self, serial, product, model, device):
		self.serial		= serial
		self.product	= product
		self.model		= model
		self.device		= device

	def __str__ (self):
		return "%s: {product: %s, model: %s, device: %s}" % (self.serial, self.product, self.model, self.device)

def getDevices (adbPath):
	proc = subprocess.Popen([adbPath, 'devices', '-l'], stdout=subprocess.PIPE)
	(stdout, stderr) = proc.communicate()

	if proc.returncode != 0:
		raise Exception("adb devices -l failed, got %d" % proc.returncode)

	ptrn = re.compile(r'^([a-zA-Z0-9\.\-:]+)\s+.*product:([^\s]+)\s+model:([^\s]+)\s+device:([^\s]+)')
	devices = []
	for line in stdout.splitlines()[1:]:
		if len(line.strip()) == 0:
			continue

		m = ptrn.match(line.decode('utf-8'))
		if m == None:
			print("WARNING: Failed to parse device info '%s'" % line)
			continue

		devices.append(Device(m.group(1), m.group(2), m.group(3), m.group(4)))

	return devices

def execWithPrintPrefix (args, linePrefix="", failOnNonZeroExit=True):

	def readApplyPrefixAndPrint (source, prefix, sink):
		while True:
			line = source.readline()
			if len(line) == 0: # EOF
				break;
			sink.write(prefix + line.decode('utf-8'))

	process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	stdoutJob = threading.Thread(target=readApplyPrefixAndPrint, args=(process.stdout, linePrefix, sys.stdout))
	stderrJob = threading.Thread(target=readApplyPrefixAndPrint, args=(process.stderr, linePrefix, sys.stderr))
	stdoutJob.start()
	stderrJob.start()
	retcode = process.wait()
	if failOnNonZeroExit and retcode != 0:
		raise Exception("Failed to execute '%s', got %d" % (str(args), retcode))

def serialApply (f, argsList):
	for args in argsList:
		f(*args)

def parallelApply (f, argsList):
	class ErrorCode:
		def __init__ (self):
			self.error = None;

	def applyAndCaptureError (func, args, errorCode):
		try:
			func(*args)
		except:
			errorCode.error = sys.exc_info()

	errorCode = ErrorCode()
	jobs = []
	for args in argsList:
		job = threading.Thread(target=applyAndCaptureError, args=(f, args, errorCode))
		job.start()
		jobs.append(job)

	for job in jobs:
		job.join()

	if errorCode.error:
		raise errorCode.error[0](errorCode.error[1]).with_traceback(errorCode.error[2])

def uninstall (adbPath, packageName, extraArgs = [], printPrefix=""):
	print(printPrefix + "Removing existing %s...\n" % packageName,)
	execWithPrintPrefix([adbPath] + extraArgs + [
			'uninstall',
			packageName
		], printPrefix, failOnNonZeroExit=False)
	print(printPrefix + "Remove complete\n",)

def install (adbPath, apkPath, extraArgs = [], printPrefix=""):
	print(printPrefix + "Installing %s...\n" % apkPath,)
	execWithPrintPrefix([adbPath] + extraArgs + [
			'install',
			'-g',
			apkPath
		], printPrefix)
	print(printPrefix + "Install complete\n",)

def installToDevice (device, adbPath, packageName, apkPath, printPrefix=""):
	if len(printPrefix) == 0:
		print("Installing to %s (%s)...\n" % (device.serial, device.model), end='')
	else:
		print(printPrefix + "Installing to %s\n" % device.serial, end='')

	uninstall(adbPath, packageName, ['-s', device.serial], printPrefix)
	install(adbPath, apkPath, ['-s', device.serial], printPrefix)

def installToDevices (devices, doParallel, adbPath, packageName, apkPath):
	padLen = max([len(device.model) for device in devices])+1
	if doParallel:
		parallelApply(installToDevice, [(device, adbPath, packageName, apkPath, ("(%s):%s" % (device.model, ' ' * (padLen - len(device.model))))) for device in devices]);
	else:
		serialApply(installToDevice, [(device, adbPath, packageName, apkPath) for device in devices]);

def installToAllDevices (doParallel, adbPath, packageName, apkPath):
	devices = getDevices(adbPath)
	installToDevices(devices, doParallel, adbPath, packageName, apkPath)

def getAPKPath (buildRootPath, target):
	package = getPackageAndLibrariesForTarget(target)[0]
	return os.path.join(buildRootPath, getBuildRootRelativeAPKPath(package))

def getPackageName (target):
	package			= getPackageAndLibrariesForTarget(target)[0]
	manifestPath	= os.path.join(DEQP_DIR, "android", package.appDirName, "AndroidManifest.xml")

	return parsePackageName(manifestPath)

def findADB ():
	adbInPath = which("adb")
	if adbInPath != None:
		return adbInPath

	sdkPath = findSDK()
	if sdkPath != None:
		adbInSDK = os.path.join(sdkPath, "platform-tools", "adb")
		if os.path.isfile(adbInSDK):
			return adbInSDK

	return None

def parseArgs ():
	defaultADBPath		= findADB()
	defaultBuildRoot	= getDefaultBuildRoot()

	parser = argparse.ArgumentParser(os.path.basename(__file__),
		formatter_class=argparse.ArgumentDefaultsHelpFormatter)
	parser.add_argument('--build-root',
		dest='buildRoot',
		default=defaultBuildRoot,
		help="Root build directory")
	parser.add_argument('--adb',
		dest='adbPath',
		default=defaultADBPath,
		help="ADB binary path",
		required=(True if defaultADBPath == None else False))
	parser.add_argument('--target',
		dest='target',
		help='Build target',
		choices=['deqp', 'openglcts'],
		default='deqp')
	parser.add_argument('-p', '--parallel',
		dest='doParallel',
		action="store_true",
		help="Install package in parallel")
	parser.add_argument('-s', '--serial',
		dest='serial',
		type=str,
		nargs='+',
		help="Install package to device with serial number")
	parser.add_argument('-a', '--all',
		dest='all',
		action="store_true",
		help="Install to all devices")

	return parser.parse_args()

if __name__ == "__main__":
	args		= parseArgs()
	packageName	= getPackageName(args.target)
	apkPath		= getAPKPath(args.buildRoot, args.target)

	if not os.path.isfile(apkPath):
		die("%s does not exist" % apkPath)

	if args.all:
		installToAllDevices(args.doParallel, args.adbPath, packageName, apkPath)
	else:
		if args.serial == None:
			devices = getDevices(args.adbPath)
			if len(devices) == 0:
				die('No devices connected')
			elif len(devices) == 1:
				installToDevice(devices[0], args.adbPath, packageName, apkPath)
			else:
				print("More than one device connected:")
				for i in range(0, len(devices)):
					print("%3d: %16s %s" % ((i+1), devices[i].serial, devices[i].model))

				deviceNdx = int(input("Choose device (1-%d): " % len(devices)))
				installToDevice(devices[deviceNdx-1], args.adbPath, packageName, apkPath)
		else:
			devices = getDevices(args.adbPath)

			devices = [dev for dev in devices if dev.serial in args.serial]
			devSerials = [dev.serial for dev in devices]
			notFounds = [serial for serial in args.serial if not serial in devSerials]

			for notFound in notFounds:
				print("Couldn't find device matching serial '%s'" % notFound)

			installToDevices(devices, args.doParallel, args.adbPath, packageName, apkPath)
