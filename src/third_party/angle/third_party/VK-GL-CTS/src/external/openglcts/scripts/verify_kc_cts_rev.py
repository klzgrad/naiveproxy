# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Khronos OpenGL CTS
# ------------------
#
# Copyright (c) 2016 The Khronos Group Inc.
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
import argparse
import subprocess

sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
from fetch_kc_cts import SHA1

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))
from build.common import *


EXTERNAL_DIR    = os.path.realpath(os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..")))

def computeChecksum (data):
	return hashlib.sha256(data).hexdigest()

class Source:
	def __init__(self, baseDir, extractDir):
		self.baseDir		= baseDir
		self.extractDir		= extractDir

	def clean (self):
		fullDstPath = os.path.join(EXTERNAL_DIR, self.baseDir, self.extractDir)
		if os.path.exists(fullDstPath):
			shutil.rmtree(fullDstPath, ignore_errors=False)

class GitRepo (Source):
	def __init__(self, url, revision, baseDir, extractDir = "src"):
		Source.__init__(self, baseDir, extractDir)
		self.url		= url
		self.revision	= revision

	def update (self):
		fullDstPath = os.path.join(EXTERNAL_DIR, self.baseDir, self.extractDir)

		if not os.path.exists(fullDstPath):
			execute(["git", "clone", "--no-checkout", self.url, fullDstPath])

		pushWorkingDir(fullDstPath)
		try:
			execute(["git", "fetch", self.url, "+refs/heads/*:refs/remotes/origin/*"])
			execute(["git", "checkout", self.revision])
		finally:
			popWorkingDir()
	def compare_rev(self):
		fullDstPath = os.path.join(EXTERNAL_DIR, self.baseDir, self.extractDir)
		pushWorkingDir(fullDstPath)
		try:
			out = subprocess.check_output(["git", "rev-parse", "HEAD"])
			if out.replace('\n', '') != SHA1:
				raise Exception ("KC CTS checkout revision %s in external/fetch_kc_cts.py doesn't match KC CTS master HEAD revision %s" % (SHA1, out))
		finally:
			popWorkingDir()

PACKAGES = [
	GitRepo(
		"git@gitlab.khronos.org:opengl/kc-cts.git",
		"HEAD",
		"kc-cts"),
]

if __name__ == "__main__":
	for pkg in PACKAGES:
		pkg.update()
		pkg.compare_rev()
