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

import sys

RENAME_LIST_2011_1_2011_2 = [
	("dEQP-GLES2.functional.shaders.random.basic_expressions.*",			"dEQP-GLES2.functional.shaders.random.basic_expression."),
	("dEQP-GLES2.functional.shaders.random.scalar_conversions.*",			"dEQP-GLES2.functional.shaders.random.scalar_conversion."),
	("dEQP-GLES2.functional.fbo.render.color_clears_*",						"dEQP-GLES2.functional.fbo.render.color_clear."),
	("dEQP-GLES2.functional.fbo.render.intersecting_quads_*",				"dEQP-GLES2.functional.fbo.render.depth."),
	("dEQP-GLES2.functional.fbo.render.mix_*",								"dEQP-GLES2.functional.fbo.render.color.mix_"),
	("dEQP-GLES2.functional.fbo.render.blend_*",							"dEQP-GLES2.functional.fbo.render.color.blend_"),
	("dEQP-GLES2.functional.fbo.render.shared_colorbuffer_clears_*",		"dEQP-GLES2.functional.fbo.render.shared_colorbuffer_clear."),
	("dEQP-GLES2.functional.fbo.render.shared_colorbuffer_*",				"dEQP-GLES2.functional.fbo.render.shared_colorbuffer."),
	("dEQP-GLES2.functional.fbo.render.shared_depthbuffer_*",				"dEQP-GLES2.functional.fbo.render.shared_depthbuffer."),
	("dEQP-GLES2.functional.fbo.render.texsubimage_*",						"dEQP-GLES2.functional.fbo.render.texsubimage."),
	("dEQP-GLES2.functional.fbo.render.recreate_colorbuffer_*",				"dEQP-GLES2.functional.fbo.render.recreate_colorbuffer.no_rebind_"),
	("dEQP-GLES2.functional.fbo.render.recreate_depthbuffer_*",				"dEQP-GLES2.functional.fbo.render.recreate_depthbuffer.no_rebind_"),
	("dEQP-GLES2.functional.fbo.render.resize_*",							"dEQP-GLES2.functional.fbo.render.resize.")
]

RENAME_LIST_2011_2_2011_3 = [
	("dEQP-GLES2.usecases.ui.src_over_linear_1_batched",                    "dEQP-GLES2.usecases.ui.src_over_linear_batched_1"),
	("dEQP-GLES2.usecases.ui.src_over_linear_2_batched",                    "dEQP-GLES2.usecases.ui.src_over_linear_batched_2"),
	("dEQP-GLES2.usecases.ui.src_over_linear_4_batched",                    "dEQP-GLES2.usecases.ui.src_over_linear_batched_4"),
	("dEQP-GLES2.usecases.ui.src_over_nearest_1_batched",                   "dEQP-GLES2.usecases.ui.src_over_nearest_batched_1"),
	("dEQP-GLES2.usecases.ui.src_over_nearest_2_batched",                   "dEQP-GLES2.usecases.ui.src_over_nearest_batched_2"),
	("dEQP-GLES2.usecases.ui.src_over_nearest_4_batched",                   "dEQP-GLES2.usecases.ui.src_over_nearest_batched_4"),
	("dEQP-GLES2.usecases.ui.premultiplied_src_over_linear_1_batched",      "dEQP-GLES2.usecases.ui.premultiplied_src_over_linear_batched_1"),
	("dEQP-GLES2.usecases.ui.premultiplied_src_over_linear_2_batched",      "dEQP-GLES2.usecases.ui.premultiplied_src_over_linear_batched_2"),
	("dEQP-GLES2.usecases.ui.premultiplied_src_over_linear_4_batched",      "dEQP-GLES2.usecases.ui.premultiplied_src_over_linear_batched_4"),
	("dEQP-GLES2.usecases.ui.premultiplied_src_over_nearest_1_batched",     "dEQP-GLES2.usecases.ui.premultiplied_src_over_nearest_batched_1"),
	("dEQP-GLES2.usecases.ui.premultiplied_src_over_nearest_2_batched",     "dEQP-GLES2.usecases.ui.premultiplied_src_over_nearest_batched_2"),
	("dEQP-GLES2.usecases.ui.premultiplied_src_over_nearest_4_batched",     "dEQP-GLES2.usecases.ui.premultiplied_src_over_nearest_batched_4"),
	("dEQP-GLES2.usecases.ui.no_blend_linear_1_batched",                    "dEQP-GLES2.usecases.ui.no_blend_linear_batched_1"),
	("dEQP-GLES2.usecases.ui.no_blend_linear_2_batched",                    "dEQP-GLES2.usecases.ui.no_blend_linear_batched_2"),
	("dEQP-GLES2.usecases.ui.no_blend_linear_4_batched",                    "dEQP-GLES2.usecases.ui.no_blend_linear_batched_4"),
	("dEQP-GLES2.usecases.ui.no_blend_nearest_1_batched",                   "dEQP-GLES2.usecases.ui.no_blend_nearest_batched_1"),
	("dEQP-GLES2.usecases.ui.no_blend_nearest_2_batched",                   "dEQP-GLES2.usecases.ui.no_blend_nearest_batched_2"),
	("dEQP-GLES2.usecases.ui.no_blend_nearest_4_batched",                   "dEQP-GLES2.usecases.ui.no_blend_nearest_batched_4")
]

RENAME_LIST_2011_3_2011_4 = []

RENAME_LIST_2011_4_2012_1 = [
	("dEQP-GLES2.functional.vertex_arrays.multiple_attributes.output_types.*", "dEQP-GLES2.functional.vertex_arrays.multiple_attributes.input_types."),
]

