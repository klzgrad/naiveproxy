#!/usr/bin/python3
#
# Copyright (c) 2018 The Khronos Group Inc.
# Copyright (c) 2018 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import xml.etree.ElementTree as ET
import argparse
import os
import sys
import subprocess
import importlib

currentheader = ""
baseextensions = {}
supersetextensions = []
sources = [
	"cgenerator.py",
	"generator.py",
	"reg.py",
	"vk.xml"
	]

def get_current_header(header):
	global currentheader
	if os.path.exists(header):
		currentheader = open(header).read()

def get_spec_ver(n):
	specver = "?"
	req = n.findall("require")
	if len(req) > 0:
		enum = req[0].findall("enum")
		if len(enum) > 0:
			for y in enum:
				if "_SPEC_VERSION" in y.get("name"):
					specver = y.get("value")
	return specver

def get_base_extensions(xmlfile):
	global baseextensions
	if os.path.exists(xmlfile):
		missing = []
		tree = ET.parse(xmlfile)
		root = tree.getroot()
		extroot = root.findall("extensions")
		ext = []
		if len(extroot) > 0 :
			ext = extroot[0].getchildren()
		for x in ext:
			name = x.get("name")
			specver = get_spec_ver(x)
			if specver not in "0:":
				baseextensions[name] = specver

			if name not in currentheader:
				if specver not in "0?":
					missing.append(name)
			else:
				if specver is "0":
					print ("!! Warning: Current header contains extension with version 0:", name)
				if specver is "?":
					print ("!! Warning: Current header contains extension with unknown version:", name)
		if len(missing) > 0:
			print ("!! Warning: current header does not include following base extension(s)")
			for x in missing:
				print ("!!  ", x)
			print ("!! These will be included in generated header.")

def parse_superset_extensions(xmlfile):
	global supersetextensions
	global baseextensions
	if os.path.exists(xmlfile):
		tree = ET.parse(xmlfile)
		root = tree.getroot()
		extroot = root.findall("extensions")
		ext = []
		if len(extroot) > 0 :
			ext = extroot[0].getchildren()
		for x in ext:
			name = x.get("name")
			specver = get_spec_ver(x)
			if name in baseextensions:
				if baseextensions[name] != specver:
					print ("!! Warning: base and superset versions for extension", name, "differ: ", baseextensions[name], "!=", specver)
					print ("!! The superset version ", specver, " will be included in generated header.")
			else:
				if specver not in "0?":
					supersetextensions.append([name, name in currentheader, specver])

def print_menu():
	global supersetextensions
	index = 0
	print()
	for x in supersetextensions:
		print (index, ") Output:", x[1],"-", x[0], "(ver " + x[2] + ")")
		index += 1
	print ("q ) Quit without saving")
	print ("go ) Generate new header with selected superset extensions")


def generate(args):
	# Dynamically import generator functions (since we downloaded the scripts)
	sys.path.insert(0, os.getcwd())
	importlib.invalidate_caches()
	reg_py = importlib.import_module("reg")
	cgenerator_py = importlib.import_module("cgenerator")

	reg = reg_py.Registry()
	tree = ET.parse(args.supersetxml)
	reg.loadElementTree(tree)
	createGenerator = cgenerator_py.COutputGenerator

	# Copyright text prefixing all headers (list of strings).
	prefixStrings = [
		'/*',
		'** Copyright (c) 2015-2018 The Khronos Group Inc.',
		'**',
		'** Licensed under the Apache License, Version 2.0 (the "License");',
		'** you may not use this file except in compliance with the License.',
		'** You may obtain a copy of the License at',
		'**',
		'**     http://www.apache.org/licenses/LICENSE-2.0',
		'**',
		'** Unless required by applicable law or agreed to in writing, software',
		'** distributed under the License is distributed on an "AS IS" BASIS,',
		'** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.',
		'** See the License for the specific language governing permissions and',
		'** limitations under the License.',
		'*/',
		''
	]

	# Text specific to Vulkan headers
	vkPrefixStrings = [
		'/*',
		'** This header is generated from the Khronos Vulkan XML API Registry.',
		'** DO NOT EDIT MANUALLY. Use the following script to generate this file:',
		'** external/vulkancts/scripts/gen_vulkan_header.py',
		'*/',
		''
	]

	# Emit everything except the extensions specifically disabled
	removeExtensionsPat = ''
	for x in supersetextensions:
		if not x[1]:
			if removeExtensionsPat != '':
				removeExtensionsPat += '|'
			removeExtensionsPat += x[0]
	if removeExtensionsPat != '':
		removeExtensionsPat = "^(" + removeExtensionsPat + ")$"
	else:
		removeExtensionsPat = None

	options = cgenerator_py.CGeneratorOptions(
		filename          = args.header,
		directory         = '.',
		apiname           = 'vulkan',
		profile           = None,
		versions          = '.*',
		emitversions      = '.*',
		defaultExtensions = 'vulkan',
		addExtensions     = None,
		removeExtensions  = removeExtensionsPat,
		emitExtensions    = '.*',
		prefixText        = prefixStrings + vkPrefixStrings,
		genFuncPointers   = True,
		protectFile       = True,
		protectFeature    = False,
		protectProto      = '#ifndef',
		protectProtoStr   = 'VK_NO_PROTOTYPES',
		apicall           = 'VKAPI_ATTR ',
		apientry          = 'VKAPI_CALL ',
		apientryp         = 'VKAPI_PTR *',
		alignFuncParam    = 48)
	gen = createGenerator(diagFile=None)
	reg.setGenerator(gen)
	reg.apiGen(options)
	print("Done")

