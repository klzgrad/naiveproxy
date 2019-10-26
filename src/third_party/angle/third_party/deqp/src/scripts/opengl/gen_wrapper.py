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
from src_util import *
from itertools import chain

try:
	from itertools import imap
except ImportError:
	imap=map

def getMangledName (funcName):
	assert funcName[:2] == "gl"
	return "glw" + funcName[2:]

def commandAliasDefinition (command):
	return "#define\t%s\t%s" % (command.name, getMangledName(command.name))

def commandWrapperDeclaration (command):
	return "%s\t%s\t(%s);" % (
		command.type,
		getMangledName(command.name),
		", ".join([param.declaration for param in command.params]))

def genWrapperHeader (iface):
	defines = imap(commandAliasDefinition, iface.commands)
	prototypes = imap(commandWrapperDeclaration, iface.commands)
	src = indentLines(chain(defines, prototypes))
	writeInlFile(os.path.join(OPENGL_INC_DIR, "glwApi.inl"), src)

def getDefaultReturn (command):
	if command.name == "glGetError":
		return "GL_INVALID_OPERATION"
	else:
		assert command.type != 'void'
		return "(%s)0" % command.type

def commandWrapperDefinition (command):
	template = """
{returnType} {mangledName} ({paramDecls})
{{
	const glw::Functions* gl = glw::getCurrentThreadFunctions();
	if (!gl)
		return{defaultReturn};
	{maybeReturn}gl->{memberName}({arguments});
}}"""
	return template.format(
		returnType		= command.type,
		mangledName		= getMangledName(command.name),
		paramDecls		= commandParams(command),
		defaultReturn	= " " + getDefaultReturn(command) if command.type != 'void' else "",
		maybeReturn		= "return " if command.type != 'void' else "",
		memberName		= getFunctionMemberName(command.name),
		arguments		= commandArgs(command))

def genWrapperImplementation (iface):
	genCommandList(iface, commandWrapperDefinition, OPENGL_INC_DIR, "glwImpl.inl")

def genWrapper (iface):
	genWrapperHeader(iface)
	genWrapperImplementation(iface)

if __name__ == "__main__":
	genWrapper(getHybridInterface())
