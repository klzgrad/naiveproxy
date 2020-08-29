# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Vulkan CTS
# ----------
#
# Copyright (c) 2016 Google Inc.
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

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

from build.common import DEQP_DIR
from build.config import ANY_GENERATOR
from build_caselists import Module, getModuleByName, getBuildConfig, DEFAULT_BUILD_DIR, DEFAULT_TARGET
from mustpass import Project, Package, Mustpass, Configuration, include, exclude, genMustpassLists, parseBuildConfigFromCmdLineArgs

COPYRIGHT_DECLARATION = """
	 Licensed under the Apache License, Version 2.0 (the "License");
	 you may not use this file except in compliance with the License.
	 You may obtain a copy of the License at

		  http://www.apache.org/licenses/LICENSE-2.0

	 Unless required by applicable law or agreed to in writing, software
	 distributed under the License is distributed on an "AS IS" BASIS,
	 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	 See the License for the specific language governing permissions and
	 limitations under the License.
	 """

MUSTPASS_PATH		= os.path.join(DEQP_DIR, "external", "vulkancts", "mustpass")
PROJECT				= Project(path = MUSTPASS_PATH, copyright = COPYRIGHT_DECLARATION)
VULKAN_MODULE		= getModuleByName("dEQP-VK")
BUILD_CONFIG		= getBuildConfig(DEFAULT_BUILD_DIR, DEFAULT_TARGET, "Debug")

# master

VULKAN_MASTER_PKG	= Package(module = VULKAN_MODULE, configurations = [
		  # Master
		  Configuration(name		= "default",
						filters		= [include("master.txt"),
									   exclude("test-issues.txt"),
									   exclude("excluded-tests.txt"),
									   exclude("android-tests.txt")]),
		  Configuration(name		= "fraction-mandatory-tests",
						filters		= [include("fraction-mandatory-tests.txt")]),
	 ])

MUSTPASS_LISTS		= [
		  Mustpass(project = PROJECT,	version = "master",	packages = [VULKAN_MASTER_PKG]),
	]

if __name__ == "__main__":
	genMustpassLists(MUSTPASS_LISTS, ANY_GENERATOR, parseBuildConfigFromCmdLineArgs())