def cleanup(args):
	if args.nofetch or args.nocleanup:
		print("Skipping cleanup")
	else:
		for x in sources:
			if os.path.exists(x):
				os.remove(x)

def fetch_sources(args):
	if not args.nofetch:
		for x in sources:
			if os.path.exists(x):
				os.remove(x)
			command = ["wget", "https://raw.github.com/KhronosGroup/Vulkan-Docs/master/xml/" + x]
			if not args.noquietwget:
				command.append("--quiet")
			print("Fetching", x)
			subprocess.call(command)
			if not os.path.exists(x):
				print("!! Error: Could not fetch", x)
				if args.noquietwget:
					print("!! Re-run with -noquietwget for diagnostic information")
				quit()
	else:
		for x in sources:
			if not os.path.exists(x):
				print("!! Error: Can't find the file",x)
				print("!! please re-run without -skipfetch")
				quit()

if __name__ == '__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('-go', action='store_true',
						default=False,
						help='Enable execution.')
	parser.add_argument('-noquietwget', action='store_true',
						default=False,
						help='Let wget output diagnostic information.')
	parser.add_argument('-nofetch', action='store_true',
						default=False,
						help='Skip fetching required sources from github.')
	parser.add_argument('-nocleanup', action='store_true',
						default=False,
						help='Do not remove fetched files after run.')
	parser.add_argument('-basexml', action='store',
						default='vk.xml',
						help='Specify the base xml file with vulkan API information. Defaults to vk.xml.')
	parser.add_argument('-supersetxml', action='store',
						default='vk.xml',
						help='Specify the xml file with superset information. Defaults to vk.xml.')
	parser.add_argument('-header', action='store',
						default='external/vulkancts/scripts/src/vulkan.h.in',
						help='Specify the current header file. Defaults to external/vulkancts/scripts/src/invulkan.h.in.')
	args = parser.parse_args()

	if not args.go:
		print(
"""
This script is used to generate the Vulkan header file for the Vulkan CTS from
the vk.xml specification. It can optionally take a superset XML file and the
user can interactively select which of the superset extensions to use in
generation.

The script automatically fetches the current vk.xml as well as the header
generation scripts from github.

For help with options, run with -h. To execute the script, run with -go.
""")
		quit()

	fetch_sources(args)

	if not os.path.exists(args.basexml):
		print("!! Error: Can't find base xml file", args.basexml)
		quit()
	if not os.path.exists(args.supersetxml):
		print("!! Error: Can't find superset xml file", args.supersetxml)
		quit()
	if not os.path.exists(args.header):
		print("!! Error: Can't find header file", args.header)
		quit()

	get_current_header(args.header)
	get_base_extensions(args.basexml)
	parse_superset_extensions(args.supersetxml)

	while True:
		print_menu()
		i = input("Option: ")
		if i != "":
			if i in "qQ":
				print ("Quiting without changes")
				cleanup(args)
				quit()
			if i == "go":
				print ("Generating new header")
				generate(args)
				cleanup(args)
				quit()
			if not i.isdigit() or int(i) >= len(supersetextensions):
				print ("Invalid input '"+i+"'")
			else:
				supersetextensions[int(i)][1] = not supersetextensions[int(i)][1]
