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
import shutil

from . common import *
from . config import *

def initBuildDir (config, generator):
	cfgArgs = []

	# Build base configuration args
	cfgArgs += config.getArgs()

	# Generator args
	cfgArgs += generator.getGenerateArgs(config.getBuildType())

	if not os.path.exists(config.buildDir):
		os.makedirs(config.buildDir)

	pushWorkingDir(config.getBuildDir())

	try:
		execute([config.getCMakePath(), config.getSrcPath()] + cfgArgs)
	finally:
		popWorkingDir()

def build (config, generator, targets = None):
	if os.path.exists(config.buildDir):
		try:
			initBuildDir(config, generator)
		except Exception as e:
			print(e)
			print("WARNING: Using existing build directory failed; re-creating build directory")
			shutil.rmtree(config.buildDir)
			initBuildDir(config, generator)
	else:
		initBuildDir(config, generator)

	baseCmd		= [config.getCMakePath(), '--build', '.']
	buildArgs	= generator.getBuildArgs(config.getBuildType())

	pushWorkingDir(config.getBuildDir())

	if targets == None:
		execute(baseCmd + buildArgs)
	else:
		for target in targets:
			execute(baseCmd + ['--target', target] + buildArgs)

	popWorkingDir()
