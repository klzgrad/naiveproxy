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
from khr_util.gen_str_util import genSetEnumUtilImpls, genQueryEnumUtilImpls

QUERY_NUM_OUT_ARGUMENTS = [

	("Basic", [
		("VIEWPORT",						4),
		("DEPTH_RANGE",						2),
		("SCISSOR_BOX",						4),
		("COLOR_WRITEMASK",					4),
		("ALIASED_POINT_SIZE_RANGE",		2),
		("ALIASED_LINE_WIDTH_RANGE",		2),
		("MAX_VIEWPORT_DIMS",				2),
		("MAX_COMPUTE_WORK_GROUP_COUNT",	3),
		("MAX_COMPUTE_WORK_GROUP_SIZE",		3),
		("PRIMITIVE_BOUNDING_BOX_EXT",		8),
		]),

	("Indexed", [
		("COLOR_WRITEMASK",				4),
		]),

	("Attribute", [
		("CURRENT_VERTEX_ATTRIB",		4),
		]),

	("Program", [
		("COMPUTE_WORK_GROUP_SIZE",		3),
		]),

	("TextureParam", [
		("TEXTURE_BORDER_COLOR",		4),
		]),
]

SET_NUM_IN_ARGUMENTS = [
	("TextureParam", [
		("TEXTURE_BORDER_COLOR",		4),
		]),
]


def addNamePrefix (prefix, groups):
	return [(groupName, [(prefix + queryName, querySize) for queryName, querySize in groupQueries]) for groupName, groupQueries in groups]

def genQueryUtil (iface):
	queryNumOutArgs = addNamePrefix("GL_", QUERY_NUM_OUT_ARGUMENTS);
	setNumInArgs    = addNamePrefix("GL_", SET_NUM_IN_ARGUMENTS);

	utilFile = os.path.join(OPENGL_DIR, "gluQueryUtil.inl")
	writeInlFile(utilFile, genQueryEnumUtilImpls(iface, queryNumOutArgs))

	utilFile = os.path.join(OPENGL_DIR, "gluCallLogUtil.inl")
	writeInlFile(utilFile, genSetEnumUtilImpls(iface, setNumInArgs))

if __name__ == "__main__":
	genQueryUtil(getHybridInterface())
