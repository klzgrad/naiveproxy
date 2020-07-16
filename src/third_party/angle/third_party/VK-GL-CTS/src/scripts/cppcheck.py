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
import sys
import shlex
import subprocess

SRC_DIRS = [
	"delibs/debase",
	"delibs/deimage",
	"delibs/depool",
	"delibs/dethread",
	"delibs/deutil",
	"delibs/decpp",

	"deqp/execserver",
	"deqp/executor",
	"deqp/modules/candytest",
	"deqp/modules/egl",
	"deqp/modules/gles2",
	"deqp/modules/gles3",
	"deqp/modules/gles31",
	"deqp/modules/gl3",
	"deqp/modules/glshared",
	"deqp/modules/glusecases",
	"deqp/modules/opencl",
	"deqp/modules/internal",
	"deqp/framework/qphelper",
	"deqp/framework/common",
	"deqp/framework/egl",
	"deqp/framework/opengl",
	"deqp/framework/opencl",
	"deqp/framework/platform",
	"deqp/framework/randomshaders",
	"deqp/framework/referencerenderer",
	"deqp/wrappers/dynlib",
	"deqp/wrappers/gles3",

	"gapir",
]

INCLUDE_DIRS = [
	"delibs/libpng",
	"delibs/libzip",
	"delibs/zlib",

	"deqp/wrappers/dynlib/inc",
	"deqp/wrappers/gles3/inc",
	"deqp/modules/gles2/accuracy",
	"deqp/modules/gles2/functional",
	"deqp/modules/gles2/performance",
	"deqp/modules/gles2/stress",
	"deqp/modules/gles2/usecases",
	"deqp/modules/gles3/accuracy",
	"deqp/modules/gles3/functional",
	"deqp/modules/gles3/stress",
	"deqp/modules/gles3/usecases",
	"deqp/modules/gles3/performance",
	"deqp/modules/gles31/functional",
	"deqp/modules/gles31/stress",
	"deqp/modules/gl3/functional",
	"deqp/modules/gl3/performance",
	"deqp/modules/gl3/stress",
	"deqp/framework/opengl/simplereference",
	"deqp/framework/opencl/inc",
	"deqp/framework/opengl/wrapper",
	"deqp/framework/opengl/simplereference",

	"gapir/base",
	"gapir/egl",
	"gapir/gles2",
	"gapir/util",

	"domeni/eigen2",
	"domeni/base",
	"domeni/engine",
	"domeni/m3g",
	"domeni/m3g_adapter",
	"domeni/renderer",
	"domeni/resource",
	"domeni/tools"
] + SRC_DIRS

ARGS = [
	"--enable=all,style",
	"--xml-version=2",
	"--platform=win64",
	"-D__cplusplus",
	"-D_M_X64",
	"-D_WIN32",
	"-D_MSC_VER=1600",
	"-DDE_DEBUG=1",
	"-DDE_COMPILER=1", # Is preprocessor buggy in recent cppcheck?
	"-DDE_OS=1",
	"-DDE_CPU=1",
	"-DDE_PTR_SIZE=4",
	"-DAB_COMPILER=1",
	"-DAB_OS=1",
	"-DDEQP_TARGET_NAME=\"Cppcheck\"",
	"-D_XOPEN_SOURCE=600",
	"--suppress=arrayIndexOutOfBounds:deqp/framework/common/tcuVector.hpp",
	"--suppress=invalidPointerCast:deqp/framework/common/tcuTexture.cpp",
	"--suppress=*:deqp/framework/opencl/cl.hpp",
	"--suppress=invalidPointerCast:deqp/modules/opencl/tclSIRLogger.cpp",
	"--suppress=preprocessorErrorDirective:deqp/framework/platform/android/tcuAndroidMain.cpp",
	"--suppress=invalidPointerCast:deqp/modules/gles3/functional/es3fTransformFeedbackTests.cpp",
	"--suppress=invalidPointerCast:deqp/modules/gles3/functional/es3fUniformBlockCase.cpp",
	"--suppress=unusedStructMember",
	"--suppress=postfixOperator",
	"--suppress=unusedFunction",
	"--suppress=unusedPrivateFunction",
	"--rule-file=deqp/scripts/no_empty_fail.rule"
]

def runCppCheck (srcBaseDir, dstFile):
	fullDstFile	= os.path.realpath(dstFile)
	command		= '"C:\\Program Files (x86)\\Cppcheck\\cppcheck.exe"'

	for arg in ARGS + ["--xml"]:
		command += " %s" % arg

	for path in INCLUDE_DIRS:
		command += " -I %s" % path

	for path in SRC_DIRS:
		command += " %s" % path

	command += ' 2> "%s"' % fullDstFile

	os.chdir(srcBaseDir)
	os.system('"%s"' % command) # Double-quotes needed for some reason

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("%s: [reportfile]" % sys.argv[0])
		sys.exit(-1)

	dstFile	= sys.argv[1]
	srcDir	= os.path.realpath(os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..")))
	runCppCheck(srcDir, dstFile)
