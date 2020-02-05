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
from collections import defaultdict

VK_SOURCE						= khr_util.registry_cache.RegistrySource(
									"https://github.com/KhronosGroup/Vulkan-Docs.git",
									"xml/vk.xml",
									"22a5a1459fbe8be546079525c4c9d50bfc20b30b",
									"26b7945a5fd040e3a3adc578d8ff786d09a7cfeeb48ff1e62aeea8b8cb34fb5b")
VK_INL_FILE						= os.path.join(os.path.dirname(__file__), "..", "framework", "vulkan", "vkApiExtensionDependencyInfo.inl")
VK_INL_HEADER					= khr_util.format.genInlHeader("Khronos Vulkan API description (vk.xml)", VK_SOURCE.getRevision())

def VK_MAKE_VERSION(major, minor, patch):
	return (((major) << 22) | ((minor) << 12) | (patch))

VK_EXT_NOT_PROMOTED				= 0xFFFFFFFF
VK_EXT_TYPE_INSTANCE			= 0
VK_EXT_TYPE_DEVICE				= 1
VK_EXT_DEP_INSTANCE				= 'instanceExtensionDependencies'
VK_EXT_DEP_DEVICE				= 'deviceExtensionDependencies'
VK_EXT_API_VERSIONS				= 'releasedApiVersions'
VK_XML_EXT_DEPS					= 'requires'
VK_XML_EXT_NAME					= 'name'
VK_XML_EXT_PROMO				= 'promotedto'
VK_XML_EXT_REQUIRES_CORE		= 'requiresCore'
VK_XML_EXT_SUPPORTED			= 'supported'
VK_XML_EXT_SUPPORTED_VULKAN		= 'vulkan'
VK_XML_EXT_API					= 'api'
VK_XML_EXT_TYPE					= 'type'
VK_XML_EXT_TYPE_DEVICE			= 'device'
VK_XML_EXT_TYPE_INSTANCE		= 'instance'

def writeInlFile(filename, lines):
	khr_util.format.writeInlFile(filename, VK_INL_HEADER, lines)

def genExtDepArray(extDepsName, extDepsDict):
	yield 'static const std::tuple<deUint32, deUint32, const char*, const char*>\t{}[]\t='.format(extDepsName)
	yield '{'
	for ( major, minor, ext, extDeps ) in extDepsDict:
		for dep in extDeps:
			yield '\tstd::make_tuple({}, {}, "{}", "{}"),'.format(major, minor, ext, dep)
	yield '};'

def genApiVersions(name, apiVersions):
	yield 'static const std::tuple<deUint32, deUint32, deUint32>\t{}[]\t='.format(name)
	yield '{'
	for ( version, major, minor ) in apiVersions:
		yield '\tstd::make_tuple({}, {}, {}),'.format(version, major, minor)
	yield '};'

def genExtDepInl(dependenciesAndVersions):
	allExtDepsDict, apiVersions = dependenciesAndVersions
	apiVersions.reverse()
	lines = []

	lines = lines + [line for line in genExtDepArray(VK_EXT_DEP_INSTANCE, allExtDepsDict[VK_EXT_TYPE_INSTANCE])]
	lines = lines + [line for line in genExtDepArray(VK_EXT_DEP_DEVICE, allExtDepsDict[VK_EXT_TYPE_DEVICE])]
	lines = lines + [line for line in genApiVersions(VK_EXT_API_VERSIONS, apiVersions)]

	writeInlFile(VK_INL_FILE, lines)

class extInfo:
	def __init__(self):
		self.type	= VK_EXT_TYPE_INSTANCE
		self.core	= VK_MAKE_VERSION(1, 0, 0)
		self.promo	= VK_EXT_NOT_PROMOTED
		self.deps	= []

def genExtDepsOnApiVersion(ext, extInfoDict, apiVersion):
	deps = []

	for dep in extInfoDict[ext].deps:
		if apiVersion >= extInfoDict[dep].promo:
			continue

		deps.append(dep)

	return deps

def genExtDeps(extensionsAndVersions):
	extInfoDict, apiVersionID = extensionsAndVersions

	allExtDepsDict	= defaultdict(list)
	apiVersions		= []

	for (major,minor) in apiVersionID:
		apiVersions.append((VK_MAKE_VERSION(major, minor, 0),major,minor))
	apiVersions.sort(key=lambda x: x[0])

	for ext, info in extInfoDict.items():
		if info.deps == None:
			continue

		for (version,major,minor) in apiVersions:
			if info.core <= version:
				extDeps = genExtDepsOnApiVersion(ext, extInfoDict, version)
				if extDeps == None:
					continue
				allExtDepsDict[info.type].append( ( major, minor, ext, extDeps ) )

	for key, value in allExtDepsDict.items():
		value.sort(key=lambda x: x[2])
	return allExtDepsDict, apiVersions

def getExtInfoDict(vkRegistry):
	extInfoDict = {}
	apiVersionID = []

	for feature in vkRegistry.features:
		if feature.attrib[VK_XML_EXT_API] != VK_XML_EXT_SUPPORTED_VULKAN:
			continue
		featureName = feature.attrib[VK_XML_EXT_NAME].split('_')
		if len(featureName)!=4 or featureName[0] != 'VK' or featureName[1] != 'VERSION' :
			continue
		apiVersionID.append( (int(featureName[2]), int(featureName[3])) )

	apiVersionsByName	= {}
	apiVersionsByNumber	= {}
	for (major,minor) in apiVersionID:
		apiVersionsByName['VK_VERSION_{}_{}'.format(major,minor)]	= VK_MAKE_VERSION(major, minor, 0);
		apiVersionsByNumber['{}.{}'.format(major,minor)]			= VK_MAKE_VERSION(major, minor, 0);

	for ext in vkRegistry.extensions:
		if ext.attrib[VK_XML_EXT_SUPPORTED] != VK_XML_EXT_SUPPORTED_VULKAN:
			continue

		name				= ext.attrib[VK_XML_EXT_NAME]
		extInfoDict[name]	= extInfo()
		if ext.attrib[VK_XML_EXT_TYPE] == VK_XML_EXT_TYPE_DEVICE:
			extInfoDict[name].type = VK_EXT_TYPE_DEVICE
		if VK_XML_EXT_REQUIRES_CORE in ext.attrib and ext.attrib[VK_XML_EXT_REQUIRES_CORE] in apiVersionsByNumber:
			extInfoDict[name].core = apiVersionsByNumber[ext.attrib[VK_XML_EXT_REQUIRES_CORE]]
		if VK_XML_EXT_PROMO in ext.attrib and ext.attrib[VK_XML_EXT_PROMO] in apiVersionsByName :
			extInfoDict[name].promo = apiVersionsByName[ext.attrib[VK_XML_EXT_PROMO]]
		if VK_XML_EXT_DEPS in ext.attrib:
			extInfoDict[name].deps = ext.attrib[VK_XML_EXT_DEPS].split(',')

	return extInfoDict, apiVersionID

def getVKRegistry():
	return khr_util.registry_cache.getRegistry(VK_SOURCE)

if __name__ == '__main__':
	genExtDepInl(genExtDeps(getExtInfoDict(getVKRegistry())))
