# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2017 The Android Open Source Project
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

def apiVersionDefinition(version):
	return "#define %s\t1" % (version)

def genVersions (iface):
	src = indentLines(map(apiVersionDefinition, iface.versions))
	writeInlFile(os.path.join(OPENGL_INC_DIR, "glwVersions.inl"), src)

if __name__ == "__main__":
	import logging, sys
	logging.basicConfig(stream=sys.stderr, level=logging.INFO)
	genVersions(getHybridInterface())
