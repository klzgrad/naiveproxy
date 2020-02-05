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

import logging
import egl
import sys

def gen ():
	egl.gen()

if __name__ == "__main__":
	# https://docs.python.org/3/howto/logging.html#what-happens-if-no-configuration-is-provided
	# To obtain the pre-3.2 behaviour, logging.lastResort can be set to None.
	if (sys.version_info >= (3, 2)):
		logging.lastResort=None

	gen()
