# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Khronos OpenGL CTS
# ------------------
#
# Copyright (c) 2016 The Khronos Group Inc.
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
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), "verify"))

from summary import *
from verify_es import verifyESSubmission
from verify_gl import verifyGLSubmission

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("%s: [directory]" % sys.argv[0])
		sys.exit(-1)

	summary	= parseRunSummary(os.path.join(sys.argv[1], "cts-run-summary.xml"))
	if "es" in summary.type:
		verifyESSubmission(sys.argv)
	else:
		assert "gl" in summary.type
		verifyGLSubmission(sys.argv)

