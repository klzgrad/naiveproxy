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

sys.path.append(os.path.dirname(os.path.dirname(__file__)))

import khr_util.format
import khr_util.registry
import khr_util.registry_cache

SCRIPTS_DIR			= os.path.dirname(__file__)
OPENGL_DIR			= os.path.normpath(os.path.join(SCRIPTS_DIR, "..", "..", "framework", "opengl"))
EGL_DIR				= os.path.normpath(os.path.join(SCRIPTS_DIR, "..", "..", "framework", "egl"))
OPENGL_INC_DIR		= os.path.join(OPENGL_DIR, "wrapper")

GL_SOURCE			= khr_util.registry_cache.RegistrySource(
						"https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry",
						"xml/gl.xml",
						"9d534f9312e56c72df763207e449c6719576fd54",
						"245e90331c83c4c743a2b9d0dad51e27a699f2040ebd34dd5338637adf276752")

EXTENSIONS			= [
	'GL_KHR_texture_compression_astc_ldr',
	'GL_KHR_blend_equation_advanced',
	'GL_KHR_blend_equation_advanced_coherent',
	'GL_KHR_debug',
	'GL_EXT_robustness',
	'GL_KHR_robustness',
	'GL_KHR_no_error',
	'GL_KHR_parallel_shader_compile',
	'GL_KHR_shader_subgroup',
	'GL_EXT_bgra',
	'GL_EXT_geometry_point_size',
	'GL_EXT_tessellation_shader',
	'GL_EXT_geometry_shader',
	'GL_EXT_texture_buffer',
	'GL_EXT_texture_filter_anisotropic',
	'GL_EXT_texture_cube_map_array',
	'GL_EXT_texture_snorm',
	'GL_EXT_primitive_bounding_box',
	'GL_EXT_texture_compression_s3tc',
	'GL_EXT_texture_type_2_10_10_10_REV',
	'GL_EXT_clip_control',
	'GL_EXT_copy_image',
	'GL_EXT_depth_bounds_test',
	'GL_EXT_direct_state_access',
	'GL_EXT_draw_buffers_indexed',
	'GL_EXT_draw_elements_base_vertex',
	'GL_EXT_direct_state_access',
	'GL_EXT_read_format_bgra',
	'GL_EXT_texture_storage',
	'GL_EXT_texture_sRGB_decode',
	'GL_EXT_texture_border_clamp',
	'GL_EXT_texture_sRGB_R8',
	'GL_EXT_texture_sRGB_RG8',
	'GL_EXT_multisampled_render_to_texture',
	'GL_EXT_debug_marker',
	'GL_EXT_polygon_offset_clamp',
	'GL_IMG_texture_compression_pvrtc',
	'GL_OES_EGL_image',
	'GL_OES_EGL_image_external',
	'GL_OES_compressed_ETC1_RGB8_texture',
	'GL_OES_compressed_paletted_texture',
	'GL_OES_required_internalformat',
	'GL_OES_packed_depth_stencil',
	'GL_OES_texture_3D',
	'GL_OES_texture_half_float',
	'GL_OES_texture_storage_multisample_2d_array',
	'GL_OES_sample_shading',
	'GL_OES_standard_derivatives',
	'GL_OES_stencil1',
	'GL_OES_stencil4',
	'GL_OES_surfaceless_context',
	'GL_OES_mapbuffer',
	'GL_OES_vertex_array_object',
	'GL_OES_viewport_array',
	'GL_ARB_clip_control',
	'GL_ARB_buffer_storage',
	'GL_ARB_compute_shader',
	'GL_ARB_draw_indirect',
	'GL_ARB_draw_instanced',
	'GL_ARB_draw_elements_base_vertex',
	'GL_ARB_direct_state_access',
	'GL_ARB_get_program_binary',
	'GL_ARB_gl_spirv',
	'GL_ARB_indirect_parameters',
	'GL_ARB_internalformat_query',
	'GL_ARB_instanced_arrays',
	'GL_ARB_multi_draw_indirect',
	'GL_ARB_parallel_shader_compile',
	'GL_ARB_program_interface_query',
	'GL_ARB_separate_shader_objects',
	'GL_ARB_shader_ballot',
	'GL_ARB_shader_image_load_store',
	'GL_ARB_shader_viewport_layer_array',
	'GL_ARB_sparse_buffer',
	'GL_ARB_sparse_texture',
	'GL_ARB_spirv_extensions',
	'GL_ARB_tessellation_shader',
	'GL_ARB_texture_barrier',
	'GL_ARB_texture_filter_minmax',
	'GL_ARB_texture_gather',
	'GL_ARB_texture_storage',
	'GL_ARB_texture_storage_multisample',
	'GL_ARB_texture_multisample',
	'GL_ARB_texture_view',
	'GL_ARB_transform_feedback2',
	'GL_ARB_transform_feedback3',
	'GL_ARB_transform_feedback_instanced',
	'GL_ARB_transform_feedback_overflow_query',
	'GL_ARB_vertex_array_bgra',
	'GL_ARB_vertex_attrib_64bit',
	'GL_ARB_vertex_attrib_binding',
	'GL_NV_deep_texture3D',
	'GL_NV_gpu_multicast',
	'GL_NV_internalformat_sample_query',
	'GL_NV_shader_subgroup_partitioned',
	'GL_NVX_cross_process_interop',
	'GL_OES_draw_elements_base_vertex',
	'GL_OVR_multiview',
	'GL_OVR_multiview_multisampled_render_to_texture',
]

