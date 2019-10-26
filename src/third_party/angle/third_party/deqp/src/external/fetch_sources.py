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
import tarfile
import hashlib
import argparse
import subprocess
import ssl
import stat

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "scripts"))

from build.common import *

EXTERNAL_DIR	= os.path.realpath(os.path.normpath(os.path.dirname(__file__)))

def computeChecksum (data):
	return hashlib.sha256(data).hexdigest()

def onReadonlyRemoveError (func, path, exc_info):
	os.chmod(path, stat.S_IWRITE)
	os.unlink(path)

class Source:
	def __init__(self, baseDir, extractDir):
		self.baseDir		= baseDir
		self.extractDir		= extractDir

	def clean (self):
		fullDstPath = os.path.join(EXTERNAL_DIR, self.baseDir, self.extractDir)
		# Remove read-only first
		readonlydir = os.path.join(fullDstPath, ".git", "objects", "pack")
		if os.path.exists(readonlydir):
			shutil.rmtree(readonlydir, onerror = onReadonlyRemoveError )
		if os.path.exists(fullDstPath):
			shutil.rmtree(fullDstPath, ignore_errors=False)

class SourcePackage (Source):
	def __init__(self, url, filename, checksum, baseDir, extractDir = "src", postExtract=None):
		Source.__init__(self, baseDir, extractDir)
		self.url			= url
		self.filename		= filename
		self.checksum		= checksum
		self.archiveDir		= "packages"
		self.postExtract	= postExtract

	def clean (self):
		Source.clean(self)
		self.removeArchives()

	def update (self, cmdProtocol = None):
		if not self.isArchiveUpToDate():
			self.fetchAndVerifyArchive()

		if self.getExtractedChecksum() != self.checksum:
			Source.clean(self)
			self.extract()
			self.storeExtractedChecksum(self.checksum)

	def removeArchives (self):
		archiveDir = os.path.join(EXTERNAL_DIR, pkg.baseDir, pkg.archiveDir)
		if os.path.exists(archiveDir):
			shutil.rmtree(archiveDir, ignore_errors=False)

	def isArchiveUpToDate (self):
		archiveFile = os.path.join(EXTERNAL_DIR, pkg.baseDir, pkg.archiveDir, pkg.filename)
		if os.path.exists(archiveFile):
			return computeChecksum(readBinaryFile(archiveFile)) == self.checksum
		else:
			return False

	def getExtractedChecksumFilePath (self):
		return os.path.join(EXTERNAL_DIR, pkg.baseDir, pkg.archiveDir, "extracted")

	def getExtractedChecksum (self):
		extractedChecksumFile = self.getExtractedChecksumFilePath()

		if os.path.exists(extractedChecksumFile):
			return readFile(extractedChecksumFile)
		else:
			return None

	def storeExtractedChecksum (self, checksum):
		checksum_bytes = checksum.encode("utf-8")
		writeBinaryFile(self.getExtractedChecksumFilePath(), checksum_bytes)

	def connectToUrl (self, url):
		result = None

		if sys.version_info < (3, 0):
			from urllib2 import urlopen
		else:
			from urllib.request import urlopen

		if args.insecure:
			print("Ignoring certificate checks")
			ssl_context = ssl._create_unverified_context()
			result = urlopen(url, context=ssl_context)
		else:
			result = urlopen(url)

		return result

	def fetchAndVerifyArchive (self):
		print("Fetching %s" % self.url)

		req			= self.connectToUrl(self.url)
		data		= req.read()
		checksum	= computeChecksum(data)
		dstPath		= os.path.join(EXTERNAL_DIR, self.baseDir, self.archiveDir, self.filename)

		if checksum != self.checksum:
			raise Exception("Checksum mismatch for %s, expected %s, got %s" % (self.filename, self.checksum, checksum))

		if not os.path.exists(os.path.dirname(dstPath)):
			os.mkdir(os.path.dirname(dstPath))

		writeBinaryFile(dstPath, data)

	def extract (self):
		print("Extracting %s to %s/%s" % (self.filename, self.baseDir, self.extractDir))

		srcPath	= os.path.join(EXTERNAL_DIR, self.baseDir, self.archiveDir, self.filename)
		tmpPath	= os.path.join(EXTERNAL_DIR, ".extract-tmp-%s" % self.baseDir)
		dstPath	= os.path.join(EXTERNAL_DIR, self.baseDir, self.extractDir)
		archive	= tarfile.open(srcPath)

		if os.path.exists(tmpPath):
			shutil.rmtree(tmpPath, ignore_errors=False)

		os.mkdir(tmpPath)

		archive.extractall(tmpPath)
		archive.close()

		extractedEntries = os.listdir(tmpPath)
		if len(extractedEntries) != 1 or not os.path.isdir(os.path.join(tmpPath, extractedEntries[0])):
			raise Exception("%s doesn't contain single top-level directory" % self.filename)

		topLevelPath = os.path.join(tmpPath, extractedEntries[0])

		if not os.path.exists(dstPath):
			os.mkdir(dstPath)

		for entry in os.listdir(topLevelPath):
			if os.path.exists(os.path.join(dstPath, entry)):
				raise Exception("%s exists already" % entry)

			shutil.move(os.path.join(topLevelPath, entry), dstPath)

		shutil.rmtree(tmpPath, ignore_errors=True)

		if self.postExtract != None:
			self.postExtract(dstPath)

