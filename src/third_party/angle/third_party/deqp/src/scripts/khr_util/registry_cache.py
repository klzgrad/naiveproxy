# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2015-2017 The Android Open Source Project
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
import urllib2
import hashlib

import registry

sys.path.append(os.path.join(os.path.dirname(__file__), ".."))

from build.common import *

BASE_URL = ""

class RegistrySource:
	def __init__(self, repository, filename, revision, checksum):
		self.repository	= repository
		self.filename	= filename
		self.revision	= revision
		self.checksum	= checksum

	def __hash__(self):
		return hash((self.repository, self.filename, self.revision, self.checksum))

	def __eq__(self, other):
		return (self.repository, self.filename, self.revision, self.checksum) == (other.repository, other.filename, other.revision, other.checksum)

	def getFilename (self):
		return os.path.basename(self.filename)

	def getCacheFilename (self):
		return "r%s-%s" % (self.revision, self.getFilename())

	def getChecksum (self):
		return self.checksum

	def getRevision (self):
		return self.revision

	def getRepo (self):
		return self.repository

	def getRevision (self):
		return self.revision

	def getFilename (self):
		return self.filename

def computeChecksum (data):
	return hashlib.sha256(data).hexdigest()

def makeSourceUrl (repository, revision, filename):
	return "%s/%s/%s" % (repository, revision, filename)

def checkoutGit (repository, revision, fullDstPath):
	if not os.path.exists(fullDstPath):
		execute(["git", "clone", "--no-checkout", repository, fullDstPath])

	pushWorkingDir(fullDstPath)
	try:
		execute(["git", "fetch", repository, "+refs/heads/*:refs/remotes/origin/*"])
		execute(["git", "checkout", revision])
	finally:
		popWorkingDir()

def checkoutFile (repository, revision, filename, cacheDir):
	try:
		req		= urllib2.urlopen(makeSourceUrl(repository, revision, filename))
		data	= req.read()
	except IOError:
		fullDstPath = os.path.join(cacheDir, "git")

		checkoutGit(repository, revision, fullDstPath)
		f		= open(os.path.join(fullDstPath, filename), "r")
		data	= f.read()
		f.close()
	except:
		print "Unexpected error:", sys.exc_info()[0]

	return data

def fetchFile (dstPath, repository, revision, filename, checksum, cacheDir):
	def writeFile (filename, data):
		f = open(filename, 'wb')
		f.write(data)
		f.close()

	if not os.path.exists(os.path.dirname(dstPath)):
		os.makedirs(os.path.dirname(dstPath))

	print "Fetching %s/%s@%s" % (repository, filename, revision)
	data		= checkoutFile(repository, revision, filename, cacheDir)
	gotChecksum	= computeChecksum(data)

	if checksum != gotChecksum:
		raise Exception("Checksum mismatch, expected %s, got %s" % (checksum, gotChecksum))

	writeFile(dstPath, data)

def checkFile (filename, checksum):
	def readFile (filename):
		f = open(filename, 'rb')
		data = f.read()
		f.close()
		return data

	if os.path.exists(filename):
		return computeChecksum(readFile(filename)) == checksum
	else:
		return False

g_registryCache = {}

def getRegistry (source):
	global g_registryCache

	if source in g_registryCache:
		return g_registryCache[source]

	cacheDir	= os.path.join(os.path.dirname(__file__), "cache")
	cachePath	= os.path.join(cacheDir, source.getCacheFilename())

	if not checkFile(cachePath, source.checksum):
		fetchFile(cachePath, source.getRepo(), source.getRevision(), source.getFilename(), source.getChecksum(), cacheDir)

	parsedReg	= registry.parse(cachePath)

	g_registryCache[source] = parsedReg

	return parsedReg
