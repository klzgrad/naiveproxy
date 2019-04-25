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

from common import getEGLRegistry, getInterface, getDefaultInterface, VERSION

import str_util
import call_log_wrapper
import proc_address_tests
import enums
import func_ptrs
import library
import gtf_wrapper

def gen ():
	registry	= getEGLRegistry()
	iface		= getDefaultInterface()

	str_util.gen(iface)
	call_log_wrapper.gen(iface)
	proc_address_tests.gen()
	enums.gen(iface)
	func_ptrs.gen(iface)
	library.gen(registry)
	gtf_wrapper.gen(registry)
