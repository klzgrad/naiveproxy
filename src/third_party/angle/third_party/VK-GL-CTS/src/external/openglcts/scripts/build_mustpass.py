# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
#
# Copyright 2015 The Android Open Source Project
# Copyright (C) 2016 The Khronos Group Inc
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

from collections import OrderedDict

from build_caselists import Module, getModuleByName, DEFAULT_BUILD_DIR, DEFAULT_TARGET
from mustpass import Project, Package, Mustpass, Configuration, include, exclude, genMustpassLists

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

from build.common import DEQP_DIR
from build.config import ANY_GENERATOR, BuildConfig


COPYRIGHT_DECLARATION = """\
/*     Copyright (C) 2016-2017 The Khronos Group Inc
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
*/"""

buildPath						= DEFAULT_BUILD_DIR.format(targetName = DEFAULT_TARGET, buildType = "Release")

#-------------------------------------------------- ES MUSTPASS----------------------------------------------------------------------

CTS_AOSP_MP_DATA_DIR			= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "gles", "aosp_mustpass")

CTS_AOSP_MP_DEVICE_DIR			= "gl_cts/data/mustpass/gles/aosp_mustpass"

CTS_MP_INC_DIR					= os.path.join(DEQP_DIR, "external", "openglcts", "modules", "runner")

CTS_AOSP_MP_ES_PROJECT			= Project(name = "AOSP Mustpass ES", path = CTS_AOSP_MP_DATA_DIR, incpath = CTS_MP_INC_DIR, devicepath = CTS_AOSP_MP_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

CTS_KHR_MP_DATA_DIR				= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "gles", "khronos_mustpass")

CTS_KHR_MP_DEVICE_DIR			= "gl_cts/data/mustpass/gles/khronos_mustpass"

CTS_KHR_MP_ES_PROJECT			= Project(name = "Khronos Mustpass ES", path = CTS_KHR_MP_DATA_DIR, incpath = CTS_MP_INC_DIR, devicepath = CTS_KHR_MP_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

CTS_AOSP_MP_EGL_DEVICE_DIR		= "gl_cts/data/mustpass/egl/aosp_mustpass"

CTS_AOSP_MP_EGL_DATA_DIR		= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "egl", "aosp_mustpass")

CTS_AOSP_MP_EGL_PROJECT			= Project(name = "AOSP Mustpass EGL", path = CTS_AOSP_MP_EGL_DATA_DIR, incpath = CTS_MP_INC_DIR, devicepath = CTS_AOSP_MP_EGL_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

CTS_KHR_MP_NOCTX_DATA_DIR		= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "gles", "khronos_mustpass_noctx")

CTS_KHR_MP_NOCTX_DEVICE_DIR		= "gl_cts/data/mustpass/gles/khronos_mustpass_noctx"

CTS_KHR_MP_NOCTX_ES_PROJECT		= Project(name = "Khronos Mustpass ES NoContext", path = CTS_KHR_MP_NOCTX_DATA_DIR, incpath = CTS_MP_INC_DIR, devicepath = CTS_KHR_MP_NOCTX_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

CTS_KHR_MP_SINGLE_DATA_DIR		= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "gles", "khronos_mustpass_single")

CTS_KHR_MP_SINGLE_DEVICE_DIR	= "gl_cts/data/mustpass/gles/khronos_mustpass_single"

CTS_KHR_MP_SINGLE_ES_PROJECT		= Project(name = "Khronos Mustpass ES Single Config", path = CTS_KHR_MP_SINGLE_DATA_DIR, incpath = CTS_MP_INC_DIR, devicepath = CTS_KHR_MP_SINGLE_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

EGL_MODULE						= getModuleByName("dEQP-EGL")
ES2CTS_MODULE					= getModuleByName("dEQP-GLES2")
ES3CTS_MODULE					= getModuleByName("dEQP-GLES3")
ES31CTS_MODULE					= getModuleByName("dEQP-GLES31")

ES2KHR_MODULE					= getModuleByName("KHR-GLES2")
ES3KHR_MODULE					= getModuleByName("KHR-GLES3")
ES31KHR_MODULE					= getModuleByName("KHR-GLES31")
ES32KHR_MODULE					= getModuleByName("KHR-GLES32")
NOCTX_ES2_KHR_MODULE			= getModuleByName("KHR-NOCTX-ES2")
NOCTX_ES32_KHR_MODULE			= getModuleByName("KHR-NOCTX-ES32")
SINGLE_ES32_KHR_MODULE			= getModuleByName("KHR-Single-GLES32")

ES2GTF_MODULE					= getModuleByName("GTF-GLES2")
ES3GTF_MODULE					= getModuleByName("GTF-GLES3")
ES31GTF_MODULE					= getModuleByName("GTF-GLES31")

GLCTS_GLES2_PKG						= Package(module = ES2CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters			= [include("gles2-master.txt")]),
	])
