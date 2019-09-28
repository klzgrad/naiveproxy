# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2015-2017 The Android Open Source Project
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
import re
import sys

def registerPaths():
	sys.path.append(os.path.dirname(os.path.dirname(__file__)))

registerPaths()

import khr_util.format
import khr_util.registry
import khr_util.registry_cache

SCRIPTS_DIR			= os.path.dirname(__file__)
EGL_DIR				= os.path.normpath(os.path.join(SCRIPTS_DIR, "..", "..", "framework", "egl"))
EGL_WRAPPER_DIR		= os.path.normpath(os.path.join(EGL_DIR, "wrapper"))

EGL_SOURCE			= khr_util.registry_cache.RegistrySource(
						"https://raw.githubusercontent.com/KhronosGroup/EGL-Registry",
						"api/egl.xml",
						"3338ed0db494d6a4db7f76627b38f0b1892db096",
						"863db99411edfd83ba1d875fb0e13e021e155689d3eae5199a9b1ec969f368e9")

VERSION				= '1.5'

EXTENSIONS			= [
	# \todo [2014-12-05 pyry] Use 1.5 core functions/enums instead
	"EGL_KHR_create_context",
	"EGL_KHR_create_context_no_error",
	"EGL_KHR_lock_surface",
	"EGL_KHR_image_base",
	"EGL_KHR_fence_sync",
	"EGL_KHR_reusable_sync",
	"EGL_KHR_wait_sync",
	"EGL_KHR_gl_texture_2D_image",
	"EGL_KHR_gl_texture_cubemap_image",
	"EGL_KHR_gl_renderbuffer_image",
	"EGL_KHR_gl_texture_3D_image",
	"EGL_EXT_create_context_robustness",
	"EGL_EXT_platform_base",
	"EGL_EXT_platform_x11",
	"EGL_KHR_platform_wayland",
	"EGL_ANDROID_image_native_buffer",
	"EGL_EXT_yuv_surface",
	"EGL_EXT_buffer_age",
	"EGL_KHR_partial_update",
	"EGL_KHR_swap_buffers_with_damage",
	"EGL_KHR_mutable_render_buffer",
	"EGL_EXT_pixel_format_float",
	"EGL_KHR_gl_colorspace",
	"EGL_EXT_gl_colorspace_bt2020_linear",
	"EGL_EXT_gl_colorspace_bt2020_pq",
	"EGL_EXT_gl_colorspace_display_p3",
	"EGL_EXT_gl_colorspace_display_p3_linear",
	"EGL_EXT_gl_colorspace_display_p3_passthrough",
	"EGL_EXT_gl_colorspace_scrgb",
	"EGL_EXT_gl_colorspace_scrgb_linear",
	"EGL_EXT_surface_SMPTE2086_metadata",
	"EGL_EXT_surface_CTA861_3_metadata",
	"EGL_EXT_gl_colorspace_bt2020_linear",
	"EGL_EXT_gl_colorspace_bt2020_pq"
]
PROTECTS			= [
	"KHRONOS_SUPPORT_INT64"
]

def getEGLRegistry ():
	return khr_util.registry_cache.getRegistry(EGL_SOURCE)

def getInterface (registry, api, version=None, profile=None, **kwargs):
	spec = khr_util.registry.spec(registry, api, version, profile, **kwargs)
	return khr_util.registry.createInterface(registry, spec, api)

def getDefaultInterface ():
	return getInterface(getEGLRegistry(), 'egl', VERSION, extensionNames = EXTENSIONS, protects = PROTECTS)

def getFunctionTypeName (funcName):
	return "%sFunc" % funcName

def getFunctionMemberName (funcName):
	assert funcName[:3] == "egl"
	return "%c%s" % (funcName[3].lower(), funcName[4:])

def genCommandList (iface, renderCommand, directory, filename, align=False):
	lines = map(renderCommand, iface.commands)
	if align:
		lines = khr_util.format.indentLines(lines)
	writeInlFile(os.path.join(directory, filename), lines)

def getVersionToken (version):
	return version.replace(".", "")

def genCommandLists (registry, renderCommand, check, directory, filePattern, align=False):
	for eFeature in registry.features:
		api			= eFeature.get('api')
		version		= eFeature.get('number')
		profile		= check(api, version)
		if profile is True:
			profile = None
		elif profile is False:
			continue
		iface		= getInterface(registry, api, version=version, profile=profile)
		filename	= filePattern % getVersionToken(version)
		genCommandList(iface, renderCommand, directory, filename, align)

INL_HEADER = khr_util.format.genInlHeader("Khronos EGL API description (egl.xml)", EGL_SOURCE.getRevision())

def writeInlFile (filename, source):
	khr_util.format.writeInlFile(filename, INL_HEADER, source)
