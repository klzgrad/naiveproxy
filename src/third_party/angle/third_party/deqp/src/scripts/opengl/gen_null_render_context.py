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

# Functions that have special implementation
OVERRIDE_FUNCS = set([
	"glGetError",
	"glGetIntegerv",
	"glGetBooleanv",
	"glGetFloatv",
	"glGetString",
	"glGetStringi",
	"glCreateShader",
	"glCreateProgram",
	"glGetShaderiv",
	"glGetProgramiv",
	"glGenTextures",
	"glGenQueries",
	"glGenBuffers",
	"glGenRenderbuffers",
	"glGenFramebuffers",
	"glGenVertexArrays",
	"glGenSamplers",
	"glGenTransformFeedbacks",
	"glGenProgramPipelines",
	"glGetInternalformativ",
	"glMapBufferRange",
	"glCheckFramebufferStatus",
	"glReadPixels",
	"glBindBuffer",
	"glDeleteBuffers",
	"glGetAttribLocation",
])

NULL_PLATFORM_DIR = os.path.normpath(os.path.join(SCRIPTS_DIR, "..", "..", "framework", "platform", "null"))

def commandDummyImpl (command):
	if command.name in OVERRIDE_FUNCS:
		return None
	template = """
GLW_APICALL {returnType} GLW_APIENTRY {commandName} ({paramDecls})
{{
{body}{maybeReturn}
}}"""
	return template.format(
		returnType	= command.type,
		commandName	= command.name,
		paramDecls	= commandParams(command),
		body		= ''.join("\tDE_UNREF(%s);\n" % p.name for p in command.params),
		maybeReturn = "\n\treturn (%s)0;" % command.type if command.type != 'void' else "")

def commandInitStatement (command):
	return "gl->%s\t= %s;" % (getFunctionMemberName(command.name), command.name)

def genNullRenderContext (iface):
	genCommandList(iface, commandInitStatement,
				   directory	= NULL_PLATFORM_DIR,
				   filename		= "tcuNullRenderContextInitFuncs.inl",
				   align		= True)
	genCommandList(iface, commandDummyImpl,
				   directory	= NULL_PLATFORM_DIR,
				   filename		= "tcuNullRenderContextFuncs.inl")

if __name__ == "__main__":
	genNullRenderContext(getHybridInterface())
