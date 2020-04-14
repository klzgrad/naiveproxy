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

from src_util import *

def getExtensionCommands (registry, iface, api, extension):
	commands = []

	# Build interface with just the single extension and no core APIs.
	# Aliases will not be set in this iface so we will use it only for
	# slicing the complete (hybrid) iface.
	extIface = getInterface(registry, api, version=False, profile='core', extensionNames=[extension])
	if not extIface.commands:
		return commands

	cmdMap = {}
	for command in iface.commands:
		cmdMap[command.name] = command

	for extCommand in extIface.commands:
		assert extCommand.name in cmdMap
		commands.append(cmdMap[extCommand.name])

	return commands

def genExtensions (registry, iface, api):
	for extName in EXTENSIONS:
		extCommands = getExtensionCommands(registry, iface, api, extName)
		if len(extCommands) == 0:
			continue # Not applicable for this api

		yield ""
		yield "if (de::contains(extSet, \"%s\"))" % extName
		yield "{"

		def genInit (command):
			ifaceName = command.alias.name if command.alias else command.name
			return "gl->%s\t= (%s)\tloader->get(\"%s\");" % (
				getFunctionMemberName(ifaceName),
				getFunctionTypeName(ifaceName),
				command.name)

		for line in indentLines(genInit(command) for command in extCommands):
			yield "\t" + line

		yield "}"

def genExtInit (registry, iface):
	nonStrippedIface = getHybridInterface(stripAliasedExtCommands = False)

	writeInlFile(os.path.join(OPENGL_INC_DIR, "glwInitExtES.inl"), genExtensions(registry, nonStrippedIface, 'gles2'))
	writeInlFile(os.path.join(OPENGL_INC_DIR, "glwInitExtGL.inl"), genExtensions(registry, nonStrippedIface, 'gl'))

if __name__ == '__main__':
	genExtInit(getGLRegistry(), getHybridInterface())
