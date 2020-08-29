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

from egl.common import *
from khr_util.format import commandParams

def commandTypedefDecl (command):
	return "typedef EGLW_APICALL %s\t(EGLW_APIENTRY* %s)\t(%s);" % (
		command.type,
		getFunctionTypeName(command.name),
		commandParams(command))

def commandMemberDecl (command):
	return "%s\t%s;" % (getFunctionTypeName(command.name),
						getFunctionMemberName(command.name))

def gen (iface):
	genCommandList(iface, commandTypedefDecl, EGL_WRAPPER_DIR, "eglwFunctionTypes.inl", True)
	genCommandList(iface, commandMemberDecl, EGL_WRAPPER_DIR, "eglwFunctions.inl", True)
