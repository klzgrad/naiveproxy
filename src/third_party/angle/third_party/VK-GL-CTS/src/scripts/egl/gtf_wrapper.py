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
from egl.common import *
from egl.enums import enumValue
from egl.library import getExtOnlyIface
from khr_util.format import indentLines
from itertools import chain

try:
	from itertools import imap
except ImportError:
	imap=map

def getMangledName (funcName):
	assert funcName[:3] == "egl"
	return "eglw" + funcName[3:]

def commandAliasDefinition (command):
	return "#define\t%s\t%s" % (command.name, getMangledName(command.name))

def commandWrapperDeclaration (command):
	return "%s\t%s\t(%s);" % (
		command.type,
		getMangledName(command.name),
		", ".join([param.declaration for param in command.params]))

NATIVE_TYPES = [
	"EGLNativeWindowType",
	"EGLNativeDisplayType",
	"EGLNativePixmapType",
]

def commandWrapperDefinition (command):
	template = """
{returnType} {mangledName} ({paramDecls})
{{
	const eglw::Library* egl = eglw::getCurrentThreadLibrary();
	if (!egl)
		return{defaultReturn};
	{maybeReturn}egl->{memberName}({arguments});
}}"""

	arguments = []

	for param in command.params:
		if param.type in NATIVE_TYPES:
			arguments.append("(void*)" + param.name)
		else:
			arguments.append(param.name)

	return template.format(
		returnType		= command.type,
		mangledName		= "eglw" + command.name[3:],
		paramDecls		= commandParams(command),
		defaultReturn	= " " + getDefaultReturn(command) if command.type != 'void' else "",
		maybeReturn		= "return " if command.type != 'void' else "",
		memberName		= getFunctionMemberName(command.name),
		arguments		= ", ".join(arguments))

def getDefaultReturn (command):
	if command.name == "glGetError":
		return "GL_INVALID_OPERATION"
	else:
		assert command.type != 'void'
		return "(%s)0" % command.type

commandParams = khr_util.format.commandParams

def enumDefinitionC (enum):
	return "#define %s\t%s" % (enum.name, enumValue(enum))

def gen (registry):
	noExtIface		= getInterface(registry, 'egl', VERSION)
	extOnlyIface	= getExtOnlyIface(registry, 'egl', EXTENSIONS)
	defaultIface	= getDefaultInterface()
	defines			= imap(commandAliasDefinition, defaultIface.commands)
	prototypes		= imap(commandWrapperDeclaration, defaultIface.commands)
	src				= indentLines(chain(defines, prototypes))

	writeInlFile(os.path.join(EGL_WRAPPER_DIR, "eglwApi.inl"), src)
	writeInlFile(os.path.join(EGL_WRAPPER_DIR, "eglwEnumsC.inl"), indentLines(map(enumDefinitionC, defaultIface.enums)))
	genCommandList(noExtIface, commandWrapperDefinition, EGL_WRAPPER_DIR, "eglwImpl.inl")
	genCommandList(extOnlyIface, commandWrapperDefinition, EGL_WRAPPER_DIR, "eglwImplExt.inl")

