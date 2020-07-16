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
import string

from egl.common import *
from khr_util.format import indentLines

class LogSpec:
	def __init__ (self, argInPrints, argOutPrints = {}, returnPrint = None):
		self.argInPrints	= argInPrints
		self.argOutPrints	= argOutPrints
		self.returnPrint	= returnPrint

def enum (group):
	return lambda name: "get%sStr(%s)" % (group, name)

def pointer (size):
	return lambda name: "getPointerStr(%s, %s)" % (name, size)

def enumPointer (group, size):
	return lambda name: "getEnumPointerStr<%(nameFunc)s>(%(name)s, %(size)s)" % {"name": name, "size": size, "nameFunc": ("get%sName" % group)}

def configAttrib (attribNdx):
	return lambda name: "getConfigAttribValueStr(param%d, %s)" % (attribNdx, name)

# Special rules for printing call arguments
CALL_LOG_SPECS = {
	"eglBindAPI":				LogSpec({0: enum("API")}),
	"eglChooseConfig":			LogSpec({1: lambda n: "getConfigAttribListStr(%s)" % n}, argOutPrints = {2: pointer("(num_config && returnValue) ? deMin32(config_size, *num_config) : 0"), 4: lambda n: "(%s ? de::toString(*%s) : \"NULL\")" % (n, n)}),
	"eglCreateContext":			LogSpec({3: lambda n: "getContextAttribListStr(%s)" % n}),
	"eglCreatePbufferSurface":	LogSpec({2: lambda n: "getSurfaceAttribListStr(%s)" % n}),
	"eglCreatePixmapSurface":	LogSpec({3: lambda n: "getSurfaceAttribListStr(%s)" % n}),
	"eglCreateWindowSurface":	LogSpec({3: lambda n: "getSurfaceAttribListStr(%s)" % n}),
	"eglGetError":				LogSpec({}, returnPrint = enum("Error")),
	"eglGetConfigAttrib":		LogSpec({2: enum("ConfigAttrib")}, argOutPrints = {3: lambda n: "getConfigAttribValuePointerStr(attribute, %s)" % n}),
	"eglGetCurrentSurface":		LogSpec({0: enum("SurfaceTarget")}),
	"eglGetProcAddress":		LogSpec({}, returnPrint = lambda n: "tcu::toHex(%s)" % (n)),
	"eglQueryAPI":				LogSpec({}, returnPrint = enum("API")),
	"eglQueryContext":			LogSpec({2: enum("ContextAttrib")}, argOutPrints = {3: lambda n: "getContextAttribValuePointerStr(attribute, %s)" % n}),
	"eglQuerySurface":			LogSpec({2: enum("SurfaceAttrib")}, argOutPrints = {3: lambda n: "getSurfaceAttribValuePointerStr(attribute, %s)" % n}),
	"eglSurfaceAttrib":			LogSpec({2: enum("SurfaceAttrib"), 3: lambda n: "getSurfaceAttribValueStr(attribute, %s)" % n}),
}

def eglwPrefix (string):
	if string[:5] == "__egl":
		return "eglw::" + string
	else:
		return re.sub(r'\bEGL', 'eglw::EGL', string)

def prefixedParams (command):
	if len(command.params) > 0:
		return ", ".join(eglwPrefix(param.declaration) for param in command.params)
	else:
		return "void"

def commandLogWrapperMemberDecl (command):
	return "%s\t%s\t(%s);" % (eglwPrefix(command.type), command.name, prefixedParams(command))

def getVarDefaultPrint (type, varName):
	if re.match(r'^const +char *\*$', type):
		return "getStringStr(%s)" % varName
	elif re.match(r'(EGLenum|EGLConfig|EGLSurface|EGLClientBuffer|EGLNativeDisplayType|EGLNativeWindowType|EGLNativePixmapType|\*)$', type):
		return "toHex(%s)" % varName
	elif type == 'EGLBoolean':
		return "getBooleanStr(%s)" % varName
	else:
		return varName

def commandLogWrapperMemberDef (command):
	src = ""
	try:
		logSpec = CALL_LOG_SPECS[command.name]
	except KeyError:
		logSpec = None

	src += "\n"
	src += "%s CallLogWrapper::%s (%s)\n{\n" % (eglwPrefix(command.type), command.name, ", ".join(eglwPrefix(p.declaration) for p in command.params))

	# Append paramemetrs
	callPrintItems = ["\"%s(\"" % command.name]
	for paramNdx, param in enumerate(command.params):
		if paramNdx > 0:
			callPrintItems.append("\", \"")

		if logSpec and paramNdx in logSpec.argInPrints:
			callPrintItems.append(logSpec.argInPrints[paramNdx](param.name))
		else:
			callPrintItems.append(getVarDefaultPrint(param.type, param.name))

	callPrintItems += ["\");\"", "TestLog::EndMessage"]

	src += "\tif (m_enableLog)\n"
	src += "\t\tm_log << TestLog::Message << %s;\n" % " << ".join(callPrintItems)

	callStr = "m_egl.%s(%s)" % (getFunctionMemberName(command.name), ", ".join([p.name for p in command.params]))

	isVoid	= command.type == 'void'
	if isVoid:
		src += "\t%s;\n" % callStr
	else:
		src += "\t%s returnValue = %s;\n" % (eglwPrefix(command.type), callStr)

	if logSpec and len(logSpec.argOutPrints) > 0:
		# Print values returned in pointers
		src += "\tif (m_enableLog)\n\t{\n"

		for paramNdx, param in enumerate(command.params):
			if paramNdx in logSpec.argOutPrints:
				src += "\t\tm_log << TestLog::Message << \"// %s = \" << %s << TestLog::EndMessage;\n" % (param.name, logSpec.argOutPrints[paramNdx](param.name))

		src += "\t}\n"

	if not isVoid:
		# Print return value
		returnPrint = getVarDefaultPrint(command.type, "returnValue")
		if logSpec and logSpec.returnPrint:
			returnPrint = logSpec.returnPrint("returnValue")

		src += "\tif (m_enableLog)\n"
		src += "\t\tm_log << TestLog::Message << \"// \" << %s << \" returned\" << TestLog::EndMessage;\n" % returnPrint
		src += "\treturn returnValue;\n"

	src += "}"
	return src

def gen (iface):
	genCommandList(iface, commandLogWrapperMemberDecl, EGL_DIR, "egluCallLogWrapperApi.inl", True)
	genCommandList(iface, commandLogWrapperMemberDef, EGL_DIR, "egluCallLogWrapper.inl", False)
