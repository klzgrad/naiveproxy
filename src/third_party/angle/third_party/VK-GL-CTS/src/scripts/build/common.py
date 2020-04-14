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
import shlex
import platform
import subprocess

DEQP_DIR = os.path.realpath(os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..")))

# HostInfo describes properties of the host where these scripts
# are running on.
class HostInfo:
	OS_WINDOWS	= 0
	OS_LINUX	= 1
	OS_OSX		= 2

	@staticmethod
	def getOs ():
		if sys.platform == 'darwin':
			return HostInfo.OS_OSX
		elif sys.platform == 'win32':
			return HostInfo.OS_WINDOWS
		elif sys.platform.startswith('linux'):
			return HostInfo.OS_LINUX
		else:
			raise Exception("Unknown sys.platform '%s'" % sys.platform)

	@staticmethod
	def getArchBits ():
		MACHINE_BITS = {
			"i386":		32,
			"i686":		32,
			"x86":		32,
			"x86_64":	64,
			"AMD64":	64
		}
		machine = platform.machine()

		if not machine in MACHINE_BITS:
			raise Exception("Unknown platform.machine() '%s'" % machine)

		return MACHINE_BITS[machine]

def die (msg):
	print(msg)
	exit(-1)

def shellquote(s):
	return '"%s"' % s.replace('\\', '\\\\').replace('"', '\"').replace('$', '\$').replace('`', '\`')

g_workDirStack = []

def pushWorkingDir (path):
	oldDir = os.getcwd()
	os.chdir(path)
	g_workDirStack.append(oldDir)

def popWorkingDir ():
	assert len(g_workDirStack) > 0
	newDir = g_workDirStack[-1]
	g_workDirStack.pop()
	os.chdir(newDir)

def execute (args):
	retcode	= subprocess.call(args)
	if retcode != 0:
		raise Exception("Failed to execute '%s', got %d" % (str(args), retcode))

def readBinaryFile (filename):
	f = open(filename, 'rb')
	data = f.read()
	f.close()
	return data

def readFile (filename):
	f = open(filename, 'rt')
	data = f.read()
	f.close()
	return data

def writeBinaryFile (filename, data):
	f = open(filename, 'wb')
	f.write(data)
	f.close()

def writeFile (filename, data):
	if (sys.version_info < (3, 0)):
		f = open(filename, 'wt')
	else:
		f = open(filename, 'wt', newline='\n')
	f.write(data)
	f.close()

def which (binName, paths = None):
	if paths == None:
		paths = os.environ['PATH'].split(os.pathsep)

	def whichImpl (binWithExt):
		for path in paths:
			path = path.strip('"')
			fullPath = os.path.join(path, binWithExt)
			if os.path.isfile(fullPath) and os.access(fullPath, os.X_OK):
				return fullPath

		return None

	extensions = [""]
	if HostInfo.getOs() == HostInfo.OS_WINDOWS:
		extensions += [".exe", ".bat"]

	for extension in extensions:
		extResult = whichImpl(binName + extension)
		if extResult != None:
			return extResult

	return None