GLCTS_3_2_2_GLES3_PKG					= Package(module = ES3CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters			= [include("gles3-master.txt")]),

		# Rotations
		Configuration(name			= "rotate-portrait",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "0",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles3-master.txt"), include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-landscape",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "90",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles3-master.txt"), include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-reverse-portrait",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "180",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles3-master.txt"), include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-reverse-landscape",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "270",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= [include("gles3-master.txt"), include("gles3-rotation.txt")]),

		# MSAA
		Configuration(name			= "multisample",
					glconfig		= "rgba8888d24s8ms4",
					rotation		= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters			= [include("gles3-master.txt"),
									   include("gles3-multisample.txt"),
									   exclude("gles3-multisample-issues.txt")]),

		# Pixel format
		Configuration(name			= "565-no-depth-no-stencil",
					glconfig		= "rgb565d0s0ms0",
					rotation		= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles3-master.txt"),
									   include("gles3-pixelformat.txt"),
									   exclude("gles3-pixelformat-issues.txt")]),
	])
GLCTS_3_2_2_GLES31_PKG					= Package(module = ES31CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters			= [include("gles31-master.txt")]),
		# Rotations
		Configuration(name			= "rotate-portrait",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "0",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles31-master.txt"), include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-landscape",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "90",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles31-master.txt"), include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-reverse-portrait",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "180",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles31-master.txt"), include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-reverse-landscape",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "270",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles31-master.txt"), include("gles31-rotation.txt")]),

		# MSAA
		Configuration(name			= "multisample",
					glconfig		= "rgba8888d24s8ms4",
					rotation		= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters			= [include("gles31-master.txt"), include("gles31-multisample.txt")]),

		# Pixel format
		Configuration(name			= "565-no-depth-no-stencil",
					glconfig		= "rgb565d0s0ms0",
					rotation		= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters			= [include("gles31-master.txt"), include("gles31-pixelformat.txt")]),
	])

# 3.2.3.x
GLCTS_3_2_3_EGL_COMMON_FILTERS			= [include("egl-master.txt"),
		exclude("egl-test-issues.txt"),
		exclude("egl-internal-api-tests.txt"),
		exclude("egl-driver-issues.txt")
	]
GLCTS_3_2_3_EGL_PKG						= Package(module = EGL_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "unspecified",
					surfacewidth    = "256",
                    surfaceheight   = "256",
					filters			= GLCTS_3_2_3_EGL_COMMON_FILTERS),
	])

GLCTS_3_2_3_GLES2_COMMON_FILTERS	= [
		include("gles2-master.txt"),
		exclude("gles2-test-issues.txt"),
		exclude("gles2-spec-issues.txt"),
		exclude("gles2-driver-issues.txt"),
		exclude("gles2-hw-issues.txt")
	]
GLCTS_3_2_3_GLES2_PKG         = Package(module = ES2CTS_MODULE, configurations = [
        # Master
        Configuration(name          = "master",
                    glconfig        = "rgba8888d24s8ms0",
                    rotation        = "unspecified",
                    surfacewidth    = "256",
                    surfaceheight   = "256",
                    filters         = GLCTS_3_2_3_GLES2_COMMON_FILTERS),
    ])

GLCTS_3_2_3_GLES3_COMMON_FILTERS		= [
		include("gles3-master.txt"),
		exclude("gles3-test-issues.txt"),
		exclude("gles3-spec-issues.txt"),
		exclude("gles3-driver-issues.txt"),
	]

GLCTS_3_2_3_GLES3_PKG				= Package(module = ES3CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "unspecified",
					surfacewidth    = "256",
					surfaceheight   = "256",
					filters		= GLCTS_3_2_3_GLES3_COMMON_FILTERS + [exclude("gles3-hw-issues.txt")]),
		# Rotations
		Configuration(name			= "rotate-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "0",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "90",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-reverse-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "180",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-reverse-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "270",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),

		# MSAA
		Configuration(name			= "multisample",
					glconfig	= "rgba8888d24s8ms4",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters		= GLCTS_3_2_3_GLES3_COMMON_FILTERS + [include("gles3-multisample.txt"), exclude("gles3-multisample-hw-issues.txt")]),

		# Pixel format
		Configuration(name			= "565-no-depth-no-stencil",
					glconfig	= "rgb565d0s0ms0",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES3_COMMON_FILTERS + [include("gles3-pixelformat.txt")]),

	])

GLCTS_3_2_3_GLES31_COMMON_FILTERS	= [
		include("gles31-master.txt"),
		exclude("gles31-test-issues.txt"),
		exclude("gles31-spec-issues.txt"),
		exclude("gles31-driver-issues.txt"),
		exclude("gles31-hw-issues.txt")
	]

