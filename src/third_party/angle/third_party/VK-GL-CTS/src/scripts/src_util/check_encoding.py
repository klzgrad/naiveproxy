# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2019 The Khronos Group Inc.
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

import sys
from functools import partial
from argparse import ArgumentParser
from common import getChangedFiles, getAllProjectFiles, isTextFile

EXCLUSION_LIST = [
	"/doc/testspecs/VK/apitests.adoc",
	"/targets/default/FindWayland.cmake",
]

def checkEnds(line, ends):
	return any(line.endswith(end) for end in ends)

def hexDumpFromFile(filename, position, size):
	print("Invalid symbol(s) at offset %x (%i), byte number: %i" % (position, position, size))
	columnWidth = 32
	size += position % columnWidth
	position = columnWidth * int(position / columnWidth)
	size = columnWidth * int((size + columnWidth - 1) / columnWidth)

	f = open(filename, 'rb')
	f.seek(position)

	out1 = ""
	out2 = ""
	numBytes = 0
	while numBytes < size:
		if numBytes % columnWidth == 0:
			if len(out1) != 0:
				print(out1 + "	" + out2)
				out1 = ""
				out2 = ""
			out1 += "%0.8X: " % + (position + numBytes)

		byte = f.read(1)
		if (byte == None):
			break;

		if (sys.version_info < (3, 0)):
			byte = int(ord(byte[0]))
		else:
			byte = int(byte[0])

		numBytes += 1

		out1 += "%0.2X " % byte
		if (byte >= 32 and byte <=127):
			out2 += chr(byte)
		else:
			out2 += '.'

	if len(out1) != 0:
		print(out1 + "	" + out2)

	f.close()

def SearchInvalidSymbols (filename):
	start = None
	end = None
	with open(filename, 'rb') as file:
		for byte in iter(partial(file.read, 1), b''):
			if (sys.version_info < (3, 0)):
				byte = int(ord(byte[0]))
			else:
				byte = int(byte[0])
			if (byte > 0x7F):
				if start == None:
					start = file.tell()
			else:
				if start != None:
					end = file.tell()
			if end != None:
				hexDumpFromFile(filename, start, end - start)
				start = None
				end = None
		if start != None:
			file.seek(0, 2) # Seek to end of file
			end = file.tell()
			hexDumpFromFile(filename, start, end - start)

def checkFileEncoding (filename):
	generalEncoding = "ascii"
	file = None
	error = False
	try:
		if (sys.version_info < (3, 0)):
			file = open(filename, 'rt')
			for line in file:
				line.decode(generalEncoding)
		else:
			file = open(filename, 'rt', encoding=generalEncoding)
			for bytes in iter(partial(file.read, 1024 * 1024), ''):
				pass
	except UnicodeDecodeError as e:
		if not checkEnds(filename.replace("\\", "/"), EXCLUSION_LIST):
			error = True
			print("")
			print("Unicode error in file: %s (%s)" % (filename, e))
			SearchInvalidSymbols(filename)
	finally:
		if file != None:
			file.close()

	return not error

def checkEncoding (files):
	error = False
	for file in files:
		if isTextFile(file):
			if not checkFileEncoding(file):
				error = True

	return not error

if __name__ == "__main__":
	parser = ArgumentParser()
	parser.add_argument("-e", "--only-errors",  action="store_true", dest="onlyErrors",   default=False, help="Print only on error")
	parser.add_argument("-i", "--only-changed", action="store_true", dest="useGitIndex",  default=False, help="Check only modified files. Uses git.")

	args = parser.parse_args()

	if args.useGitIndex:
		files = getChangedFiles()
	else:
		files = getAllProjectFiles()

	error = not checkEncoding(files)

	if error:
		print("One or more checks failed")
		sys.exit(1)
	if not args.onlyErrors:
		print("All checks passed")
