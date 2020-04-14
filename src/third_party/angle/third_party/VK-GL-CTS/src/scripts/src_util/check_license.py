# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2016 The Android Open Source Project
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

import sys
from common import isTextFile
from fnmatch import fnmatch

LICENSE_APACHE2  = 0
LICENSE_MIT      = 1
LICENSE_MULTIPLE = 2
LICENSE_UNKNOWN  = 3

LICENSE_KEYS	= [
	# \note Defined this way to avoid triggering license check error on this file
	("P" + "ermission is hereby granted, free of charge",    LICENSE_MIT),
	("L" + "icensed under the Apache License, Version 2.0",  LICENSE_APACHE2),
]

SOURCE_FILES	= ["*.py", "*.java", "*.c", "*.h", "*.cpp", "*.hpp"]

def readFile (file):
	f = open(file, 'rt')
	c = f.read()
	f.close()
	return c

def getFileLicense (file):
	contents	= readFile(file)
	detected	= LICENSE_UNKNOWN

	for searchStr, license in LICENSE_KEYS:
		if contents.find(searchStr) != -1:
			if detected != LICENSE_UNKNOWN:
				detected = LICENSE_MULTIPLE
			else:
				detected = license

	return detected

def checkFileLicense (file):
	license = getFileLicense(file)

	if license == LICENSE_MIT:
		print("%s: contains MIT license" % file)
	elif license == LICENSE_MULTIPLE:
		print("%s: contains multiple licenses" % file)
	elif license == LICENSE_UNKNOWN:
		print("%s: missing/unknown license" % file)

	return license == LICENSE_APACHE2

def isSourceFile (file):
	for ptrn in SOURCE_FILES:
		if fnmatch(file, ptrn):
			return True
	return False

def checkLicense (files):
	error = False
	for file in files:
		if isTextFile(file) and isSourceFile(file):
			if not checkFileLicense(file):
				error = True

	return not error
