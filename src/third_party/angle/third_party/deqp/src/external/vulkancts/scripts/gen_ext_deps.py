# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# drawElements Quality Program utilities
# --------------------------------------
#
# Copyright 2018 The Android Open Source Project
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

import khr_util.format
import khr_util.registry_cache

VK_SOURCE						= khr_util.registry_cache.RegistrySource(
									"https://github.com/KhronosGroup/Vulkan-Docs.git",
									"xml/vk.xml",
									"9f2171b41192282a9957c43a37d5d8c6a982abed",
									"e7d8761b09a76c85d5f949bf6c930407dcea34f679b09ed6b4bf1398bd2e7742")
VK_INL_FILE						= os.path.join(os.path.dirname(__file__), "..", "framework", "vulkan", "vkApiExtensionDependencyInfo.inl")
VK_INL_HEADER					= khr_util.format.genInlHeader("Khronos Vulkan API description (vk.xml)", VK_SOURCE.getRevision())

def VK_MAKE_VERSION(major, minor, patch):
	return (((major) << 22) | ((minor) << 12) | (patch))

VK_API_VERSION_1_0				= VK_MAKE_VERSION(1, 0, 0)
VK_API_VERSION_1_1				= VK_MAKE_VERSION(1, 1, 0)
VK_EXT_NOT_PROMOTED				= 0xFFFFFFFF
VK_EXT_TYPE_INST				= 0
VK_EXT_TYPE_DEV					= 1
VK_INST_EXT_DEP_1_0				= 'instanceExtensionDependencies_1_0'
VK_INST_EXT_DEP_1_1				= 'instanceExtensionDependencies_1_1'
VK_DEV_EXT_DEP_1_0				= 'deviceExtensionDependencies_1_0'
VK_DEV_EXT_DEP_1_1				= 'deviceExtensionDependencies_1_1'
VK_XML_EXT_DEPS					= 'requires'
VK_XML_EXT_NAME					= 'name'
VK_XML_EXT_PROMO				= 'promotedto'
VK_XML_EXT_PROMO_1_1			= 'VK_VERSION_1_1'
VK_XML_EXT_REQUIRES_CORE		= 'requiresCore'
VK_XML_EXT_REQUIRES_CORE_1_1	= '1.1'
VK_XML_EXT_SUPPORTED			= 'supported'
VK_XML_EXT_SUPPORTED_VULKAN		= 'vulkan'
VK_XML_EXT_TYPE					= 'type'
VK_XML_EXT_TYPE_DEVICE			= 'device'
VK_XML_EXT_TYPE_INSTANCE		= 'instance'

def writeInlFile(filename, lines):
	khr_util.format.writeInlFile(filename, VK_INL_HEADER, lines)

def genExtDepArray(extDepsName, extDepsDict):
	yield 'static const std::pair<const char*, const char*>\t%s[]\t=' % extDepsName
	yield '{'
	for ext in sorted(extDepsDict.keys()):
		for dep in extDepsDict[ext]:
			yield '\tstd::make_pair("%s", "%s"),' % (ext, dep)
	yield '};'

def genExtDepInl(allExtDepsDict):
	lines = []

	if VK_INST_EXT_DEP_1_0 in allExtDepsDict:
		lines = lines + [line for line in genExtDepArray(VK_INST_EXT_DEP_1_0, allExtDepsDict[VK_INST_EXT_DEP_1_0])]
	if VK_INST_EXT_DEP_1_1 in allExtDepsDict:
		lines = lines + [line for line in genExtDepArray(VK_INST_EXT_DEP_1_1, allExtDepsDict[VK_INST_EXT_DEP_1_1])]
	if VK_DEV_EXT_DEP_1_0 in allExtDepsDict:
		lines = lines + [line for line in genExtDepArray(VK_DEV_EXT_DEP_1_0, allExtDepsDict[VK_DEV_EXT_DEP_1_0])]
	if VK_DEV_EXT_DEP_1_1 in allExtDepsDict:
		lines = lines + [line for line in genExtDepArray(VK_DEV_EXT_DEP_1_1, allExtDepsDict[VK_DEV_EXT_DEP_1_1])]

	writeInlFile(VK_INL_FILE, lines)

class extInfo:
	def __init__(self):
		self.type	= VK_EXT_TYPE_INST
		self.core	= VK_API_VERSION_1_0
		self.promo	= VK_EXT_NOT_PROMOTED
		self.deps	= []

def genExtDepsOnApiVersion(ext, extInfoDict, apiVersion):
	deps = []

	for dep in extInfoDict[ext].deps:
		if apiVersion >= extInfoDict[dep].promo:
			continue

		deps.append(dep)

	return deps

def genExtDeps(extInfoDict):
	allExtDepsDict						= {}
	allExtDepsDict[VK_INST_EXT_DEP_1_0]	= {}
	allExtDepsDict[VK_INST_EXT_DEP_1_1]	= {}
	allExtDepsDict[VK_DEV_EXT_DEP_1_0]	= {}
	allExtDepsDict[VK_DEV_EXT_DEP_1_1]	= {}

	for ext, info in extInfoDict.items():
		if info.deps == None:
			continue

		if info.type == VK_EXT_TYPE_INST:
			allExtDepsDict[VK_INST_EXT_DEP_1_1][ext]	= genExtDepsOnApiVersion(ext, extInfoDict, VK_API_VERSION_1_1);
			if info.core >= VK_API_VERSION_1_1:
				continue
			allExtDepsDict[VK_INST_EXT_DEP_1_0][ext]	= genExtDepsOnApiVersion(ext, extInfoDict, VK_API_VERSION_1_0);
		else:
			allExtDepsDict[VK_DEV_EXT_DEP_1_1][ext]		= genExtDepsOnApiVersion(ext, extInfoDict, VK_API_VERSION_1_1);
			if info.core >= VK_API_VERSION_1_1:
				continue
			allExtDepsDict[VK_DEV_EXT_DEP_1_0][ext]		= genExtDepsOnApiVersion(ext, extInfoDict, VK_API_VERSION_1_0);

	return allExtDepsDict

def getExtInfoDict(vkRegistry):
	extInfoDict = {}

	for ext in vkRegistry.extensions:
		if (ext.attrib[VK_XML_EXT_SUPPORTED] != VK_XML_EXT_SUPPORTED_VULKAN):
			continue

		name				= ext.attrib[VK_XML_EXT_NAME]
		extInfoDict[name]	= extInfo()
		if ext.attrib[VK_XML_EXT_TYPE] == VK_XML_EXT_TYPE_DEVICE:
			extInfoDict[name].type = VK_EXT_TYPE_DEV
		if VK_XML_EXT_REQUIRES_CORE in ext.attrib:
			if ext.attrib[VK_XML_EXT_REQUIRES_CORE] == VK_XML_EXT_REQUIRES_CORE_1_1:
				extInfoDict[name].core = VK_API_VERSION_1_1
		if VK_XML_EXT_PROMO in ext.attrib:
			if ext.attrib[VK_XML_EXT_PROMO] == VK_XML_EXT_PROMO_1_1:
				extInfoDict[name].promo = VK_API_VERSION_1_1
		if VK_XML_EXT_DEPS in ext.attrib:
			extInfoDict[name].deps = ext.attrib[VK_XML_EXT_DEPS].split(',')

	return extInfoDict

def getVKRegistry():
	return khr_util.registry_cache.getRegistry(VK_SOURCE)

if __name__ == '__main__':
	genExtDepInl(genExtDeps(getExtInfoDict(getVKRegistry())))
