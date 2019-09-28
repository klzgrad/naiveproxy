# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright (c) 2017 The Khronos Group Inc.
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
import codecs
from optparse import OptionParser

FILE_PATTERNS		= ["*.hpp", "*.h", "*.cpp", "*.py"]
IGNORE_FILES		= set()
CHECK_END_COMMENT	= True

def hasBOM (file):
	with open(file, 'rb') as f:
		line0 = f.readline()
		if line0.startswith(codecs.BOM_UTF8):
			return True
	return False

def removeBOM (file):
	with open(file, 'r+b') as f:
		chunk = f.read(1024)
		if chunk.startswith(codecs.BOM_UTF8):
			chunk = chunk[3:]
		else:
			return
		readpos = 1024;
		writepos = 0;
		while chunk:
			f.seek(writepos, os.SEEK_SET)
			f.write(chunk)
			writepos += len(chunk)
			f.seek(readpos, os.SEEK_SET)
			chunk = f.read(1024)
			readpos += len(chunk)
		f.truncate(readpos-3)

def getFileList (path):
	if os.path.isfile(path):
		yield path
	elif os.path.isdir(path):
		for root, dirs, files in os.walk(path):
			for file in files:
				yield os.path.join(root, file)

def checkBOMs (files, fix):
	correct = True
	for file in files:
		if hasBOM(file):
			if fix:
				removeBOM(file)
				print("File %s contained BOM and was fixed" % file)
			else:
				correct = False
				print("File %s contains BOM" % file)
	return correct

if __name__ == "__main__":
	parser = OptionParser()
	parser.add_option("-x", "--fix", action="store_true", dest="fix", default=False, help="attempt to fix BOMs")

	(options, args)	= parser.parse_args()
	fix				= options.fix

	print("Checking BOMs...")
	for dir in args:
		checkBOMs(getFileList(os.path.normpath(dir)), fix)