class SourceFile (Source):
	def __init__(self, url, filename, checksum, baseDir, extractDir = "src"):
		Source.__init__(self, baseDir, extractDir)
		self.url			= url
		self.filename		= filename
		self.checksum		= checksum

	def update (self, cmdProtocol = None):
		if not self.isFileUpToDate():
			Source.clean(self)
			self.fetchAndVerifyFile()

	def isFileUpToDate (self):
		file = os.path.join(EXTERNAL_DIR, pkg.baseDir, pkg.extractDir, pkg.filename)
		if os.path.exists(file):
			data = readFile(file)
			return computeChecksum(data.encode('utf-8')) == self.checksum
		else:
			return False

	def connectToUrl (self, url):
		result = None

		if sys.version_info < (3, 0):
			from urllib2 import urlopen
		else:
			from urllib.request import urlopen

		if args.insecure:
			print("Ignoring certificate checks")
			ssl_context = ssl._create_unverified_context()
			result = urlopen(url, context=ssl_context)
		else:
			result = urlopen(url)

		return result

	def fetchAndVerifyFile (self):
		print("Fetching %s" % self.url)

		req			= self.connectToUrl(self.url)
		data		= req.read()
		checksum	= computeChecksum(data)
		dstPath		= os.path.join(EXTERNAL_DIR, self.baseDir, self.extractDir, self.filename)

		if checksum != self.checksum:
			raise Exception("Checksum mismatch for %s, expected %s, got %s" % (self.filename, self.checksum, checksum))

		if not os.path.exists(os.path.dirname(dstPath)):
			os.mkdir(os.path.dirname(dstPath))

		writeBinaryFile(dstPath, data)

class GitRepo (Source):
	def __init__(self, httpsUrl, sshUrl, revision, baseDir, extractDir = "src", removeTags = []):
		Source.__init__(self, baseDir, extractDir)
		self.httpsUrl	= httpsUrl
		self.sshUrl		= sshUrl
		self.revision	= revision
		self.removeTags	= removeTags

	def detectProtocol(self, cmdProtocol = None):
		# reuse parent repo protocol
		proc = subprocess.Popen(['git', 'ls-remote', '--get-url', 'origin'], stdout=subprocess.PIPE)
		(stdout, stderr) = proc.communicate()

		if proc.returncode != 0:
			raise Exception("Failed to execute 'git ls-remote origin', got %d" % proc.returncode)
		if (stdout[:3] == 'ssh') or (stdout[:3] == 'git'):
			protocol = 'ssh'
		else:
			# remote 'origin' doesn't exist, assume 'https' as checkout protocol
			protocol = 'https'
		return protocol

	def selectUrl(self, cmdProtocol = None):
		try:
			if cmdProtocol == None:
				protocol = self.detectProtocol(cmdProtocol)
			else:
				protocol = cmdProtocol
		except:
			# fallback to https on any issues
			protocol = 'https'

		if protocol == 'ssh':
			if self.sshUrl != None:
				url = self.sshUrl
			else:
				assert self.httpsUrl != None
				url = self.httpsUrl
		else:
			assert protocol == 'https'
			url = self.httpsUrl

		assert url != None
		return url

	def update (self, cmdProtocol = None):
		fullDstPath = os.path.join(EXTERNAL_DIR, self.baseDir, self.extractDir)

		url = self.selectUrl(cmdProtocol)
		if not os.path.exists(fullDstPath):
			execute(["git", "clone", "--no-checkout", url, fullDstPath])

		pushWorkingDir(fullDstPath)
		try:
			for tag in self.removeTags:
				proc = subprocess.Popen(['git', 'tag', '-l', tag], stdout=subprocess.PIPE)
				(stdout, stderr) = proc.communicate()
				if proc.returncode == 0:
					execute(["git", "tag", "-d",tag])
			execute(["git", "fetch", "--tags", url, "+refs/heads/*:refs/remotes/origin/*"])
			execute(["git", "checkout", self.revision])
		finally:
			popWorkingDir()