GLCTS_3_2_3_GLES31_PKG				= Package(module = ES31CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters		= GLCTS_3_2_3_GLES31_COMMON_FILTERS),

		# Rotations
		Configuration(name			= "rotate-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "0",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "90",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-reverse-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "180",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-reverse-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "270",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),

		# MSAA
		Configuration(name			= "multisample",
					glconfig	= "rgba8888d24s8ms4",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters		= [include("gles31-master.txt"),
									include("gles31-multisample.txt"),
									exclude("gles31-multisample-test-issues.txt")]),

		# Pixel format
		Configuration(name			= "565-no-depth-no-stencil",
					glconfig	= "rgb565d0s0ms0",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= GLCTS_3_2_3_GLES31_COMMON_FILTERS + [include("gles31-pixelformat.txt")]),
	])

GLCTS_3_2_3_GLES32_KHR_COMMON_FILTERS	= [
		include("gles32-khr-master.txt"),
		exclude("gles32-khr-test-issues.txt"),
		exclude("gles32-khr-spec-issues.txt")
	]

GLCTS_3_2_3_GLES32_KHR_PKG_1CFG			= Package(module = ES32KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= GLCTS_3_2_3_GLES32_KHR_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= GLCTS_3_2_3_GLES32_KHR_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "-1",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= GLCTS_3_2_3_GLES32_KHR_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "-1",
					surfaceheight	= "64",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= GLCTS_3_2_3_GLES32_KHR_COMMON_FILTERS),
	])

GLCTS_3_2_3_GLES32_KHR_PKG_N1CFG		= Package(module = ES32KHR_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= GLCTS_3_2_3_GLES32_KHR_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= GLCTS_3_2_3_GLES32_KHR_COMMON_FILTERS),
	])


# master
MASTER_EGL_COMMON_FILTERS			= [include("egl-master.txt"),
										exclude("egl-test-issues.txt"),
										exclude("egl-internal-api-tests.txt")]
MASTER_EGL_PKG						= Package(module = EGL_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig		= "rgba8888d24s8ms0",
					rotation		= "unspecified",
					surfacewidth    = "256",
                    surfaceheight   = "256",
					filters			= MASTER_EGL_COMMON_FILTERS),
	])

MASTER_GLES2_COMMON_FILTERS			= [
				include("gles2-master.txt"),
				exclude("gles2-test-issues.txt"),
				exclude("gles2-spec-issues.txt")
		]
MASTER_GLES2_PKG         = Package(module = ES2CTS_MODULE, configurations = [
        # Master
        Configuration(name          = "master",
                    glconfig        = "rgba8888d24s8ms0",
                    rotation        = "unspecified",
                    surfacewidth    = "256",
                    surfaceheight   = "256",
                    filters         = MASTER_GLES2_COMMON_FILTERS),
    ])

MASTER_GLES3_COMMON_FILTERS		= [
		include("gles3-master.txt"),
		exclude("gles3-test-issues.txt"),
		exclude("gles3-spec-issues.txt")
	]
MASTER_GLES3_PKG				= Package(module = ES3CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "unspecified",
					surfacewidth    = "256",
					surfaceheight   = "256",
					filters		= MASTER_GLES3_COMMON_FILTERS),
		# Rotations
		Configuration(name			= "rotate-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "0",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "90",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-reverse-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "180",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),
		Configuration(name			= "rotate-reverse-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "270",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES3_COMMON_FILTERS + [include("gles3-rotation.txt")]),

		# MSAA
		Configuration(name			= "multisample",
					glconfig	= "rgba8888d24s8ms4",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters		= MASTER_GLES3_COMMON_FILTERS + [include("gles3-multisample.txt")]),

		# Pixel format
		Configuration(name			= "565-no-depth-no-stencil",
					glconfig	= "rgb565d0s0ms0",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES3_COMMON_FILTERS + [include("gles3-pixelformat.txt")]),
	])
MASTER_GLES31_COMMON_FILTERS             = [
		include("gles31-master.txt"),
		exclude("gles31-test-issues.txt"),
		exclude("gles31-spec-issues.txt")
	]

MASTER_GLES31_PKG				= Package(module = ES31CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "master",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters		= MASTER_GLES31_COMMON_FILTERS),

		# Rotations
		Configuration(name			= "rotate-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "0",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "90",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-reverse-portrait",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "180",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),
		Configuration(name			= "rotate-reverse-landscape",
					glconfig	= "rgba8888d24s8ms0",
					rotation	= "270",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES31_COMMON_FILTERS + [include("gles31-rotation.txt")]),

		# MSAA
		Configuration(name			= "multisample",
					glconfig	= "rgba8888d24s8ms4",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					filters		= MASTER_GLES31_COMMON_FILTERS + [include("gles31-multisample.txt")]),

		# Pixel format
		Configuration(name			= "565-no-depth-no-stencil",
					glconfig	= "rgb565d0s0ms0",
					rotation	= "unspecified",
					surfacewidth	= "256",
					surfaceheight	= "256",
					os				= "android",
					filters		= MASTER_GLES31_COMMON_FILTERS + [include("gles31-pixelformat.txt")]),
	])