ALIASING_EXCEPTIONS = [
	# registry insists that this aliases glRenderbufferStorageMultisample,
	# and from a desktop GL / GLX perspective it *must*, but for ES they are
	# unfortunately separate functions with different semantics.
	'glRenderbufferStorageMultisampleEXT',
]

def getGLRegistry ():
	return khr_util.registry_cache.getRegistry(GL_SOURCE)

def getHybridInterface (stripAliasedExtCommands = True):
	# This is a bit awkward, since we have to create a strange hybrid
	# interface that includes both GL and ES features and extensions.
	registry = getGLRegistry()
	glFeatures = registry.getFeatures('gl')
	esFeatures = registry.getFeatures('gles2')
	spec = khr_util.registry.InterfaceSpec()

	for feature in registry.getFeatures('gl'):
		spec.addFeature(feature, 'gl', 'core')

	for feature in registry.getFeatures('gles2'):
		spec.addFeature(feature, 'gles2')

	for extName in EXTENSIONS:
		extension = registry.extensions[extName]
		# Add all extensions using the ES2 api, but force even non-ES2
		# extensions to be included.
		spec.addExtension(extension, 'gles2', 'core', force=True)

	iface = khr_util.registry.createInterface(registry, spec, 'gles2')

	if stripAliasedExtCommands:
		# Remove redundant extension commands that are already provided by core.
		strippedCmds = []

		for command in iface.commands:
			if command.alias == None or command.name in ALIASING_EXCEPTIONS:
				strippedCmds.append(command)

		iface.commands = strippedCmds

	return iface

def versionCheck(version):
	if type(version) is bool:
		if version == False:
			return True
	if type(version) is str:
		return version < "3.2"
	raise "Version check failed"

def getInterface (registry, api, version=None, profile=None, **kwargs):
	spec = khr_util.registry.spec(registry, api, version, profile, **kwargs)
	if api == 'gl' and profile == 'core' and versionCheck(version):
		gl32 = registry.features['GL_VERSION_3_2']
		for eRemove in gl32.xpath('remove'):
			spec.addComponent(eRemove)
	return khr_util.registry.createInterface(registry, spec, api)

def getVersionToken (api, version):
	prefixes = { 'gles2': "ES", 'gl': "GL" }
	return prefixes[api] + version.replace(".", "")

def genCommandList(iface, renderCommand, directory, filename, align=False):
	lines = map(renderCommand, iface.commands)
	lines = filter(lambda l: l != None, lines)
	if align:
		lines = indentLines(lines)
	writeInlFile(os.path.join(directory, filename), lines)

def genCommandLists(registry, renderCommand, check, directory, filePattern, align=False):
	for eFeature in registry.features:
		api			= eFeature.get('api')
		version		= eFeature.get('number')
		profile		= check(api, version)
		if profile is True:
			profile = None
		elif profile is False:
			continue
		iface		= getInterface(registry, api, version=version, profile=profile)
		filename	= filePattern % getVersionToken(api, version)
		genCommandList(iface, renderCommand, directory, filename, align)

def getFunctionTypeName (funcName):
	return "%sFunc" % funcName

def getFunctionMemberName (funcName):
	assert funcName[:2] == "gl"
	if funcName[:5] == "glEGL":
		# Otherwise we end up with gl.eGLImage...
		return "egl%s" % funcName[5:]
	else:
		return "%c%s" % (funcName[2].lower(), funcName[3:])

INL_HEADER = khr_util.format.genInlHeader("Khronos GL API description (gl.xml)", GL_SOURCE.getRevision())

def writeInlFile (filename, source):
	khr_util.format.writeInlFile(filename, INL_HEADER, source)

# Aliases from khr_util.common
indentLines			= khr_util.format.indentLines
normalizeConstant	= khr_util.format.normalizeConstant
commandParams		= khr_util.format.commandParams
commandArgs			= khr_util.format.commandArgs
