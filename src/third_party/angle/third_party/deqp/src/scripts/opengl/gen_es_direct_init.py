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

def commandDirectInitStatement (command):
	# Workaround for broken headers
	if command.name == "glShaderSource":
		cast = "(%s)" % getFunctionTypeName(command.name)
	else:
		cast = ""
	return "gl->%s\t= %s&%s;" % (getFunctionMemberName(command.name),
								 cast,
								 command.name)

def genESDirectInit (registry):
	genCommandLists(registry, commandDirectInitStatement,
					check		= lambda api, _: api == 'gles2',
					directory	= OPENGL_INC_DIR,
					filePattern	= "glwInit%sDirect.inl",
					align		= True)

if __name__ == "__main__":
	genESDirectInit(getGLRegistry())