def postExtractLibpng (path):
	shutil.copy(os.path.join(path, "scripts", "pnglibconf.h.prebuilt"),
				os.path.join(path, "pnglibconf.h"))

PACKAGES = [
	SourcePackage(
		"http://zlib.net/zlib-1.2.11.tar.gz",
		"zlib-1.2.11.tar.gz",
		"c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
		"zlib"),
	SourcePackage(
		"http://prdownloads.sourceforge.net/libpng/libpng-1.6.27.tar.gz",
		"libpng-1.6.27.tar.gz",
		"c9d164ec247f426a525a7b89936694aefbc91fb7a50182b198898b8fc91174b4",
		"libpng",
		postExtract = postExtractLibpng),
	SourceFile(
		"https://raw.githubusercontent.com/baldurk/renderdoc/v1.1/renderdoc/api/app/renderdoc_app.h",
		"renderdoc_app.h",
		"e7b5f0aa5b1b0eadc63a1c624c0ca7f5af133aa857d6a4271b0ef3d0bdb6868e",
		"renderdoc"),
	GitRepo(
		"https://github.com/KhronosGroup/SPIRV-Tools.git",
		None,
		"2c0111e6eba779cf30e8c7f5a733ea0762895ba0",
		"spirv-tools"),
	GitRepo(
		"https://github.com/KhronosGroup/glslang.git",
		None,
		"a549bb81752365526f6d7334f00961ea08689211",
		"glslang",
		removeTags = ["master-tot"]),
	GitRepo(
		"https://github.com/KhronosGroup/SPIRV-Headers.git",
		None,
		"8b911bd2ba37677037b38c9bd286c7c05701bcda",
		"spirv-headers"),
	GitRepo(
		"https://github.com/google/amber.git",
		None,
		"d26ee22dd7faab1845a531d410f7ec1db407402a",
		"amber"),
]

def parseArgs ():
	versionsForInsecure = ((2,7,9), (3,4,3))
	versionsForInsecureStr = ' or '.join(('.'.join(str(x) for x in v)) for v in versionsForInsecure)

	parser = argparse.ArgumentParser(description = "Fetch external sources")
	parser.add_argument('--clean', dest='clean', action='store_true', default=False,
						help='Remove sources instead of fetching')
	parser.add_argument('--insecure', dest='insecure', action='store_true', default=False,
						help="Disable certificate check for external sources."
						" Minimum python version required " + versionsForInsecureStr)
	parser.add_argument('--protocol', dest='protocol', default=None, choices=['ssh', 'https'],
						help="Select protocol to checkout git repositories.")

	args = parser.parse_args()

	if args.insecure:
		for versionItem in versionsForInsecure:
			if (sys.version_info.major == versionItem[0]):
				if sys.version_info < versionItem:
					parser.error("For --insecure minimum required python version is " +
								versionsForInsecureStr)
				break;

	return args

if __name__ == "__main__":
	args = parseArgs()

	for pkg in PACKAGES:
		if args.clean:
			pkg.clean()
		else:
			pkg.update(args.protocol)