RENAME_LIST_2012_2_2012_3 = [
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_float_vertex",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_float_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_float_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_float_float_fragment"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_float_vertex",			"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_float_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_float_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_float_float_fragment"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec2_vertex",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec2_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec2_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec2_float_fragment"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec2_vertex",			"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec2_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec2_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec2_float_fragment"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec3_vertex",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec3_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec3_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec3_float_fragment"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec3_vertex",			"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec3_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec3_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec3_float_fragment"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec4_vertex",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec4_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec4_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.mediump_vec4_float_fragment"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec4_vertex",			"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec4_float_vertex"),
	("dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec4_fragment",		"dEQP-GLES2.functional.shaders.operator.geometric.refract.highp_vec4_float_fragment"),
	("dEQP-GLES2.functional.negative_api.texture.copyteximage2d_unequal_width_height_cube",	"dEQP-GLES2.functional.negative_api.texture.copyteximage2d_inequal_width_height_cube"),
	("dEQP-GLES2.functional.negative_api.texture.teximage2d_unequal_width_height_cube",		"dEQP-GLES2.functional.negative_api.texture.teximage2d_inequal_width_height_cube"),
	("dEQP-GLES2.functional.negative_api.vertex_array.draw_arrays",							"dEQP-GLES2.functional.negative_api.vertex_array.draw_arrays_invalid_program"),
	("dEQP-GLES2.functional.negative_api.vertex_array.draw_elemens",						"dEQP-GLES2.functional.negative_api.vertex_array.draw_elements_invalid_program"),
	("dEQP-GLES2.functional.negative_api.shader.attach_shader_invalid_object",				"dEQP-GLES2.functional.negative_api.shader.attach_shader"),
	("dEQP-GLES2.functional.negative_api.shader.detach_shader_invalid_object",				"dEQP-GLES2.functional.negative_api.shader.detach_shader"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.1_vertex_lights_no_texture",				"dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.1_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.2_vertex_lights_no_texture",             "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.2_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.4_vertex_lights_no_texture",             "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.4_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.1_vertex_lights",                        "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.1_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.2_vertex_lights",                        "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.2_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.4_vertex_lights",                        "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.4_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.1_fragment_lights",                      "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.1_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.2_fragment_lights",                      "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.2_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.1sample.4_fragment_lights",                      "dEQP-GLES2.usecases.shadow.shadowmaps.basic_1sample.4_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.1_vertex_lights_no_texture",             "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.1_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.2_vertex_lights_no_texture",             "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.2_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.4_vertex_lights_no_texture",             "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.4_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.1_vertex_lights",                        "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.1_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.2_vertex_lights",                        "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.2_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.4_vertex_lights",                        "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.4_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.1_fragment_lights",                      "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.1_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.2_fragment_lights",                      "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.2_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.4sample.4_fragment_lights",                      "dEQP-GLES2.usecases.shadow.shadowmaps.basic_4sample.4_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.1_vertex_lights_no_texture",            "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.1_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.2_vertex_lights_no_texture",            "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.2_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.4_vertex_lights_no_texture",            "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.4_vertex_lights_no_texture"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.1_vertex_lights",                       "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.1_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.2_vertex_lights",                       "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.2_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.4_vertex_lights",                       "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.4_vertex_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.1_fragment_lights",                     "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.1_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.2_fragment_lights",                     "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.2_fragment_lights"),
	("dEQP-GLES2.usecases.shadow.shadowmap.16sample.4_fragment_lights",                     "dEQP-GLES2.usecases.shadow.shadowmaps.basic_16sample.4_fragment_lights")
]

RENAME_LIST_2012_3_2012_4 = [
	("dEQP-GLES2.functional.depth.*",				"dEQP-GLES2.functional.fragment_ops.depth."),
	("dEQP-GLES2.functional.stencil.*",				"dEQP-GLES2.functional.fragment_ops.stencil.")
]

def readCaseList (filename):
	f = open(filename, 'r')
	cases = []
	for line in f:
		if line[0:5] == "TEST:":
			cases.append(line[6:].strip())
	f.close()
	return cases

def isWildcardPattern (pattern):
	return pattern[-1:] == '*'

# returns (cases, renames)
def renameCases (cases, rename):
	renamedCases	= []
	renamedSet		= set()
	renames			= []
	for case in cases:
		renamed = None

		for src, dst in rename:
			if isWildcardPattern(src) and case[:len(src)-1] == src[:-1]:
				renamed = dst + case[len(src)-1:]
				break
			elif case == src:
				renamed = dst
				break

		if renamed != None:
			renames.append((case, renamed))
			case = renamed

		# It is possible that some later case is renamed to case already seen in the list
		assert not case in renamedSet or renamed != None
		if case not in renamedSet:
			renamedCases.append(case)
			renamedSet.add(case)

	return (renamedCases, renames)

# returns (added, removed) lists
def diffCaseLists (old, new):
	added	= []
	removed	= []

	oldSet = set(old)
	newSet = set(new)

	# build added list
	for case in new:
		if not case in oldSet:
			added.append(case)

	# build removed set
	for case in old:
		if not case in newSet:
			removed.append(case)

	return (added, removed)

if __name__ == "__main__":
	if len(sys.argv) != 3:
		print("%s [old caselist] [new caselist]" % sys.argv[0])
		sys.exit(-1)

	oldCases	= readCaseList(sys.argv[1])
	newCases	= readCaseList(sys.argv[2])
	rename		= RENAME_LIST_2012_3_2012_4

	renamedCases, renameList	= renameCases(oldCases, rename)
	added, removed				= diffCaseLists(renamedCases, newCases)

#	for src, dst in rename:
#		print("RENAME: %s -> %s" % (src, dst))

	for case in added:
		print("ADD: %s" % case)

	for src, dst in renameList:
		print("RENAME: %s -> %s" % (src, dst))

	for case in removed:
		print("REMOVE: %s" % case)