GLCTS_GLES2_KHR_PKG_1CFG			= Package(module = ES2KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles2-khr-master.txt")]),
	])

GLCTS_GLES2_DEQP_PKG_1CFG			= Package(module = ES2CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "deqp-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles2-deqp-master.txt")]),
	])

GLCTS_GLES2_GTF_PKG_1CFG			= Package(module = ES2GTF_MODULE, configurations = [
		# Master
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles2-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= [include("gles2-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "-1",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= [include("gles2-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "-1",
					surfaceheight	= "64",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= [include("gles2-gtf-master.txt")]),
		Configuration(name			= "gtf-egl",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles2-gtf-egl.txt")]),
		Configuration(name			= "gtf-egl",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= [include("gles2-gtf-egl.txt")]),
	])

GLCTS_GLES2_KHR_PKG_N1CFG			= Package(module = ES2KHR_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles2-khr-master.txt")]),
	])

GLCTS_GLES2_DEQP_PKG_N1CFG			= Package(module = ES2CTS_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "deqp-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles2-deqp-master.txt")]),
	])

GLCTS_GLES2_GTF_PKG_N1CFG			= Package(module = ES2GTF_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters		= [include("gles2-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= [include("gles2-gtf-master.txt")]),
	])

GLCTS_GLES3_DEQP_PKG_1CFG			= Package(module = ES3CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "deqp-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles3-deqp-master.txt")]),
	])

GLCTS_GLES3_KHR_PKG_1CFG			= Package(module = ES3KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles3-khr-master.txt")]),
	])

GLCTS_GLES3_GTF_PKG_1CFG			= Package(module = ES3GTF_MODULE, configurations = [
		# Master
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles3-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= [include("gles3-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "-1",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= [include("gles3-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "-1",
					surfaceheight	= "64",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= [include("gles3-gtf-master.txt")]),
	])

GLCTS_GLES3_DEQP_PKG_N1CFG			= Package(module = ES3CTS_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "deqp-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles3-deqp-master.txt")]),
	])

GLCTS_GLES3_KHR_PKG_N1CFG			= Package(module = ES3KHR_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles3-khr-master.txt")]),
	])
GLCTS_GLES3_GTF_PKG_N1CFG			= Package(module = ES3GTF_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles3-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= [include("gles3-gtf-master.txt")]),
	])

GLCTS_GLES31_DEQP_PKG_1CFG			= Package(module = ES31CTS_MODULE, configurations = [
		# Master
		Configuration(name			= "deqp-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles31-deqp-master.txt")]),
	])

GLCTS_GLES31_KHR_PKG_1CFG			= Package(module = ES31KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles31-khr-master.txt")]),
	])

GLCTS_GLES31_GTF_PKG_1CFG			= Package(module = ES31GTF_MODULE, configurations = [
		# Master
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles31-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= [include("gles31-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "-1",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= [include("gles31-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "-1",
					surfaceheight	= "64",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= [include("gles31-gtf-master.txt")]),
	])

GLCTS_GLES31_KHR_PKG_N1CFG			= Package(module = ES31KHR_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles31-khr-master.txt")]),
	])

GLCTS_GLES31_DEQP_PKG_N1CFG			= Package(module = ES31CTS_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "deqp-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles31-deqp-master.txt")]),
	])

GLCTS_GLES31_GTF_PKG_N1CFG			= Package(module = ES31GTF_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "gtf-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles31-gtf-master.txt")]),
		Configuration(name			= "gtf-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= [include("gles31-gtf-master.txt")]),
	])

MASTER_GLES32_COMMON_FILTERS             = [
		include("gles32-khr-master.txt"),
		exclude("gles32-khr-test-issues.txt"),
		exclude("gles32-khr-spec-issues.txt")
	]

GLCTS_GLES32_KHR_PKG_1CFG			= Package(module = ES32KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= MASTER_GLES32_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= MASTER_GLES32_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "-1",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= MASTER_GLES32_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "-1",
					surfaceheight	= "64",
					baseseed		= "3",
					fboconfig		= "rgba8888d24s8",
					filters			= MASTER_GLES32_COMMON_FILTERS),
	])

GLCTS_GLES32_KHR_PKG_N1CFG			= Package(module = ES32KHR_MODULE, useforfirsteglconfig = False, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= MASTER_GLES32_COMMON_FILTERS),
		Configuration(name			= "khr-master",
					surfacewidth	= "113",
					surfaceheight	= "47",
					baseseed		= "2",
					filters			= MASTER_GLES32_COMMON_FILTERS),
	])

GLCTS_NOCTX_ES2_KHR_PKG			= Package(module = NOCTX_ES2_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-noctx-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles2-khr-master.txt")]),
	])

GLCTS_NOCTX_ES32_KHR_PKG		= Package(module = NOCTX_ES32_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-noctx-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= MASTER_GLES32_COMMON_FILTERS),
	])

GLCTS_SINGLE_ES32_KHR_PKG		= Package(module = SINGLE_ES32_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-single",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gles32-khr-single.txt")]),
	])


