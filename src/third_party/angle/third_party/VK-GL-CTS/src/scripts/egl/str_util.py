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
from khr_util.gen_str_util import addValuePrefix, genStrUtilProtos, genStrUtilImpls

# Bitfield mapping
BITFIELD_GROUPS = [
	("APIBits", [
		"OPENGL_BIT",
		"OPENGL_ES_BIT",
		"OPENGL_ES2_BIT",
		"OPENGL_ES3_BIT_KHR",
		"OPENVG_BIT"
		]),
	("SurfaceBits", [
		"PBUFFER_BIT",
		"PIXMAP_BIT",
		"WINDOW_BIT",
		"MULTISAMPLE_RESOLVE_BOX_BIT",
		"SWAP_BEHAVIOR_PRESERVED_BIT",
		"VG_ALPHA_FORMAT_PRE_BIT",
		"VG_COLORSPACE_LINEAR_BIT",
		"LOCK_SURFACE_BIT_KHR",
		"OPTIMAL_FORMAT_BIT_KHR"
		])
]

# Enum mapping
ENUM_GROUPS = [
	("Boolean",			["TRUE", "FALSE"]),
	("BoolDontCare",	["TRUE", "FALSE", "DONT_CARE"]),
	("API",				["OPENGL_API", "OPENGL_ES_API", "OPENVG_API"]),
	("Error", [
		"SUCCESS",
		"NOT_INITIALIZED",
		"BAD_ACCESS",
		"BAD_ALLOC",
		"BAD_ATTRIBUTE",
		"BAD_CONFIG",
		"BAD_CONTEXT",
		"BAD_CURRENT_SURFACE",
		"BAD_DISPLAY",
		"BAD_MATCH",
		"BAD_NATIVE_PIXMAP",
		"BAD_NATIVE_WINDOW",
		"BAD_PARAMETER",
		"BAD_SURFACE",
		"CONTEXT_LOST"
		]),
	("ContextAttrib", [
		"CONFIG_ID",
		"CONTEXT_CLIENT_TYPE",
		"CONTEXT_CLIENT_VERSION",
		"RENDER_BUFFER"
		]),
	("ConfigAttrib", [
		"BUFFER_SIZE",
		"RED_SIZE",
		"GREEN_SIZE",
		"BLUE_SIZE",
		"LUMINANCE_SIZE",
		"ALPHA_SIZE",
		"ALPHA_MASK_SIZE",
		"BIND_TO_TEXTURE_RGB",
		"BIND_TO_TEXTURE_RGBA",
		"COLOR_BUFFER_TYPE",
		"CONFIG_CAVEAT",
		"CONFIG_ID",
		"CONFORMANT",
		"DEPTH_SIZE",
		"LEVEL",
		"MATCH_NATIVE_PIXMAP",
		"MAX_SWAP_INTERVAL",
		"MIN_SWAP_INTERVAL",
		"NATIVE_RENDERABLE",
		"NATIVE_VISUAL_TYPE",
		"RENDERABLE_TYPE",
		"SAMPLE_BUFFERS",
		"SAMPLES",
		"STENCIL_SIZE",
		"SURFACE_TYPE",
		"TRANSPARENT_TYPE",
		"TRANSPARENT_RED_VALUE",
		"TRANSPARENT_GREEN_VALUE",
		"TRANSPARENT_BLUE_VALUE",
		"COLOR_COMPONENT_TYPE_EXT"
		]),
	("SurfaceAttrib", [
		"CONFIG_ID",
		"WIDTH",
		"HEIGHT",
		"HORIZONTAL_RESOLUTION",
		"VERTICAL_RESOLUTION",
		"LARGEST_PBUFFER",
		"MIPMAP_TEXTURE",
		"MIPMAP_LEVEL",
		"MULTISAMPLE_RESOLVE",
		"PIXEL_ASPECT_RATIO",
		"RENDER_BUFFER",
		"SWAP_BEHAVIOR",
		"TEXTURE_FORMAT",
		"TEXTURE_TARGET",
		"ALPHA_FORMAT",
		"COLORSPACE"
		]),
	("YuvOrder", [
		"NONE",
		"YUV_ORDER_YUV_EXT",
		"YUV_ORDER_YVU_EXT",
		"YUV_ORDER_YUYV_EXT",
		"YUV_ORDER_UYVY_EXT",
		"YUV_ORDER_YVYU_EXT",
		"YUV_ORDER_VYUY_EXT",
		"YUV_ORDER_AYUV_EXT",
		]),
	("YuvPlaneBpp", [
		"YUV_PLANE_BPP_0_EXT",
		"YUV_PLANE_BPP_8_EXT",
		"YUV_PLANE_BPP_10_EXT",
		]),
	("ColorComponentType",	["COLOR_COMPONENT_TYPE_FIXED_EXT", "COLOR_COMPONENT_TYPE_FLOAT_EXT"]),
	("SurfaceTarget",		["READ", "DRAW"]),

	# ConfigAttrib values
	("ColorBufferType",		["RGB_BUFFER", "LUMINANCE_BUFFER"]),
	("ConfigCaveat",		["NONE", "SLOW_CONFIG", "NON_CONFORMANT_CONFIG"]),
	("TransparentType",		["NONE", "TRANSPARENT_RGB"]),

	# SurfaceAttrib values
	("MultisampleResolve",	["MULTISAMPLE_RESOLVE_DEFAULT", "MULTISAMPLE_RESOLVE_BOX"]),
	("RenderBuffer",		["SINGLE_BUFFER", "BACK_BUFFER"]),
	("SwapBehavior",		["BUFFER_DESTROYED", "BUFFER_PRESERVED"]),
	("TextureFormat",		["NO_TEXTURE", "TEXTURE_RGB", "TEXTURE_RGBA"]),
	("TextureTarget",		["NO_TEXTURE", "TEXTURE_2D"]),
	("AlphaFormat",			["ALPHA_FORMAT_NONPRE", "ALPHA_FORMAT_PRE"]),
	("Colorspace",			["COLORSPACE_sRGB", "COLORSPACE_LINEAR"])
]

def gen (iface):
	enumGroups		= addValuePrefix(ENUM_GROUPS, "EGL_")
	bitfieldGroups	= addValuePrefix(BITFIELD_GROUPS, "EGL_")
	prototypeFile	= os.path.join(EGL_DIR, "egluStrUtilPrototypes.inl")
	implFile		= os.path.join(EGL_DIR, "egluStrUtil.inl")

	writeInlFile(prototypeFile, indentLines(genStrUtilProtos(iface, enumGroups, bitfieldGroups)))
	writeInlFile(implFile, genStrUtilImpls(iface, enumGroups, bitfieldGroups))
