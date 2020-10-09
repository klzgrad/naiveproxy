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
from itertools import chain

INL_HEADER_TMPL = """\
/* WARNING: This is auto-generated file. Do not modify, since changes will
 * be lost! Modify the generating script instead.
 *
 * Generated from {registryName} revision {revision}.
 */\
"""

def genInlHeader (registryName, revision):
	return INL_HEADER_TMPL.format(
		registryName	= registryName,
		revision		= str(revision))

def genInlHeaderForSource (registrySource):
	return genInlHeaderForSource(registrySource.getFilename(), registrySource.getRevision())

def nextMod (val, mod):
	if val % mod == 0:
		return val + mod
	else:
		return int(val/mod)*mod + mod

def indentLines (lines):
	tabSize = 4

	# Split into columns
	lineColumns = [line.split("\t") for line in lines if line is not None]
	if len(lineColumns) == 0:
		return

	numColumns = max(len(line) for line in lineColumns)

	# Figure out max length per column
	columnLengths = [nextMod(max(len(line[ndx]) for line in lineColumns if len(line) > ndx), tabSize) for ndx in range(numColumns)]

	for line in lineColumns:
		indented = []
		for columnNdx, col in enumerate(line[:-1]):
			colLen	= len(col)
			while colLen < columnLengths[columnNdx]:
				col		+= "\t"
				colLen	 = nextMod(colLen, tabSize)
			indented.append(col)

		# Append last col
		indented.append(line[-1])
		yield "".join(indented)

def readFile (filename):
	f = open(filename, 'rb')
	data = f.read()
	f.close()
	return data

def writeFileIfChanged (filename, data):
	if not os.path.exists(filename) or readFile(filename) != data:
		if (sys.version_info < (3, 0)):
			f = open(filename, 'wt')
		else:
			f = open(filename, 'wt', newline='\n')
		f.write(data)
		f.close()

def writeLines (filename, lines):
	text = ""
	for line in lines:
		text += line
		text += "\n"

	writeFileIfChanged(filename, text)
	print(filename)

def writeInlFile (filename, header, source):
	writeLines(filename, chain([header], source))

def normalizeConstant (constant):
	value = int(constant, base=0)
	if value >= 1 << 63:
		suffix = 'ull'
	elif value >= 1 << 32:
		suffix = 'll'
	elif value >= 1 << 31:
		suffix = 'u'
	else:
		suffix = ''
	return constant + suffix

def commandParams (command):
	if len(command.params) > 0:
		return ", ".join(param.declaration for param in command.params)
	else:
		return "void"

def commandArgs (command):
	return ", ".join(param.name for param in command.params)