ES_MUSTPASS_LISTS		= [
	# 3.2.2.x
	Mustpass(project = CTS_KHR_MP_ES_PROJECT,	version = "3.2.2.x", isCurrent=False,
			packages = [GLCTS_GLES2_KHR_PKG_1CFG,
						GLCTS_GLES2_DEQP_PKG_1CFG,
						GLCTS_GLES2_GTF_PKG_1CFG,
						GLCTS_GLES2_KHR_PKG_N1CFG,
						GLCTS_GLES2_DEQP_PKG_N1CFG,
						GLCTS_GLES2_GTF_PKG_N1CFG,
						GLCTS_GLES3_KHR_PKG_1CFG,
						GLCTS_GLES3_DEQP_PKG_1CFG,
						GLCTS_GLES3_GTF_PKG_1CFG,
						GLCTS_GLES3_KHR_PKG_N1CFG,
						GLCTS_GLES3_DEQP_PKG_N1CFG,
						GLCTS_GLES3_GTF_PKG_N1CFG,
						GLCTS_GLES31_KHR_PKG_1CFG,
						GLCTS_GLES31_DEQP_PKG_1CFG,
						GLCTS_GLES31_GTF_PKG_1CFG,
						GLCTS_GLES31_KHR_PKG_N1CFG,
						GLCTS_GLES31_DEQP_PKG_N1CFG,
						GLCTS_GLES31_GTF_PKG_N1CFG,
						GLCTS_GLES32_KHR_PKG_1CFG,
						GLCTS_GLES32_KHR_PKG_N1CFG,
						]),

	Mustpass(project = CTS_AOSP_MP_ES_PROJECT,	version = "3.2.2.x", isCurrent=False,
			packages = [GLCTS_GLES2_PKG, GLCTS_3_2_2_GLES3_PKG, GLCTS_3_2_2_GLES31_PKG]),

	# 3.2.3.x
	Mustpass(project = CTS_KHR_MP_ES_PROJECT,	version = "3.2.3.x", isCurrent=False,
			packages = [GLCTS_GLES2_KHR_PKG_1CFG,
						GLCTS_GLES2_GTF_PKG_1CFG,
						GLCTS_GLES2_KHR_PKG_N1CFG,
						GLCTS_GLES2_GTF_PKG_N1CFG,
						GLCTS_GLES3_KHR_PKG_1CFG,
						GLCTS_GLES3_GTF_PKG_1CFG,
						GLCTS_GLES3_KHR_PKG_N1CFG,
						GLCTS_GLES3_GTF_PKG_N1CFG,
						GLCTS_GLES31_KHR_PKG_1CFG,
						GLCTS_GLES31_GTF_PKG_1CFG,
						GLCTS_GLES31_KHR_PKG_N1CFG,
						GLCTS_GLES31_GTF_PKG_N1CFG,
						GLCTS_3_2_3_GLES32_KHR_PKG_1CFG,
						GLCTS_3_2_3_GLES32_KHR_PKG_N1CFG,
						]),

	Mustpass(project = CTS_AOSP_MP_ES_PROJECT, version = "3.2.3.x", isCurrent=False,
			packages = [GLCTS_3_2_3_GLES2_PKG, GLCTS_3_2_3_GLES3_PKG, GLCTS_3_2_3_GLES31_PKG]),

	Mustpass(project = CTS_AOSP_MP_EGL_PROJECT, version = "3.2.3.x", isCurrent=False,
			packages = [GLCTS_3_2_3_EGL_PKG]),

	# 3.2.4.x
	Mustpass(project = CTS_KHR_MP_ES_PROJECT,	version = "3.2.4.x", isCurrent=False,
			packages = [GLCTS_GLES2_KHR_PKG_1CFG,
						GLCTS_GLES2_KHR_PKG_N1CFG,
						GLCTS_GLES3_KHR_PKG_1CFG,
						GLCTS_GLES3_KHR_PKG_N1CFG,
						GLCTS_GLES31_KHR_PKG_1CFG,
						GLCTS_GLES31_KHR_PKG_N1CFG,
						GLCTS_3_2_3_GLES32_KHR_PKG_1CFG,
						GLCTS_3_2_3_GLES32_KHR_PKG_N1CFG,
						]),


	Mustpass(project = CTS_KHR_MP_NOCTX_ES_PROJECT, version = "3.2.4.x", isCurrent=False,
			packages = [GLCTS_NOCTX_ES2_KHR_PKG, GLCTS_NOCTX_ES32_KHR_PKG]),

	Mustpass(project = CTS_AOSP_MP_ES_PROJECT, version = "3.2.4.x", isCurrent=False,
			packages = [GLCTS_3_2_3_GLES2_PKG, GLCTS_3_2_3_GLES3_PKG, GLCTS_3_2_3_GLES31_PKG]),

	Mustpass(project = CTS_AOSP_MP_EGL_PROJECT, version = "3.2.4.x", isCurrent=False,
			packages = [GLCTS_3_2_3_EGL_PKG]),

	# 3.2.5.x

	Mustpass(project = CTS_KHR_MP_ES_PROJECT,   version = "3.2.5.x", isCurrent=False,
			packages = [GLCTS_GLES2_KHR_PKG_1CFG,
						GLCTS_GLES2_KHR_PKG_N1CFG,
						GLCTS_GLES3_KHR_PKG_1CFG,
						GLCTS_GLES3_KHR_PKG_N1CFG,
						GLCTS_GLES31_KHR_PKG_1CFG,
						GLCTS_GLES31_KHR_PKG_N1CFG,
						GLCTS_GLES32_KHR_PKG_1CFG,
						GLCTS_GLES32_KHR_PKG_N1CFG,
						]),

	Mustpass(project = CTS_KHR_MP_NOCTX_ES_PROJECT, version = "3.2.5.x", isCurrent=False,
			packages = [GLCTS_NOCTX_ES2_KHR_PKG, GLCTS_NOCTX_ES32_KHR_PKG]),

	Mustpass(project = CTS_AOSP_MP_ES_PROJECT, version = "3.2.5.x", isCurrent=False,
			packages = [GLCTS_3_2_3_GLES2_PKG, GLCTS_3_2_3_GLES3_PKG, GLCTS_3_2_3_GLES31_PKG]),

	Mustpass(project = CTS_AOSP_MP_EGL_PROJECT, version = "3.2.5.x", isCurrent=False,
			packages = [GLCTS_3_2_3_EGL_PKG]),

	# 3.2.6.x

	Mustpass(project = CTS_KHR_MP_ES_PROJECT,   version = "3.2.6.x", isCurrent=True,
			packages = [GLCTS_GLES2_KHR_PKG_1CFG,
						GLCTS_GLES2_KHR_PKG_N1CFG,
						GLCTS_GLES3_KHR_PKG_1CFG,
						GLCTS_GLES3_KHR_PKG_N1CFG,
						GLCTS_GLES31_KHR_PKG_1CFG,
						GLCTS_GLES31_KHR_PKG_N1CFG,
						GLCTS_GLES32_KHR_PKG_1CFG,
						GLCTS_GLES32_KHR_PKG_N1CFG,
						]),

	Mustpass(project = CTS_KHR_MP_NOCTX_ES_PROJECT, version = "3.2.6.x", isCurrent=True,
			packages = [GLCTS_NOCTX_ES2_KHR_PKG, GLCTS_NOCTX_ES32_KHR_PKG]),

	Mustpass(project = CTS_KHR_MP_SINGLE_ES_PROJECT, version = "3.2.6.x", isCurrent=True,
			packages = [GLCTS_SINGLE_ES32_KHR_PKG]),

	Mustpass(project = CTS_AOSP_MP_ES_PROJECT, version = "3.2.6.x", isCurrent=True,
			packages = [GLCTS_3_2_3_GLES2_PKG, GLCTS_3_2_3_GLES3_PKG, GLCTS_3_2_3_GLES31_PKG]),

	Mustpass(project = CTS_AOSP_MP_EGL_PROJECT, version = "3.2.6.x", isCurrent=True,
			packages = [GLCTS_3_2_3_EGL_PKG]),

	# master

	Mustpass(project = CTS_KHR_MP_ES_PROJECT,	version = "master", isCurrent=False,
			packages = [GLCTS_GLES2_KHR_PKG_1CFG,
						GLCTS_GLES2_KHR_PKG_N1CFG,
						GLCTS_GLES3_KHR_PKG_1CFG,
						GLCTS_GLES3_KHR_PKG_N1CFG,
						GLCTS_GLES31_KHR_PKG_1CFG,
						GLCTS_GLES31_KHR_PKG_N1CFG,
						GLCTS_GLES32_KHR_PKG_1CFG,
						GLCTS_GLES32_KHR_PKG_N1CFG,
						]),

	Mustpass(project = CTS_KHR_MP_NOCTX_ES_PROJECT, version = "master", isCurrent=False,
			packages = [GLCTS_NOCTX_ES2_KHR_PKG, GLCTS_NOCTX_ES32_KHR_PKG]),

	Mustpass(project = CTS_KHR_MP_SINGLE_ES_PROJECT, version = "master", isCurrent=False,
			packages = [GLCTS_SINGLE_ES32_KHR_PKG]),

	Mustpass(project = CTS_AOSP_MP_ES_PROJECT, version = "master", isCurrent=False,
			packages = [MASTER_GLES2_PKG, MASTER_GLES3_PKG, MASTER_GLES31_PKG]),

	Mustpass(project = CTS_AOSP_MP_EGL_PROJECT, version = "master", isCurrent=False,
			packages = [MASTER_EGL_PKG])

	]

