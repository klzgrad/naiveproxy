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

from src_util import *

def enumDefinition (enum):
	return "#define %s\t%s" % (enum.name, normalizeConstant(enum.value))

def genEnums (iface):
	src = indentLines(map(enumDefinition, iface.enums))
	writeInlFile(os.path.join(OPENGL_INC_DIR, "glwEnums.inl"), src)

if __name__ == "__main__":
	import logging, sys
	logging.basicConfig(stream=sys.stderr, level=logging.INFO)
	genEnums(getHybridInterface())
