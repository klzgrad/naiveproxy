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

from src_util import getGLRegistry, getHybridInterface
from gen_call_log_wrapper import genCallLogWrapper
from gen_enums import genEnums
from gen_versions import genVersions
from gen_es_direct_init import genESDirectInit
from gen_es_static_library import genESStaticLibrary
from gen_ext_init import genExtInit
from gen_func_init import genFuncInit
from gen_func_ptrs import genFunctionPointers
from gen_null_render_context import genNullRenderContext
from gen_str_util import genStrUtil
from gen_wrapper import genWrapper
from gen_query_util import genQueryUtil
import logging
import sys

def genAll ():
	# https://docs.python.org/3/howto/logging.html#what-happens-if-no-configuration-is-provided
	# To obtain the pre-3.2 behaviour, logging.lastResort can be set to None.
	if (sys.version_info >= (3, 2)):
		logging.lastResort=None

	registry = getGLRegistry()
	iface = getHybridInterface()
	genCallLogWrapper(iface)
	genEnums(iface)
	genVersions(iface)
	genESDirectInit(registry)
	genESStaticLibrary(registry)
	genExtInit(registry, iface)
	genFuncInit(registry)
	genFunctionPointers(iface)
	genNullRenderContext(iface)
	genStrUtil(iface)
	genWrapper(iface)
	genQueryUtil(iface)

if __name__ == "__main__":
	genAll()