ES_BUILD_CONFIG				= BuildConfig(buildPath, "Debug", ["-DDEQP_TARGET=%s" % DEFAULT_TARGET, "-DGLCTS_GTF_TARGET=gles32"])

#-------------------------------------------------- GL MUSTPASS----------------------------------------------------------------------

GL_CTS_MP_INC_DIR					= os.path.join(DEQP_DIR, "external", "openglcts", "modules", "runner")

GL_CTS_KHR_MP_DATA_DIR				= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "gl", "khronos_mustpass")

GL_CTS_KHR_MP_DEVICE_DIR			= "gl_cts/data/mustpass/gl/khronos_mustpass"

GL_CTS_KHR_MP_PROJECT				= Project(name = "Khronos Mustpass GL", path = GL_CTS_KHR_MP_DATA_DIR, incpath = GL_CTS_MP_INC_DIR, devicepath = GL_CTS_KHR_MP_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

GL_CTS_KHR_MP_NOCTX_DATA_DIR		= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "gl", "khronos_mustpass_noctx")

GL_CTS_KHR_MP_NOCTX_DEVICE_DIR		= "gl_cts/data/mustpass/gl/khronos_mustpass_noctx"

GL_CTS_NOCTX_PROJECT				= Project(name = "Khronos Mustpass GL NoContext", path = GL_CTS_KHR_MP_NOCTX_DATA_DIR, incpath = GL_CTS_MP_INC_DIR, devicepath = GL_CTS_KHR_MP_NOCTX_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

GL_CTS_KHR_MP_SINGLE_DATA_DIR		= os.path.join(DEQP_DIR, "external", "openglcts", "data", "mustpass", "gl", "khronos_mustpass_single")

GL_CTS_KHR_MP_SINGLE_DEVICE_DIR		= "gl_cts/data/mustpass/gl/khronos_mustpass_single"

GL_CTS_KHR_SINGLE_PROJECT			= Project(name = "Khronos Mustpass GL Single Config", path = GL_CTS_KHR_MP_SINGLE_DATA_DIR, incpath = GL_CTS_MP_INC_DIR, devicepath = GL_CTS_KHR_MP_SINGLE_DEVICE_DIR, copyright = COPYRIGHT_DECLARATION)

GL_MODULES							= OrderedDict([
			('KHR-GL46',		['master',		[include('gl46-master.txt'), exclude('gl46-test-issues.txt')]]),
			('KHR-GL45',		['master',		[include('gl45-master.txt'), exclude('gl45-test-issues.txt')]]),
			('KHR-GL44',		['master',		[include('gl44-master.txt'), exclude('gl44-test-issues.txt')]]),
			('KHR-GL43',		['master',		[include('gl43-master.txt'), exclude('gl43-test-issues.txt')]]),
			('KHR-GL42',		['master',		[include('gl42-master.txt'), exclude('gl42-test-issues.txt')]]),
			('KHR-GL41',		['master',		[include('gl41-master.txt'), exclude('gl41-test-issues.txt')]]),
			('KHR-GL40',		['master',		[include('gl40-master.txt'), exclude('gl40-test-issues.txt')]]),
			('KHR-GL33',		['master',		[include('gl33-master.txt'), exclude('gl33-test-issues.txt')]]),
			('KHR-GL32',		['master',		[include('gl32-master.txt'), exclude('gl32-test-issues.txt')]]),
			('KHR-GL31',		['master',		[include('gl31-master.txt'), exclude('gl31-test-issues.txt')]]),
			('KHR-GL30',		['master',		[include('gl30-master.txt'), exclude('gl30-test-issues.txt')]]),
			('GTF-GL46',		['gtf-master',	[include('gl46-gtf-master.txt')]]),
			('GTF-GL45',		['gtf-master',	[include('gl45-gtf-master.txt')]]),
			('GTF-GL44',		['gtf-master',	[include('gl44-gtf-master.txt')]]),
			('GTF-GL43',		['gtf-master',	[include('gl43-gtf-master.txt')]]),
			('GTF-GL42',		['gtf-master',	[include('gl42-gtf-master.txt')]]),
			('GTF-GL41',		['gtf-master',	[include('gl41-gtf-master.txt')]]),
			('GTF-GL40',		['gtf-master',	[include('gl40-gtf-master.txt')]]),
			('GTF-GL33',		['gtf-master',	[include('gl33-gtf-master.txt')]]),
			('GTF-GL32',		['gtf-master',	[include('gl32-gtf-master.txt')]]),
			('GTF-GL31',		['gtf-master',	[include('gl31-gtf-master.txt')]]),
			('GTF-GL30',		['gtf-master',	[include('gl30-gtf-master.txt')]])
		])

NOCTX_GL30_KHR_MODULE			= getModuleByName("KHR-NOCTX-GL30")
NOCTX_GL40_KHR_MODULE			= getModuleByName("KHR-NOCTX-GL40")
NOCTX_GL43_KHR_MODULE			= getModuleByName("KHR-NOCTX-GL43")
NOCTX_GL45_KHR_MODULE			= getModuleByName("KHR-NOCTX-GL45")
SINGLE_GL45_KHR_MODULE			= getModuleByName("KHR-Single-GL45")
SINGLE_GL46_KHR_MODULE			= getModuleByName("KHR-Single-GL46")

GLCTS_NOCTX_GL30_KHR_PKG			= Package(module = NOCTX_GL30_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gl30-khr-master.txt")]),
	])

