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

def commandInitStatement (command):
	return "gl->%s\t= (%s)\tloader->get(\"%s\");" % (
		getFunctionMemberName(command.name),
		getFunctionTypeName(command.name),
		command.name)

def genFuncInit (registry):
	def check(api, version):
		if api == 'gl' and version >= "3.0":
			return 'core'
		return api == 'gles2'

	genCommandLists(registry, commandInitStatement,
					check		= check,
					directory	= OPENGL_INC_DIR,
					filePattern	= "glwInit%s.inl",
					align		= True)

if __name__ == "__main__":
	genFuncInit(getGLRegistry())
