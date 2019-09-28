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
from khr_util.format import indentLines, normalizeConstant

TYPED_VALUES = [
	"EGL_DONT_CARE",
	"EGL_UNKNOWN",
	"EGL_NO_CONTEXT",
	"EGL_NO_DISPLAY",
	"EGL_DEFAULT_DISPLAY",
	"EGL_NO_SURFACE",
	"EGL_NO_IMAGE",
	"EGL_NO_SYNC",
	"EGL_NO_IMAGE_KHR",
	"EGL_NO_SYNC_KHR"
]

def enumValue (enum, typePrefix = ""):
	if enum.name in TYPED_VALUES:
          # incoming is EGL_CAST(<type>, <value>)
          n,v = enum.value.split(",", 1)
          # output is ((<typePrefix>::<type>)<value>)
          return "((%s%s)%s" % (typePrefix, n.replace("EGL_CAST(", ""), v)

	else:
		return normalizeConstant(enum.value)

def enumDefinition (enum):
	return "#define %s\t%s" % (enum.name, enumValue(enum, "eglw::"))

def gen (iface):
	writeInlFile(os.path.join(EGL_WRAPPER_DIR, "eglwEnums.inl"), indentLines(map(enumDefinition, iface.enums)))