GLCTS_NOCTX_GL40_KHR_PKG			= Package(module = NOCTX_GL40_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gl40-khr-master.txt")]),
	])

GLCTS_NOCTX_GL43_KHR_PKG			= Package(module = NOCTX_GL43_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gl43-khr-master.txt")]),
	])

GLCTS_NOCTX_GL45_KHR_PKG			= Package(module = NOCTX_GL45_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-master",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gl45-khr-master.txt")]),
	])

GLCTS_SINGLE_GL45_KHR_PKG			= Package(module = SINGLE_GL45_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-single",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gl45-khr-single.txt")]),
	])

GLCTS_SINGLE_GL46_KHR_PKG			= Package(module = SINGLE_GL46_KHR_MODULE, configurations = [
		# Master
		Configuration(name			= "khr-single",
					surfacewidth	= "64",
					surfaceheight	= "64",
					baseseed		= "1",
					filters			= [include("gl46-khr-single.txt")]),
	])

def generateGLMustpass():
		gl_packages = []
		for packageName in GL_MODULES:
			cfgName			= GL_MODULES[packageName][0]
			cfgFilter		= GL_MODULES[packageName][1]
			config_w64xh64	= Configuration(name = cfgName, surfacewidth = "64", surfaceheight = "64", baseseed = "1", filters = cfgFilter)
			config_w113xh47	= Configuration(name = cfgName, surfacewidth = "113", surfaceheight = "47", baseseed = "2", filters = cfgFilter)
			config_w64		= Configuration(name = cfgName, surfacewidth = "64", surfaceheight = "-1", baseseed = "3", fboconfig = "rgba8888d24s8", filters = cfgFilter)
			config_h64		= Configuration(name = cfgName, surfacewidth = "-1", surfaceheight = "64", baseseed = "3", fboconfig = "rgba8888d24s8", filters = cfgFilter)

			pkgModule		= getModuleByName(packageName)
			pkg0			= Package(module = pkgModule,
										useforfirsteglconfig = True,
										configurations = [
											config_w64xh64, config_w113xh47, config_w64, config_h64
										]
									)
			pkg1			= Package(module = pkgModule,
										useforfirsteglconfig = False,
										configurations = [
											config_w64xh64, config_w113xh47,
										]
									)
			gl_packages.append(pkg0)
			gl_packages.append(pkg1)

		mustpass = [Mustpass(project = GL_CTS_KHR_MP_PROJECT, version = "4.6.0.x", isCurrent=False, packages = gl_packages),
					Mustpass(project = GL_CTS_NOCTX_PROJECT, version = "4.6.0.x", isCurrent=False, packages = [GLCTS_NOCTX_GL30_KHR_PKG, GLCTS_NOCTX_GL40_KHR_PKG, GLCTS_NOCTX_GL43_KHR_PKG, GLCTS_NOCTX_GL45_KHR_PKG]),
				    Mustpass(project = GL_CTS_KHR_MP_PROJECT, version = "4.6.1.x", isCurrent=True, packages = gl_packages),
                    Mustpass(project = GL_CTS_NOCTX_PROJECT, version = "4.6.1.x", isCurrent=True, packages = [GLCTS_NOCTX_GL30_KHR_PKG, GLCTS_NOCTX_GL40_KHR_PKG, GLCTS_NOCTX_GL43_KHR_PKG, GLCTS_NOCTX_GL45_KHR_PKG]),
                    Mustpass(project = GL_CTS_KHR_SINGLE_PROJECT, version = "4.6.1.x", isCurrent=True, packages = [GLCTS_SINGLE_GL45_KHR_PKG, GLCTS_SINGLE_GL46_KHR_PKG]),
					]
		return mustpass

GL_BUILD_CONFIG					= BuildConfig(buildPath, "Debug", ["-DDEQP_TARGET=%s" % DEFAULT_TARGET, "-DGLCTS_GTF_TARGET=gl"])

if __name__ == "__main__":
	gtfCMakeLists = os.path.join(DEQP_DIR, "external", "kc-cts", "src", "GTF_ES", "CMakeLists.txt")
	if os.path.isfile(gtfCMakeLists) == False:
		raise Exception("GTF sources not found. GTF module is required to build the mustpass files. 'cd external && python fetch_kc_cts.py'")
	genMustpassLists(ES_MUSTPASS_LISTS, ANY_GENERATOR, ES_BUILD_CONFIG)
	gl_mustpass_lists = generateGLMustpass()
	genMustpassLists(gl_mustpass_lists, ANY_GENERATOR, GL_BUILD_CONFIG)
