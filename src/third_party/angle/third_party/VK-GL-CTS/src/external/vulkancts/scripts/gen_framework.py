# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------
# Vulkan CTS
# ----------
#
# Copyright (c) 2015 Google Inc.
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
import re
import sys
import copy
from itertools import chain
from collections import OrderedDict

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

from build.common import DEQP_DIR
from khr_util.format import indentLines, writeInlFile

VULKAN_H_DIR	= os.path.join(os.path.dirname(__file__), "src")
VULKAN_DIR		= os.path.join(os.path.dirname(__file__), "..", "framework", "vulkan")

INL_HEADER = """\
/* WARNING: This is auto-generated file. Do not modify, since changes will
 * be lost! Modify the generating script instead.
 */\
"""

DEFINITIONS			= [
	("VK_MAX_PHYSICAL_DEVICE_NAME_SIZE",	"size_t"),
	("VK_MAX_EXTENSION_NAME_SIZE",			"size_t"),
	("VK_MAX_DRIVER_NAME_SIZE",				"size_t"),
	("VK_MAX_DRIVER_INFO_SIZE",				"size_t"),
	("VK_UUID_SIZE",						"size_t"),
	("VK_LUID_SIZE",						"size_t"),
	("VK_MAX_MEMORY_TYPES",					"size_t"),
	("VK_MAX_MEMORY_HEAPS",					"size_t"),
	("VK_MAX_DESCRIPTION_SIZE",				"size_t"),
	("VK_MAX_DEVICE_GROUP_SIZE",			"size_t"),
	("VK_ATTACHMENT_UNUSED",				"deUint32"),
	("VK_SUBPASS_EXTERNAL",					"deUint32"),
	("VK_QUEUE_FAMILY_IGNORED",				"deUint32"),
	("VK_QUEUE_FAMILY_EXTERNAL",			"deUint32"),
	("VK_REMAINING_MIP_LEVELS",				"deUint32"),
	("VK_REMAINING_ARRAY_LAYERS",			"deUint32"),
	("VK_WHOLE_SIZE",						"vk::VkDeviceSize"),
	("VK_TRUE",								"vk::VkBool32"),
	("VK_FALSE",							"vk::VkBool32"),
]

PLATFORM_TYPES		= [
	# VK_KHR_xlib_surface
	(["Display","*"],						["XlibDisplayPtr"],				"void*"),
	(["Window"],							["XlibWindow"],					"deUintptr",),
	(["VisualID"],							["XlibVisualID"],				"deUint32"),

	# VK_KHR_xcb_surface
	(["xcb_connection_t", "*"],				["XcbConnectionPtr"],			"void*"),
	(["xcb_window_t"],						["XcbWindow"],					"deUintptr"),
	(["xcb_visualid_t"],					["XcbVisualid"],				"deUint32"),

	# VK_KHR_wayland_surface
	(["struct", "wl_display","*"],			["WaylandDisplayPtr"],			"void*"),
	(["struct", "wl_surface", "*"],			["WaylandSurfacePtr"],			"void*"),

	# VK_KHR_mir_surface
	(["MirConnection", "*"],				["MirConnectionPtr"],			"void*"),
	(["MirSurface", "*"],					["MirSurfacePtr"],				"void*"),

	# VK_KHR_android_surface
	(["ANativeWindow", "*"],				["AndroidNativeWindowPtr"],		"void*"),

	# VK_KHR_win32_surface
	(["HINSTANCE"],							["Win32InstanceHandle"],		"void*"),
	(["HWND"],								["Win32WindowHandle"],			"void*"),
	(["HANDLE"],							["Win32Handle"],				"void*"),
	(["const", "SECURITY_ATTRIBUTES", "*"],	["Win32SecurityAttributesPtr"],	"const void*"),
	(["AHardwareBuffer", "*"],				["AndroidHardwareBufferPtr"],	"void*"),
	(["HMONITOR"],							["Win32MonitorHandle"],			"void*"),
	(["LPCWSTR"],							["Win32LPCWSTR"],				"const void*"),

	# VK_EXT_acquire_xlib_display
	(["RROutput"],							["RROutput"],					"void*"),

	(["zx_handle_t"],						["zx_handle_t"],				"deInt32"),
	(["GgpFrameToken"],						["GgpFrameToken"],				"deInt32"),
	(["GgpStreamDescriptor"],				["GgpStreamDescriptor"],		"deInt32"),
	(["CAMetalLayer"],						["CAMetalLayer"],				"void*"),
]

PLATFORM_TYPE_NAMESPACE	= "pt"

TYPE_SUBSTITUTIONS		= [
	("uint8_t",		"deUint8"),
	("uint16_t",	"deUint16"),
	("uint32_t",	"deUint32"),
	("uint64_t",	"deUint64"),
	("int8_t",		"deInt8"),
	("int16_t",		"deInt16"),
	("int32_t",		"deInt32"),
	("int64_t",		"deInt64"),
	("bool32_t",	"deUint32"),
	("size_t",		"deUintptr"),

	# Platform-specific
	("DWORD",		"deUint32"),
	("HANDLE*",		PLATFORM_TYPE_NAMESPACE + "::" + "Win32Handle*"),
]

EXTENSION_POSTFIXES				= ["KHR", "EXT", "NV", "NVX", "KHX", "NN", "MVK", "FUCHSIA", "GGP", "AMD"]
EXTENSION_POSTFIXES_STANDARD	= ["KHR", "EXT"]

def prefixName (prefix, name):
	name = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name[2:])
	name = re.sub(r'([a-zA-Z])([0-9])', r'\1_\2', name)
	name = name.upper()

	name = name.replace("YCB_CR_", "YCBCR_")
	name = name.replace("WIN_32_", "WIN32_")
	name = name.replace("8_BIT_", "8BIT_")
	name = name.replace("16_BIT_", "16BIT_")
	name = name.replace("INT_64_", "INT64_")
	name = name.replace("D_3_D_12_", "D3D12_")
	name = name.replace("IOSSURFACE_", "IOS_SURFACE_")
	name = name.replace("MAC_OS", "MACOS_")
	name = name.replace("TEXTURE_LOD", "TEXTURE_LOD_")
	name = name.replace("VIEWPORT_W", "VIEWPORT_W_")
	name = name.replace("_IDPROPERTIES", "_ID_PROPERTIES")
	name = name.replace("PHYSICAL_DEVICE_SHADER_FLOAT_16_INT_8_FEATURES", "PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES")
	name = name.replace("_PCIBUS_", "_PCI_BUS_")
	name = name.replace("ASTCD", "ASTC_D")
	name = name.replace("AABBNV", "AABB_NV")
	name = name.replace("IMAGE_PIPE", "IMAGEPIPE")
	name = name.replace("SMBUILTINS", "SM_BUILTINS")
	name = name.replace("ASTCHDRFEATURES", "ASTC_HDR_FEATURES")
	name = name.replace("UINT_8", "UINT8")
	name = name.replace("VULKAN_11_FEATURES", "VULKAN_1_1_FEATURES")
	name = name.replace("VULKAN_11_PROPERTIES", "VULKAN_1_1_PROPERTIES")
	name = name.replace("VULKAN_12_FEATURES", "VULKAN_1_2_FEATURES")
	name = name.replace("VULKAN_12_PROPERTIES", "VULKAN_1_2_PROPERTIES")
	name = name.replace("INT_8_", "INT8_")
	name = name.replace("AABBNV", "AABB_NV")

	return prefix + name

class Version:
	def __init__ (self, versionTuple):
		self.major = versionTuple[0]
		self.minor = versionTuple[1]
		self.patch = versionTuple[2]

	def getInHex (self):
		if self.patch == 0:
			return "VK_API_VERSION_%d_%d" % (self.major, self.minor)
		return '0x%Xu' % (hash(self))

	def isStandardVersion (self):
		if self.patch != 0:
			return False
		if self.major != 1:
			return False
		return True

	def getBestRepresentation (self):
		if self.isStandardVersion():
			return self.getInHex()
		return self.getDefineName()

	def getDefineName (self):
		return 'VERSION_%d_%d_%d' % (self.major, self.minor, self.patch)

	def __hash__ (self):
		return (self.major << 22) | (self.minor << 12) | self.patch

	def __eq__ (self, other):
		return self.major == other.major and self.minor == other.minor and self.patch == other.patch

	def __str__ (self):
		return self.getBestRepresentation()


class Handle:
	TYPE_DISP		= 0
	TYPE_NONDISP	= 1

	def __init__ (self, type, name):
		self.type		= type
		self.name		= name
		self.alias		= None
		self.isAlias	= False

	def getHandleType (self):
		return prefixName("HANDLE_TYPE_", self.name)

	def checkAliasValidity (self):
		pass

	def __repr__ (self):
		return '%s (%s, %s)' % (self.name, self.alias, self.isAlias)

class Definition:
	def __init__ (self, type, name, value):
		self.type	= type
		self.name	= name
		self.value	= value
		self.alias	= None
		self.isAlias	= False

	def __repr__ (self):
		return '%s = %s (%s)' % (self.name, self.value, self.type)

class Enum:
	def __init__ (self, name, values):
		self.name		= name
		self.values		= values
		self.alias		= None
		self.isAlias	= False

	def checkAliasValidity (self):
		if self.alias != None:
			if len(self.values) != len(self.alias.values):
				raise Exception("%s has different number of flags than its alias %s." % (self.name, self.alias.name))
			for index, value in enumerate(self.values):
				aliasVal = self.alias.values[index]
				if value[1] != aliasVal[1] or not (value[0].startswith(aliasVal[0]) or aliasVal[0].startswith(value[0])):
					raise Exception("Flag %s of %s has different value than %s of %s." % (self.alias.values[index], self.alias.name, value, self.name))

	def __repr__ (self):
		return '%s (%s) %s' % (self.name, self.alias, self.values)

class Bitfield:
	def __init__ (self, name, values):
		self.name		= name
		self.values		= values
		self.alias		= None
		self.isAlias	= False

	def checkAliasValidity (self):
		if self.alias != None:
			if len(self.values) != len(self.alias.values):
				raise Exception("%s has different number of flags than its alias %s." % (self.name, self.alias.name))
			for index, value in enumerate(self.values):
				aliasVal = self.alias.values[index]
				if value[1] != aliasVal[1] or not (value[0].startswith(aliasVal[0]) or aliasVal[0].startswith(value[0])):
					raise Exception("Flag %s of %s has different value than %s of %s." % (self.alias.values[index], self.alias.name, value, self.name))

	def __repr__ (self):
		return '%s (%s)' % (self.name, self.alias)

class Variable:
	def __init__ (self, type, name, arraySizeOrFieldWidth):
		type		= type.replace('*',' *').replace('&',' &')
		for src, dst in TYPE_SUBSTITUTIONS:
			type = type.replace(src, dst)
		self.type	= type.split(' ')
		for platformType, substitute, compat in PLATFORM_TYPES:
			range = self.contains(self.type, platformType)
			if range != None:
				self.type = self.type[:range[0]]+[PLATFORM_TYPE_NAMESPACE + '::' + substitute[0]] + substitute[1:] + self.type[range[1]:]
				break
		self.name		= name
		if len(arraySizeOrFieldWidth) > 0 and arraySizeOrFieldWidth[0] == ':':
			self.arraySize	= ''
			self.fieldWidth = arraySizeOrFieldWidth
		else:
			self.arraySize	= arraySizeOrFieldWidth
			self.fieldWidth = ''

	def contains(self, big, small):
		for i in range(len(big)-len(small)+1):
			for j in range(len(small)):
				if big[i+j] != small[j]:
					break
			else:
				return i, i+len(small)
		return None

	def getType (self):
		return ' '.join(self.type).replace(' *','*').replace(' &','&')

	def getAsString (self, separator):
		return '%s%s%s%s%s' % (self.getType(), separator, self.name, self.arraySize, self.fieldWidth)

	def __repr__ (self):
		return '<%s> <%s> <%s>' % (self.type, self.name, self.arraySize)

	def __eq__ (self, other):
		if len(self.type) != len(other.type):
			return False
		for index, type in enumerate(self.type):
			if "*" == type or "&" == type or "const" == type or "volatile" == type:
				if type != other.type[index]:
					return False
			elif type != other.type[index] and \
				type not in map(lambda ext: other.type[index] + ext, EXTENSION_POSTFIXES_STANDARD) and \
				other.type[index] not in map(lambda ext: type + ext, EXTENSION_POSTFIXES_STANDARD):
				return False
		return self.arraySize == other.arraySize

	def __ne__ (self, other):
		return not self == other

class CompositeType:
	CLASS_STRUCT	= 0
	CLASS_UNION		= 1

	def __init__ (self, typeClass, name, members, apiVersion = None):
		self.typeClass	= typeClass
		self.name		= name
		self.members	= members
		self.alias		= None
		self.isAlias	= False
		self.apiVersion	= apiVersion

	def getClassName (self):
		names = {CompositeType.CLASS_STRUCT: 'struct', CompositeType.CLASS_UNION: 'union'}
		return names[self.typeClass]

	def checkAliasValidity (self):
		if self.alias != None:
			if len(self.members) != len(self.alias.members):
				raise Exception("%s has different number of members than its alias %s." % (self.name, self.alias.name))
			for index, member in enumerate(self.members ):
				break
				#if member != self.alias.members[index]:
					#raise Exception("Member %s of %s is different than core member %s in %s." % (self.alias.members[index], self.alias.name, member, self.name))
					#raise Exception("Member ",str(self.alias.members[index])," of ", str(self.alias.name)," is different than core member ", str(member)," in ", str(self.name),".")
	def __repr__ (self):
		return '%s (%s)' % (self.name, self.alias)

class Function:
	TYPE_PLATFORM		= 0 # Not bound to anything
	TYPE_INSTANCE		= 1 # Bound to VkInstance
	TYPE_DEVICE			= 2 # Bound to VkDevice

	def __init__ (self, name, returnType, arguments, apiVersion = None):
		self.name		= name
		self.returnType	= returnType
		self.arguments	= arguments
		self.alias		= None
		self.isAlias	= False
		self.apiVersion	= apiVersion

	def getType (self):
		# Special functions
		if self.name == "vkGetInstanceProcAddr":
			return Function.TYPE_PLATFORM
		assert len(self.arguments) > 0
		firstArgType = self.arguments[0].getType()
		if firstArgType in ["VkInstance", "VkPhysicalDevice"]:
			return Function.TYPE_INSTANCE
		elif firstArgType in ["VkDevice", "VkCommandBuffer", "VkQueue"]:
			return Function.TYPE_DEVICE
		else:
			return Function.TYPE_PLATFORM

	def checkAliasValidity (self):
		if self.alias != None:
			if len(self.arguments) != len(self.alias.arguments):
				raise Exception("%s has different number of arguments than its alias %s." % (self.name, self.alias.name))
			if self.returnType != self.alias.returnType or not (self.returnType.startswith(self.alias.returnType) or self.alias.returnType.startswith(self.returnType)):
				raise Exception("%s has different return value's type than its alias %s." % (self.name, self.alias.name))
			for index, argument in enumerate(self.arguments):
				if argument != self.alias.arguments[index]:
					raise Exception("argument %s: \"%s\" of %s is different than \"%s\" of %s." % (index, self.alias.arguments[index].getAsString(' '), self.alias.name, argument.getAsString(' '), self.name))

	def __repr__ (self):
		return '%s (%s)' % (self.name, self.alias)

class Extension:
	def __init__ (self, name, handles, enums, bitfields, compositeTypes, functions, definitions, additionalDefinitions, typedefs, versionInCore):
		self.name			= name
		self.definitions	= definitions
		self.additionalDefs = additionalDefinitions
		self.handles		= handles
		self.enums			= enums
		self.bitfields		= bitfields
		self.compositeTypes	= compositeTypes
		self.functions		= functions
		self.typedefs		= typedefs
		self.versionInCore	= versionInCore

	def __repr__ (self):
		return 'EXT:\n%s ->\nENUMS:\n%s\nCOMPOS:\n%s\nFUNCS:\n%s\nBITF:\n%s\nHAND:\n%s\nDEFS:\n%s\n' % (self.name, self.enums, self.compositeTypes, self.functions, self.bitfields, self.handles, self.definitions, self.versionInCore)

class API:
	def __init__ (self, versions, definitions, handles, enums, bitfields, compositeTypes, functions, extensions):
		self.versions		= versions
		self.definitions	= definitions
		self.handles		= handles
		self.enums			= enums
		self.bitfields		= bitfields
		self.compositeTypes	= compositeTypes
		self.functions		= functions # \note contains extension functions as well
		self.extensions		= extensions

def readFile (filename):
	with open(filename, 'rt') as f:
		return f.read()

IDENT_PTRN	= r'[a-zA-Z_][a-zA-Z0-9_]*'
WIDTH_PTRN	= r'[:0-9]*'
TYPE_PTRN	= r'[a-zA-Z_][a-zA-Z0-9_ \t*&]*'

def getInterfaceName (function):
	assert function.name[:2] == "vk"
	return function.name[2].lower() + function.name[3:]

def getFunctionTypeName (function):
	assert function.name[:2] == "vk"
	return function.name[2:] + "Func"

def endsWith (str, postfix):
	return str[-len(postfix):] == postfix

def splitNameExtPostfix (name):
	knownExtPostfixes = EXTENSION_POSTFIXES
	for postfix in knownExtPostfixes:
		if endsWith(name, postfix):
			return (name[:-len(postfix)], postfix)
	return (name, "")

def getBitEnumNameForBitfield (bitfieldName):
	bitfieldName, postfix = splitNameExtPostfix(bitfieldName)
	assert bitfieldName[-1] == "s"
	return bitfieldName[:-1] + "Bits" + postfix

def getBitfieldNameForBitEnum (bitEnumName):
	bitEnumName, postfix = splitNameExtPostfix(bitEnumName)
	assert bitEnumName[-4:] == "Bits"
	return bitEnumName[:-4] + "s" + postfix

def parsePreprocDefinedValue (src, name):
	value = parsePreprocDefinedValueOptional(src, name)
	if value is None:
		raise Exception("No such definition: %s" % name)
	return value

def parsePreprocDefinedValueOptional (src, name):
	definition = re.search(r'#\s*define\s+' + name + r'\s+([^\n]+)\n', src)
	if definition is None:
		return None
	value = definition.group(1).strip()
	if value == "UINT32_MAX":
		value = "(~0u)"
	return value

def parseEnum (name, src):
	keyValuePtrn = '(' + IDENT_PTRN + r')\s*=\s*([^\s,\n}]+)\s*[,\n}]'
	return Enum(name, re.findall(keyValuePtrn, src))

# \note Parses raw enums, some are mapped to bitfields later
def parseEnums (src):
	matches	= re.findall(r'typedef enum(\s*' + IDENT_PTRN + r')?\s*{([^}]*)}\s*(' + IDENT_PTRN + r')\s*;', src)
	enums	= []
	for enumname, contents, typename in matches:
		enums.append(parseEnum(typename, contents))
	return enums

def parseCompositeType (type, name, src):
	typeNamePtrn	= r'(' + TYPE_PTRN + r')(\s+' + IDENT_PTRN + r')((\[[^\]]+\]|:[0-9]+)*)\s*;'
	matches			= re.findall(typeNamePtrn, src)
	members			= [Variable(t.strip(), n.strip(), a.strip()) for t, n, a, _ in matches]
	return CompositeType(type, name, members)

def parseCompositeTypes (src):
	typeMap	= { 'struct': CompositeType.CLASS_STRUCT, 'union': CompositeType.CLASS_UNION }
	matches	= re.findall(r'typedef (struct|union)(\s*' + IDENT_PTRN + r')?\s*{([^}]*)}\s*(' + IDENT_PTRN + r')\s*;', src)
	types	= []
	for type, structname, contents, typename in matches:
		types.append(parseCompositeType(typeMap[type], typename, contents))
	return types

def parseCompositeTypesByVersion (src, versionsData):

	# find occurence of extension is a place where
	# we cant assign apiVersion to found structures
	extPtrn		= r'#define\s+[A-Z0-9_]+_EXTENSION_NAME\s+"([^"]+)"'
	versionEnd	= re.search(extPtrn, src)
	versions	= [Version((v[2], v[3], 0)) for v in versionsData]
	versions.append(None)

	# construct list of locations where version definitions start, and add the end of the file to it
	sectionLocations = [versionDef[1] for versionDef in versionsData]
	sectionLocations.append(versionEnd.start())
	sectionLocations.append(len(src))

	# construct function declaration pattern
	ptrn		= r'typedef (struct|union)(\s*' + IDENT_PTRN + r')?\s*{([^}]*)}\s*(' + IDENT_PTRN + r')\s*;'
	regPtrn		= re.compile(ptrn)
	types		= []
	typeMap		= { 'struct': CompositeType.CLASS_STRUCT, 'union': CompositeType.CLASS_UNION }

	# iterate over all versions and find all structure definitions
	for index, v in enumerate(versions):
		matches = regPtrn.findall(src, sectionLocations[index], sectionLocations[index+1])
		for type, structname, contents, typename in matches:
			compositeType = parseCompositeType(typeMap[type], typename, contents)
			compositeType.apiVersion = v
			types.append(compositeType)
	return types

def parseVersions (src):
	# returns list of tuples each with four items:
	# 1. string with version token (without ' 1' at the end)
	# 2. starting point off version specific definitions in vulkan.h.in
	# 3. major version number
	# 4. minor version number
	return [(m.group()[:-2], m.start(), int(m.group(1)), int(m.group(2))) for m in re.finditer('VK_VERSION_([1-9])_([0-9]) 1', src)]

def parseHandles (src):
	matches	= re.findall(r'VK_DEFINE(_NON_DISPATCHABLE|)_HANDLE\((' + IDENT_PTRN + r')\)[ \t]*[\n\r]', src)
	handles	= []
	typeMap	= {'': Handle.TYPE_DISP, '_NON_DISPATCHABLE': Handle.TYPE_NONDISP}
	for type, name in matches:
		handle = Handle(typeMap[type], name)
		handles.append(handle)
	return handles

def parseArgList (src):
	typeNamePtrn	= r'(' + TYPE_PTRN + r')(\s+' + IDENT_PTRN + r')((\[[^\]]+\])*)\s*'
	args			= []
	for rawArg in src.split(','):
		m = re.search(typeNamePtrn, rawArg)
		args.append(Variable(m.group(1).strip(), m.group(2).strip(), m.group(3)))
	return args

def removeTypeExtPostfix (name):
	for extPostfix in EXTENSION_POSTFIXES_STANDARD:
		if endsWith(name, extPostfix):
			return name[0:-len(extPostfix)]
	return None

def populateExtensionAliases(allObjects, extensionObjects):
	for object in extensionObjects:
		withoutPostfix = removeTypeExtPostfix(object.name)
		if withoutPostfix != None and withoutPostfix in allObjects:
			# max 1 alias is assumed by functions in this file
			assert allObjects[withoutPostfix].alias == None
			allObjects[withoutPostfix].alias = object
			object.isAlias = True
	for object in extensionObjects:
		object.checkAliasValidity()

def populateAliasesWithTypedefs (objects, src):
	objectsByName = {}
	for object in objects:
		objectsByName[object.name] = object
		ptrn	= r'\s*typedef\s+' + object.name + r'\s+([^;]+)'
		stash = re.findall(ptrn, src)
		if len(stash) == 1:
			objExt = copy.deepcopy(object)
			objExt.name = stash[0]
			object.alias = objExt
			objExt.isAlias = True
			objects.append(objExt)

def removeAliasedValues (enum):
	valueByName = {}
	for name, value in enum.values:
		valueByName[name] = value

	def removeDefExtPostfix (name):
		for extPostfix in EXTENSION_POSTFIXES:
			if endsWith(name, "_" + extPostfix):
				return name[0:-(len(extPostfix)+1)]
		return None

	newValues = []
	for name, value in enum.values:
		withoutPostfix = removeDefExtPostfix(name)
		if withoutPostfix != None and withoutPostfix in valueByName and valueByName[withoutPostfix] == value:
			continue
		newValues.append((name, value))
	enum.values = newValues

def parseFunctions (src):
	ptrn		= r'VKAPI_ATTR\s+(' + TYPE_PTRN + ')\s+VKAPI_CALL\s+(' + IDENT_PTRN + r')\s*\(([^)]*)\)\s*;'
	matches		= re.findall(ptrn, src)
	functions	= []
	for returnType, name, argList in matches:
		functions.append(Function(name.strip(), returnType.strip(), parseArgList(argList)))
	return functions

def parseFunctionsByVersion (src, versions):
	# construct list of locations where version definitions start, and add the end of the file to it
	sectionLocations = [versionDef[1] for versionDef in versions]
	sectionLocations.append(len(src))

	# construct function declaration pattern
	ptrn		= r'VKAPI_ATTR\s+(' + TYPE_PTRN + ')\s+VKAPI_CALL\s+(' + IDENT_PTRN + r')\s*\(([^)]*)\)\s*;'
	regPtrn		= re.compile(ptrn)
	functions	= []

	# iterate over all versions and find all function definitions
	for index, v in enumerate(versions):
		matches = regPtrn.findall(src, sectionLocations[index], sectionLocations[index+1])
		for returnType, name, argList in matches:
			functions.append(Function(name.strip(), returnType.strip(), parseArgList(argList), v[0]))
	return functions

def splitByExtension (src):
	ptrn		= r'#define\s+[A-Z0-9_]+_EXTENSION_NAME\s+"([^"]+)"'
	# Construct long pattern that will be used to split whole source by extensions
	match		= "#define\s+("
	for part in re.finditer(ptrn, src):
		 match += part.group(1)+"|"
	match = match[:-1] + ")\s+1"
	parts = re.split(match, src)

	# First part is core, following tuples contain extension name and all its definitions
	byExtension	= [(None, parts[0])]
	for ndx in range(1, len(parts), 2):
		byExtension.append((parts[ndx], parts[ndx+1]))
	return byExtension

def parseDefinitions (extensionName, src):

	def skipDefinition (extensionName, definition):
		if extensionName == None:
			return True
		extNameUpper = extensionName.upper()
		extNameUpper = extNameUpper.replace("VK_INTEL_SHADER_INTEGER_FUNCTIONS2", "VK_INTEL_SHADER_INTEGER_FUNCTIONS_2")
		extNameUpper = extNameUpper.replace("VK_EXT_ROBUSTNESS2", "VK_EXT_ROBUSTNESS_2")
		extNameUpper = extNameUpper.replace("VK_EXT_FRAGMENT_DENSITY_MAP2", "VK_EXT_FRAGMENT_DENSITY_MAP_2")
		extNameUpper = extNameUpper.replace("VK_AMD_SHADER_CORE_PROPERTIES2", "VK_AMD_SHADER_CORE_PROPERTIES_2")
		# SPEC_VERSION enums
		if definition[0].startswith(extNameUpper) and definition[1].isdigit():
			return False
		if definition[0].startswith(extNameUpper):
			return True
		if definition[1].isdigit():
			return True
		return False

	ptrn		= r'#define\s+([^\s]+)\s+([^\r\n]+)'
	matches		= re.findall(ptrn, src)

	return [Definition(None, match[0], match[1]) for match in matches if not skipDefinition(extensionName, match)]

def parseTypedefs (src):

	ptrn		= r'typedef\s+([^\s]+)\s+([^\r\n]+);'
	matches		= re.findall(ptrn, src)

	return [Definition(None, match[0], match[1]) for match in matches]

def parseExtensions (src, versions, allFunctions, allCompositeTypes, allEnums, allBitfields, allHandles, allDefinitions):

	def getCoreVersion (extensionName, extensionsData):
		# returns None when extension was not added to core for any Vulkan version
		# returns array containing DEVICE or INSTANCE string followed by the vulkan version in which this extension is core
		# note that this function is also called for vulkan 1.0 source for which extName is None
		if not extensionName:
			return None
		ptrn		= extensionName + r'\s+(DEVICE|INSTANCE)\s+([0-9_]+)'
		coreVersion = re.search(ptrn, extensionsData, re.I)
		if coreVersion != None:
			return [coreVersion.group(1)] + [int(number) for number in coreVersion.group(2).split('_')[:3]]
		return None

	extensionsData			= readFile(os.path.join(VULKAN_H_DIR, "extensions_data.txt"))
	splitSrc				= splitByExtension(src)
	extensions				= []
	functionsByName			= {function.name: function for function in allFunctions}
	compositeTypesByName	= {compType.name: compType for compType in allCompositeTypes}
	enumsByName				= {enum.name: enum for enum in allEnums}
	bitfieldsByName			= {bitfield.name: bitfield for bitfield in allBitfields}
	handlesByName			= {handle.name: handle for handle in allHandles}
	definitionsByName		= {definition.name: definition for definition in allDefinitions}

	for extensionName, extensionSrc in splitSrc:
		definitions			= [Definition("deUint32", v.getInHex(), parsePreprocDefinedValueOptional(extensionSrc, v.getInHex())) for v in versions]
		definitions.extend([Definition(type, name, parsePreprocDefinedValueOptional(extensionSrc, name)) for name, type in DEFINITIONS])
		definitions			= [definition for definition in definitions if definition.value != None]
		additionalDefinitions = parseDefinitions(extensionName, extensionSrc)
		handles				= parseHandles(extensionSrc)
		functions			= parseFunctions(extensionSrc)
		compositeTypes		= parseCompositeTypes(extensionSrc)
		rawEnums			= parseEnums(extensionSrc)
		bitfieldNames		= parseBitfieldNames(extensionSrc)
		typedefs			= parseTypedefs(extensionSrc)
		enumBitfieldNames	= [getBitEnumNameForBitfield(name) for name in bitfieldNames]
		enums				= [enum for enum in rawEnums if enum.name not in enumBitfieldNames]

		extCoreVersion		= getCoreVersion(extensionName, extensionsData)
		extFunctions		= [functionsByName[function.name] for function in functions]
		extCompositeTypes	= [compositeTypesByName[compositeType.name] for compositeType in compositeTypes]
		extEnums			= [enumsByName[enum.name] for enum in enums]
		extBitfields		= [bitfieldsByName[bitfieldName] for bitfieldName in bitfieldNames]
		extHandles			= [handlesByName[handle.name] for handle in handles]
		extDefinitions		= [definitionsByName[definition.name] for definition in definitions]

		if extCoreVersion != None:
			populateExtensionAliases(functionsByName, extFunctions)
			populateExtensionAliases(handlesByName, extHandles)
			populateExtensionAliases(enumsByName, extEnums)
			populateExtensionAliases(bitfieldsByName, extBitfields)
			populateExtensionAliases(compositeTypesByName, extCompositeTypes)

		extensions.append(Extension(extensionName, extHandles, extEnums, extBitfields, extCompositeTypes, extFunctions, extDefinitions, additionalDefinitions, typedefs, extCoreVersion))
	return extensions

def parseBitfieldNames (src):
	ptrn		= r'typedef\s+VkFlags\s(' + IDENT_PTRN + r')\s*;'
	matches		= re.findall(ptrn, src)

	return matches

def parseAPI (src):
	versionsData = parseVersions(src)
	versions     = [Version((v[2], v[3], 0)) for v in versionsData]
	definitions	 = [Definition("deUint32", v.getInHex(), parsePreprocDefinedValue(src, v.getInHex())) for v in versions]
	definitions.extend([Definition(type, name, parsePreprocDefinedValue(src, name)) for name, type in DEFINITIONS])

	handles			= parseHandles(src)
	rawEnums		= parseEnums(src)
	bitfieldNames	= parseBitfieldNames(src)
	enums			= []
	bitfields		= []
	bitfieldEnums	= set([getBitEnumNameForBitfield(n) for n in bitfieldNames if getBitEnumNameForBitfield(n) in [enum.name for enum in rawEnums]])
	compositeTypes	= parseCompositeTypesByVersion(src, versionsData)
	allFunctions	= parseFunctionsByVersion(src, versionsData)

	for enum in rawEnums:
		if enum.name in bitfieldEnums:
			bitfields.append(Bitfield(getBitfieldNameForBitEnum(enum.name), enum.values))
		else:
			enums.append(enum)

	for bitfieldName in bitfieldNames:
		if not bitfieldName in [bitfield.name for bitfield in bitfields]:
			# Add empty bitfield
			bitfields.append(Bitfield(bitfieldName, []))

	extensions = parseExtensions(src, versions, allFunctions, compositeTypes, enums, bitfields, handles, definitions)

	# Populate alias fields
	populateAliasesWithTypedefs(compositeTypes, src)
	populateAliasesWithTypedefs(enums, src)
	populateAliasesWithTypedefs(bitfields, src)
	populateAliasesWithTypedefs(handles, src)

	for enum in enums:
		removeAliasedValues(enum)


	# Make generator to create Deleter<VkAccelerationStructureNV>
	for f in allFunctions:
		if (f.name == 'vkDestroyAccelerationStructureNV'):
			f.arguments[1].type[0] = 'VkAccelerationStructureNV'

	# Dealias handles VkAccelerationStructureNV and VkAccelerationStructureKHR
	for handle in handles:
		if handle.name == 'VkAccelerationStructureKHR':
			handle.alias = None
		if handle.name == 'VkAccelerationStructureNV':
			handle.isAlias = False
	return API(
		versions		= versions,
		definitions		= definitions,
		handles			= handles,
		enums			= enums,
		bitfields		= bitfields,
		compositeTypes	= compositeTypes,
		functions		= allFunctions,
		extensions		= extensions)

def splitUniqueAndDuplicatedEntries (handles):
	listOfUniqueHandles = []
	duplicates			= OrderedDict()
	for handle in handles:
		if handle.alias != None:
			duplicates[handle.alias] = handle
		if not handle.isAlias:
			listOfUniqueHandles.append(handle)
	return listOfUniqueHandles, duplicates

def writeHandleType (api, filename):
	uniqeHandles, duplicatedHandles = splitUniqueAndDuplicatedEntries(api.handles)

	def genHandles ():
		yield "\t%s\t= 0," % uniqeHandles[0].getHandleType()
		for handle in uniqeHandles[1:]:
			yield "\t%s," % handle.getHandleType()
		for duplicate in duplicatedHandles:
			yield "\t%s\t= %s," % (duplicate.getHandleType(), duplicatedHandles[duplicate].getHandleType())
		yield "\tHANDLE_TYPE_LAST\t= %s + 1" % (uniqeHandles[-1].getHandleType())

	def genHandlesBlock ():
		yield "enum HandleType"
		yield "{"

		for line in indentLines(genHandles()):
			yield line

		yield "};"
		yield ""

	writeInlFile(filename, INL_HEADER, genHandlesBlock())

def getEnumValuePrefix (enum):
	prefix = enum.name[0]
	for i in range(1, len(enum.name)):
		if enum.name[i].isupper() and not enum.name[i-1].isupper():
			prefix += "_"
		prefix += enum.name[i].upper()
	return prefix

def parseInt (value):
	if value[:2] == "0x":
		return int(value, 16)
	else:
		return int(value, 10)

def areEnumValuesLinear (enum):
	curIndex = 0
	for name, value in enum.values:
		if value[:2] != "VK":
			intValue = parseInt(value)
			if intValue != curIndex:
				# consider enums containing *_MAX_ENUM = 0x7FFFFFFF as linear
				if intValue == 0x7FFFFFFF:
					return True
				return False
			curIndex += 1
	return True

def genEnumSrc (enum):
	yield "enum %s" % enum.name
	yield "{"

	lines = ["\t%s\t= %s," % v for v in enum.values]
	if areEnumValuesLinear(enum):
		lastItem = "\t%s_LAST," % getEnumValuePrefix(enum)
		if parseInt(enum.values[-1][1]) == 0x7FFFFFFF:
			# if last enum item is *_MAX_ENUM then we need to make sure
			# it stays last entry also if we append *_LAST to generated
			# source (without this value of *_LAST won't be correct)
			lines.insert(-1, lastItem)
		else:
			lines.append(lastItem)

	for line in indentLines(lines):
		yield line

	yield "};"

def genBitfieldSrc (bitfield):
	if len(bitfield.values) > 0:
		yield "enum %s" % getBitEnumNameForBitfield(bitfield.name)
		yield "{"
		for line in indentLines(["\t%s\t= %s," % v for v in bitfield.values]):
			yield line
		yield "};"
	yield "typedef deUint32 %s;" % bitfield.name

def genCompositeTypeSrc (type):
	yield "%s %s" % (type.getClassName(), type.name)
	yield "{"
	for line in indentLines(['\t'+m.getAsString('\t')+';' for m in type.members]):
		yield line
	yield "};"

def genHandlesSrc (handles):
	uniqeHandles, duplicatedHandles = splitUniqueAndDuplicatedEntries(handles)

	def genLines (handles):
		for handle in uniqeHandles:
			if handle.type == Handle.TYPE_DISP:
				yield "VK_DEFINE_HANDLE\t(%s,\t%s);" % (handle.name, handle.getHandleType())
			elif handle.type == Handle.TYPE_NONDISP:
				yield "VK_DEFINE_NON_DISPATCHABLE_HANDLE\t(%s,\t%s);" % (handle.name, handle.getHandleType())

		for duplicate in duplicatedHandles:
			if duplicate.type == Handle.TYPE_DISP:
				yield "VK_DEFINE_HANDLE\t(%s,\t%s);" % (duplicate.name, duplicatedHandles[duplicate].getHandleType())
			elif duplicate.type == Handle.TYPE_NONDISP:
				yield "VK_DEFINE_NON_DISPATCHABLE_HANDLE\t(%s,\t%s);" % (duplicate.name, duplicatedHandles[duplicate].getHandleType())

	for line in indentLines(genLines(handles)):
		yield line

def stripTrailingComment(str):
	index = str.find("//")
	if index == -1:
		return str
	return str[:index]

def genDefinitionsSrc (definitions):
	for line in ["#define %s\t(static_cast<%s>\t(%s))" % (definition.name, definition.type, stripTrailingComment(definition.value)) for definition in definitions]:
		yield line

def genDefinitionsAliasSrc (definitions):
	for line in ["#define %s\t%s" % (definition.name, definitions[definition].name) for definition in definitions]:
		if definition.value != definitions[definition].value and definition.value != definitions[definition].name:
			raise Exception("Value of %s (%s) is different than core definition value %s (%s)." % (definition.name, definition.value, definitions[definition].name, definitions[definition].value))
		yield line

def genMaxFrameworkVersion (definitions):
	maxApiVersionMajor = 1
	maxApiVersionMinor = 0
	for definition in definitions:
		match = re.match("VK_API_VERSION_(\d+)_(\d+)", definition.name)
		if match:
			apiVersionMajor = int(match.group(1))
			apiVersionMinor = int(match.group(2))
			if apiVersionMajor > maxApiVersionMajor:
				maxApiVersionMajor = apiVersionMajor
				maxApiVersionMinor = apiVersionMinor
			elif apiVersionMajor == maxApiVersionMajor and apiVersionMinor > maxApiVersionMinor:
				maxApiVersionMinor = apiVersionMinor
	yield "#define VK_API_MAX_FRAMEWORK_VERSION\tVK_API_VERSION_%d_%d" % (maxApiVersionMajor, maxApiVersionMinor)

def writeBasicTypes (api, filename):

	def gen ():
		definitionsCore, definitionDuplicates = splitUniqueAndDuplicatedEntries(api.definitions)

		for line in indentLines(chain(genDefinitionsSrc(definitionsCore), genMaxFrameworkVersion(definitionsCore), genDefinitionsAliasSrc(definitionDuplicates))):
			yield line
		yield ""

		for line in genHandlesSrc(api.handles):
			yield line
		yield ""

		for enum in api.enums:
			if not enum.isAlias:
				for line in genEnumSrc(enum):
					yield line
			else:
				for enum2 in api.enums:
					if enum2.alias == enum:
						yield "typedef %s %s;" % (enum2.name, enum.name)
			yield ""

		for bitfield in api.bitfields:
			if not bitfield.isAlias:
				for line in genBitfieldSrc(bitfield):
					yield line
			else:
				for bitfield2 in api.bitfields:
					if bitfield2.alias == bitfield:
						yield "typedef %s %s;" % (bitfield2.name, bitfield.name)
			yield ""
		for line in indentLines(["VK_DEFINE_PLATFORM_TYPE(%s,\t%s);" % (s[0], c) for n, s, c in PLATFORM_TYPES]):
			yield line

		for ext in api.extensions:
			if ext.additionalDefs != None:
				for definition in ext.additionalDefs:
					yield "#define " + definition.name + " " + definition.value
	writeInlFile(filename, INL_HEADER, gen())

def writeCompositeTypes (api, filename):
	def gen ():
		for type in api.compositeTypes:
			type.checkAliasValidity()

			if not type.isAlias:
				for line in genCompositeTypeSrc(type):
					yield line
			else:
				for type2 in api.compositeTypes:
					if type2.alias == type:
						yield "typedef %s %s;" % (type2.name, type.name)
			yield ""

	writeInlFile(filename, INL_HEADER, gen())

def argListToStr (args):
	return ", ".join(v.getAsString(' ') for v in args)

def writeInterfaceDecl (api, filename, functionTypes, concrete):
	def genProtos ():
		postfix = "" if concrete else " = 0"
		for function in api.functions:
			if not function.getType() in functionTypes:
				continue
			if not function.isAlias:
				yield "virtual %s\t%s\t(%s) const%s;" % (function.returnType, getInterfaceName(function), argListToStr(function.arguments), postfix)

	writeInlFile(filename, INL_HEADER, indentLines(genProtos()))

def writeFunctionPtrTypes (api, filename):
	def genTypes ():
		for function in api.functions:
			yield "typedef VKAPI_ATTR %s\t(VKAPI_CALL* %s)\t(%s);" % (function.returnType, getFunctionTypeName(function), argListToStr(function.arguments))

	writeInlFile(filename, INL_HEADER, indentLines(genTypes()))

def writeFunctionPointers (api, filename, functionTypes):
	def FunctionsYielder ():
		for function in api.functions:
			if function.getType() in functionTypes:
				if function.isAlias:
					if function.getType() == Function.TYPE_INSTANCE and function.arguments[0].getType() == "VkPhysicalDevice":
						yield "%s\t%s;" % (getFunctionTypeName(function), getInterfaceName(function))
				else:
					yield "%s\t%s;" % (getFunctionTypeName(function), getInterfaceName(function))

	writeInlFile(filename, INL_HEADER, indentLines(FunctionsYielder()))

def writeInitFunctionPointers (api, filename, functionTypes, cond = None):
	def makeInitFunctionPointers ():
		for function in api.functions:
			if function.getType() in functionTypes and (cond == None or cond(function)):
				interfaceName = getInterfaceName(function)
				if function.isAlias:
					if function.getType() == Function.TYPE_INSTANCE and function.arguments[0].getType() == "VkPhysicalDevice":
						yield "m_vk.%s\t= (%s)\tGET_PROC_ADDR(\"%s\");" % (getInterfaceName(function), getFunctionTypeName(function), function.name)
				else:
					yield "m_vk.%s\t= (%s)\tGET_PROC_ADDR(\"%s\");" % (getInterfaceName(function), getFunctionTypeName(function), function.name)
					if function.alias != None:
						yield "if (!m_vk.%s)" % (getInterfaceName(function))
						yield "    m_vk.%s\t= (%s)\tGET_PROC_ADDR(\"%s\");" % (getInterfaceName(function), getFunctionTypeName(function), function.alias.name)
	lines = [line.replace('    ', '\t') for line in indentLines(makeInitFunctionPointers())]
	writeInlFile(filename, INL_HEADER, lines)

def writeFuncPtrInterfaceImpl (api, filename, functionTypes, className):
	def makeFuncPtrInterfaceImpl ():
		for function in api.functions:
			if function.getType() in functionTypes and not function.isAlias:
				yield ""
				yield "%s %s::%s (%s) const" % (function.returnType, className, getInterfaceName(function), argListToStr(function.arguments))
				yield "{"
				if function.name == "vkEnumerateInstanceVersion":
					yield "	if (m_vk.enumerateInstanceVersion)"
					yield "		return m_vk.enumerateInstanceVersion(pApiVersion);"
					yield ""
					yield "	*pApiVersion = VK_API_VERSION_1_0;"
					yield "	return VK_SUCCESS;"
				elif function.getType() == Function.TYPE_INSTANCE and function.arguments[0].getType() == "VkPhysicalDevice" and function.alias != None:
					yield "	vk::VkPhysicalDeviceProperties props;"
					yield "	m_vk.getPhysicalDeviceProperties(physicalDevice, &props);"
					yield "	if (props.apiVersion >= VK_API_VERSION_1_1)"
					yield "		%sm_vk.%s(%s);" % ("return " if function.returnType != "void" else "", getInterfaceName(function), ", ".join(a.name for a in function.arguments))
					yield "	else"
					yield "		%sm_vk.%s(%s);" % ("return " if function.returnType != "void" else "", getInterfaceName(function.alias), ", ".join(a.name for a in function.arguments))
				else:
					yield "	%sm_vk.%s(%s);" % ("return " if function.returnType != "void" else "", getInterfaceName(function), ", ".join(a.name for a in function.arguments))
				yield "}"

	writeInlFile(filename, INL_HEADER, makeFuncPtrInterfaceImpl())

def writeStrUtilProto (api, filename):
	def makeStrUtilProto ():
		for line in indentLines(["const char*\tget%sName\t(%s value);" % (enum.name[2:], enum.name) for enum in api.enums if not enum.isAlias]):
			yield line
		yield ""
		for line in indentLines(["inline tcu::Format::Enum<%s>\tget%sStr\t(%s value)\t{ return tcu::Format::Enum<%s>(get%sName, value);\t}" % (e.name, e.name[2:], e.name, e.name, e.name[2:]) for e in api.enums if not e.isAlias]):
			yield line
		yield ""
		for line in indentLines(["inline std::ostream&\toperator<<\t(std::ostream& s, %s value)\t{ return s << get%sStr(value);\t}" % (e.name, e.name[2:]) for e in api.enums if not e.isAlias]):
			yield line
		yield ""
		for line in indentLines(["tcu::Format::Bitfield<32>\tget%sStr\t(%s value);" % (bitfield.name[2:], bitfield.name) for bitfield in api.bitfields if not bitfield.isAlias or bitfield.name=='VkBuildAccelerationStructureFlagsNV']):
			yield line
		yield ""
		for line in indentLines(["std::ostream&\toperator<<\t(std::ostream& s, const %s& value);" % (s.name) for s in api.compositeTypes if not s.isAlias]):
			yield line

	writeInlFile(filename, INL_HEADER, makeStrUtilProto())

def writeStrUtilImpl (api, filename):
	def makeStrUtilImpl ():
		for line in indentLines(["template<> const char*\tgetTypeName<%s>\t(void) { return \"%s\";\t}" % (handle.name, handle.name) for handle in api.handles if not handle.isAlias]):
			yield line

		yield ""
		yield "namespace %s" % PLATFORM_TYPE_NAMESPACE
		yield "{"

		for line in indentLines("std::ostream& operator<< (std::ostream& s, %s\tv) { return s << tcu::toHex(v.internal); }" % ''.join(s) for n, s, c in PLATFORM_TYPES):
			yield line

		yield "}"

		for enum in api.enums:
			if enum.isAlias:
				continue
			yield ""
			yield "const char* get%sName (%s value)" % (enum.name[2:], enum.name)
			yield "{"
			yield "\tswitch (value)"
			yield "\t{"
			for line in indentLines(["\t\tcase %s:\treturn \"%s\";" % (n, n) for n, v in enum.values if v[:2] != "VK"] + ["\t\tdefault:\treturn DE_NULL;"]):
				yield line
			yield "\t}"
			yield "}"

		for bitfield in api.bitfields:
			if bitfield.isAlias:
				if bitfield.name != 'VkBuildAccelerationStructureFlagsNV':
					continue
			yield ""
			yield "tcu::Format::Bitfield<32> get%sStr (%s value)" % (bitfield.name[2:], bitfield.name)
			yield "{"

			if len(bitfield.values) > 0:
				yield "\tstatic const tcu::Format::BitDesc s_desc[] ="
				yield "\t{"
				for line in indentLines(["\t\ttcu::Format::BitDesc(%s,\t\"%s\")," % (n, n) for n, v in bitfield.values]):
					yield line
				yield "\t};"
				yield "\treturn tcu::Format::Bitfield<32>(value, DE_ARRAY_BEGIN(s_desc), DE_ARRAY_END(s_desc));"
			else:
				yield "\treturn tcu::Format::Bitfield<32>(value, DE_NULL, DE_NULL);"
			yield "}"

		bitfieldTypeNames = set([bitfield.name for bitfield in api.bitfields])

		for type in api.compositeTypes:
			if not type.isAlias:
				yield ""
				yield "std::ostream& operator<< (std::ostream& s, const %s& value)" % type.name
				yield "{"
				yield "\ts << \"%s = {\\n\";" % type.name
				for member in type.members:
					memberName	= member.name
					valFmt		= None
					newLine		= ""
					if member.getType() in bitfieldTypeNames:
						valFmt = "get%sStr(value.%s)" % (member.getType()[2:], member.name)
					elif member.getType() == "const char*" or member.getType() == "char*":
						valFmt = "getCharPtrStr(value.%s)" % member.name
					elif member.getType() == PLATFORM_TYPE_NAMESPACE + "::Win32LPCWSTR":
						valFmt = "getWStr(value.%s)" % member.name
					elif member.arraySize != '':
						if member.name in ["extensionName", "deviceName", "layerName", "description"]:
							valFmt = "(const char*)value.%s" % member.name
						elif member.getType() == 'char' or member.getType() == 'deUint8':
							newLine = "'\\n' << "
							valFmt	= "tcu::formatArray(tcu::Format::HexIterator<%s>(DE_ARRAY_BEGIN(value.%s)), tcu::Format::HexIterator<%s>(DE_ARRAY_END(value.%s)))" % (member.getType(), member.name, member.getType(), member.name)
						else:
							if member.name == "memoryTypes" or member.name == "memoryHeaps":
								endIter = "DE_ARRAY_BEGIN(value.%s) + value.%sCount" % (member.name, member.name[:-1])
							else:
								endIter = "DE_ARRAY_END(value.%s)" % member.name
							newLine = "'\\n' << "
							valFmt	= "tcu::formatArray(DE_ARRAY_BEGIN(value.%s), %s)" % (member.name, endIter)
						memberName = member.name
					else:
						valFmt = "value.%s" % member.name
					yield ("\ts << \"\\t%s = \" << " % memberName) + newLine + valFmt + " << '\\n';"
				yield "\ts << '}';"
				yield "\treturn s;"
				yield "}"
	writeInlFile(filename, INL_HEADER, makeStrUtilImpl())


def writeObjTypeImpl (api, filename):
	def makeObjTypeImpl ():

		yield "namespace vk"
		yield "{"

		yield "template<typename T> VkObjectType getObjectType	(void);"

		for line in indentLines(["template<> inline VkObjectType\tgetObjectType<%s>\t(void) { return %s;\t}" % (handle.name, prefixName("VK_OBJECT_TYPE_", handle.name)) for handle in api.handles if not handle.isAlias]):
			yield line

		yield "}"

	writeInlFile(filename, INL_HEADER, makeObjTypeImpl())

class ConstructorFunction:
	def __init__ (self, type, name, objectType, ifaceArgs, arguments):
		self.type		= type
		self.name		= name
		self.objectType	= objectType
		self.ifaceArgs	= ifaceArgs
		self.arguments	= arguments

def getConstructorFunctions (api):
	funcs = []
	ifacesDict = {
		Function.TYPE_PLATFORM: [Variable("const PlatformInterface&", "vk", "")],
		Function.TYPE_INSTANCE: [Variable("const InstanceInterface&", "vk", "")],
		Function.TYPE_DEVICE: [Variable("const DeviceInterface&", "vk", "")]
	}
	for function in api.functions:
		if function.isAlias:
			continue
		if (function.name[:8] == "vkCreate" or function.name == "vkAllocateMemory") and not "createInfoCount" in [a.name for a in function.arguments]:
			if function.name == "vkCreateDisplayModeKHR":
				continue # No way to delete display modes (bug?)

			# \todo [pyry] Rather hacky
			ifaceArgs = ifacesDict[function.getType()]
			if function.name == "vkCreateDevice":
				ifaceArgs = [Variable("const PlatformInterface&", "vkp", ""), Variable("VkInstance", "instance", "")] + ifaceArgs

			assert (function.arguments[-2].type == ["const", "VkAllocationCallbacks", "*"])

			objectType	= function.arguments[-1].type[0] #not getType() but type[0] on purpose
			arguments	= function.arguments[:-1]
			funcs.append(ConstructorFunction(function.getType(), getInterfaceName(function), objectType, ifaceArgs, arguments))
	return funcs

def addVersionDefines(versionSpectrum):
	output = ["#define " + ver.getDefineName() + " " + ver.getInHex() for ver in versionSpectrum if not ver.isStandardVersion()]
	return output

def removeVersionDefines(versionSpectrum):
	output = ["#undef " + ver.getDefineName() for ver in versionSpectrum if not ver.isStandardVersion()]
	return output

def writeRefUtilProto (api, filename):
	functions = getConstructorFunctions(api)

	def makeRefUtilProto ():
		unindented = []
		for line in indentLines(["Move<%s>\t%s\t(%s = DE_NULL);" % (function.objectType, function.name, argListToStr(function.ifaceArgs + function.arguments)) for function in functions]):
			yield line

	writeInlFile(filename, INL_HEADER, makeRefUtilProto())

def writeRefUtilImpl (api, filename):
	functions = getConstructorFunctions(api)

	def makeRefUtilImpl ():
		yield "namespace refdetails"
		yield "{"
		yield ""

		for function in api.functions:
			if function.getType() == Function.TYPE_DEVICE \
			and (function.name[:9] == "vkDestroy" or function.name == "vkFreeMemory") \
			and not function.name == "vkDestroyDevice" \
			and not function.isAlias:
				objectType = function.arguments[-2].getType()
				yield "template<>"
				yield "void Deleter<%s>::operator() (%s obj) const" % (objectType, objectType)
				yield "{"
				yield "\tm_deviceIface->%s(m_device, obj, m_allocator);" % (getInterfaceName(function))
				yield "}"
				yield ""

		yield "} // refdetails"
		yield ""

		dtorDict = {
			Function.TYPE_PLATFORM: "object",
			Function.TYPE_INSTANCE: "instance",
			Function.TYPE_DEVICE: "device"
		}

		for function in functions:
			deleterArgsString = ''
			if function.name == "createDevice":
				# createDevice requires two additional parameters to setup VkDevice deleter
				deleterArgsString = "vkp, instance, object, " +  function.arguments[-1].name
			else:
				deleterArgsString = "vk, %s, %s" % (dtorDict[function.type], function.arguments[-1].name)

			yield "Move<%s> %s (%s)" % (function.objectType, function.name, argListToStr(function.ifaceArgs + function.arguments))
			yield "{"
			yield "\t%s object = 0;" % function.objectType
			yield "\tVK_CHECK(vk.%s(%s));" % (function.name, ", ".join([a.name for a in function.arguments] + ["&object"]))
			yield "\treturn Move<%s>(check<%s>(object), Deleter<%s>(%s));" % (function.objectType, function.objectType, function.objectType, deleterArgsString)
			yield "}"
			yield ""

	writeInlFile(filename, INL_HEADER, makeRefUtilImpl())

def writeStructTraitsImpl (api, filename):
	def gen ():
		for type in api.compositeTypes:
			if type.getClassName() == "struct" and type.members[0].name == "sType" and not type.isAlias and type.name != "VkBaseOutStructure" and type.name != "VkBaseInStructure":
				yield "template<> VkStructureType getStructureType<%s> (void)" % type.name
				yield "{"
				yield "\treturn %s;" % prefixName("VK_STRUCTURE_TYPE_", type.name)
				yield "}"
				yield ""

	writeInlFile(filename, INL_HEADER, gen())

def writeNullDriverImpl (api, filename):
	def genNullDriverImpl ():
		specialFuncNames	= [
				"vkCreateGraphicsPipelines",
				"vkCreateComputePipelines",
				"vkCreateRayTracingPipelinesNV",
				"vkCreateRayTracingPipelinesKHR",
				"vkGetInstanceProcAddr",
				"vkGetDeviceProcAddr",
				"vkEnumeratePhysicalDevices",
				"vkEnumerateInstanceExtensionProperties",
				"vkEnumerateDeviceExtensionProperties",
				"vkGetPhysicalDeviceFeatures",
				"vkGetPhysicalDeviceFeatures2KHR",
				"vkGetPhysicalDeviceProperties",
				"vkGetPhysicalDeviceProperties2KHR",
				"vkGetPhysicalDeviceQueueFamilyProperties",
				"vkGetPhysicalDeviceMemoryProperties",
				"vkGetPhysicalDeviceFormatProperties",
				"vkGetPhysicalDeviceImageFormatProperties",
				"vkGetDeviceQueue",
				"vkGetBufferMemoryRequirements",
				"vkGetBufferMemoryRequirements2KHR",
				"vkGetImageMemoryRequirements",
				"vkGetImageMemoryRequirements2KHR",
				"vkAllocateMemory",
				"vkMapMemory",
				"vkUnmapMemory",
				"vkAllocateDescriptorSets",
				"vkFreeDescriptorSets",
				"vkResetDescriptorPool",
				"vkAllocateCommandBuffers",
				"vkFreeCommandBuffers",
				"vkCreateDisplayModeKHR",
				"vkCreateSharedSwapchainsKHR",
				"vkGetPhysicalDeviceExternalBufferPropertiesKHR",
				"vkGetPhysicalDeviceImageFormatProperties2KHR",
				"vkGetMemoryAndroidHardwareBufferANDROID",
			]

		coreFunctions		= [f for f in api.functions if not f.isAlias]
		specialFuncs		= [f for f in coreFunctions if f.name in specialFuncNames]
		createFuncs			= [f for f in coreFunctions if (f.name[:8] == "vkCreate" or f.name == "vkAllocateMemory") and not f in specialFuncs]
		destroyFuncs		= [f for f in coreFunctions if (f.name[:9] == "vkDestroy" or f.name == "vkFreeMemory") and not f in specialFuncs]
		dummyFuncs			= [f for f in coreFunctions if f not in specialFuncs + createFuncs + destroyFuncs]

		def getHandle (name):
			for handle in api.handles:
				if handle.name == name[0]:
					return handle
			raise Exception("No such handle: %s" % name)

		for function in createFuncs:
			objectType	= function.arguments[-1].type[:-1]
			argsStr		= ", ".join([a.name for a in function.arguments[:-1]])

			yield "VKAPI_ATTR %s VKAPI_CALL %s (%s)" % (function.returnType, getInterfaceName(function), argListToStr(function.arguments))
			yield "{"
			yield "\tDE_UNREF(%s);" % function.arguments[-2].name

			if getHandle(objectType).type == Handle.TYPE_NONDISP:
				yield "\tVK_NULL_RETURN((*%s = allocateNonDispHandle<%s, %s>(%s)));" % (function.arguments[-1].name, objectType[0][2:], objectType[0], argsStr)
			else:
				yield "\tVK_NULL_RETURN((*%s = allocateHandle<%s, %s>(%s)));" % (function.arguments[-1].name, objectType[0][2:], objectType[0], argsStr)
			yield "}"
			yield ""

		for function in destroyFuncs:
			objectArg	= function.arguments[-2]

			yield "VKAPI_ATTR %s VKAPI_CALL %s (%s)" % (function.returnType, getInterfaceName(function), argListToStr(function.arguments))
			yield "{"
			for arg in function.arguments[:-2]:
				yield "\tDE_UNREF(%s);" % arg.name

			if getHandle(objectArg.type).type == Handle.TYPE_NONDISP:
				yield "\tfreeNonDispHandle<%s, %s>(%s, %s);" % (objectArg.getType()[2:], objectArg.getType(), objectArg.name, function.arguments[-1].name)
			else:
				yield "\tfreeHandle<%s, %s>(%s, %s);" % (objectArg.getType()[2:], objectArg.getType(), objectArg.name, function.arguments[-1].name)

			yield "}"
			yield ""

		for function in dummyFuncs:
			yield "VKAPI_ATTR %s VKAPI_CALL %s (%s)" % (function.returnType, getInterfaceName(function), argListToStr(function.arguments))
			yield "{"
			for arg in function.arguments:
				yield "\tDE_UNREF(%s);" % arg.name
			if function.returnType != "void":
				yield "\treturn VK_SUCCESS;"
			yield "}"
			yield ""

		def genFuncEntryTable (type, name):
			funcs = [f for f in api.functions if f.getType() == type]
			refFuncs = {}
			for f in api.functions:
				if f.alias != None:
					refFuncs[f.alias] = f

			yield "static const tcu::StaticFunctionLibrary::Entry %s[] =" % name
			yield "{"
			for line in indentLines(["\tVK_NULL_FUNC_ENTRY(%s,\t%s)," % (function.name, getInterfaceName(function if not function.isAlias else refFuncs[function])) for function in funcs]):
				yield line
			yield "};"
			yield ""

		# Func tables
		for line in genFuncEntryTable(Function.TYPE_PLATFORM, "s_platformFunctions"):
			yield line

		for line in genFuncEntryTable(Function.TYPE_INSTANCE, "s_instanceFunctions"):
			yield line

		for line in genFuncEntryTable(Function.TYPE_DEVICE, "s_deviceFunctions"):
			yield line

	writeInlFile(filename, INL_HEADER, genNullDriverImpl())

def writeTypeUtil (api, filename):
	# Structs filled by API queries are not often used in test code
	QUERY_RESULT_TYPES = set([
			"VkPhysicalDeviceFeatures",
			"VkPhysicalDeviceLimits",
			"VkFormatProperties",
			"VkImageFormatProperties",
			"VkPhysicalDeviceSparseProperties",
			"VkQueueFamilyProperties",
			"VkMemoryType",
			"VkMemoryHeap",
		])
	COMPOSITE_TYPES = set([t.name for t in api.compositeTypes if not t.isAlias])

	def isSimpleStruct (type):
		def hasArrayMember (type):
			for member in type.members:
				if member.arraySize != '':
					return True
			return False

		def hasCompositeMember (type):
			for member in type.members:
				if member.getType() in COMPOSITE_TYPES:
					return True
			return False

		return type.typeClass == CompositeType.CLASS_STRUCT and \
		type.members[0].getType() != "VkStructureType" and \
		not type.name in QUERY_RESULT_TYPES and \
		not hasArrayMember(type) and \
		not hasCompositeMember(type)

	def gen ():
		for type in api.compositeTypes:
			if not isSimpleStruct(type) or type.isAlias:
				continue

			yield ""
			yield "inline %s make%s (%s)" % (type.name, type.name[2:], argListToStr(type.members))
			yield "{"
			yield "\t%s res;" % type.name
			for line in indentLines(["\tres.%s\t= %s;" % (m.name, m.name) for m in type.members]):
				yield line
			yield "\treturn res;"
			yield "}"

	writeInlFile(filename, INL_HEADER, gen())

def writeDriverIds(filename):

	driverIdsString = []
	driverIdsString.append("static const struct\n"
					 "{\n"
					 "\tstd::string driver;\n"
					 "\tdeUint32 id;\n"
					 "} driverIds [] =\n"
					 "{")

	vulkanCore = readFile(os.path.join(VULKAN_H_DIR, "vulkan_core.h"))

	items = re.search(r'(?:typedef\s+enum\s+VkDriverId\s*{)((.*\n)*)(?:}\s*VkDriverId\s*;)', vulkanCore).group(1).split(',')
	driverItems = dict()
	for item in items:
		item.strip()
		splitted = item.split('=')
		key = splitted[0].strip()
		value_str = splitted[1].strip()
		try: # is this previously defined value?
			value = driverItems[value_str]
		except:
			value = value_str
			value_str = ""
		if value_str:
			value_str = "\t// " + value_str
		driverItems[key] = value
		if not item == items[-1]:
			driverIdsString.append("\t{\"" + key + "\"" + ", " + value + "}," + value_str)
		else:
			driverIdsString.append("\t{\"" + key + "\"" + ", " + value + "}" + value_str)
		driverItems[key] = value

	driverIdsString.append("};")

	writeInlFile(filename, INL_HEADER, driverIdsString)


def writeSupportedExtenions(api, filename):

	def writeExtensionsForVersions(map):
		result = []
		for version in map:
			result.append("	if (coreVersion >= " + str(version) + ")")
			result.append("	{")
			for extension in map[version]:
				result.append('		dst.push_back("' + extension.name + '");')
			result.append("	}")

		return result

	instanceMap		= {}
	deviceMap		= {}
	versionSet		= set()

	for ext in api.extensions:
		if ext.versionInCore != None:
			if ext.versionInCore[0] == 'INSTANCE':
				list = instanceMap.get(Version(ext.versionInCore[1:]))
				instanceMap[Version(ext.versionInCore[1:])] = list + [ext] if list else [ext]
			else:
				list = deviceMap.get(Version(ext.versionInCore[1:]))
				deviceMap[Version(ext.versionInCore[1:])] = list + [ext] if list else [ext]
			versionSet.add(Version(ext.versionInCore[1:]))

	lines = addVersionDefines(versionSet) + [
	"",
	"void getCoreDeviceExtensionsImpl (deUint32 coreVersion, ::std::vector<const char*>&%s)" % (" dst" if len(deviceMap) != 0 else ""),
	"{"] + writeExtensionsForVersions(deviceMap) + [
	"}",
	"",
	"void getCoreInstanceExtensionsImpl (deUint32 coreVersion, ::std::vector<const char*>&%s)" % (" dst" if len(instanceMap) != 0 else ""),
	"{"] + writeExtensionsForVersions(instanceMap) + [
	"}",
	""] + removeVersionDefines(versionSet)
	writeInlFile(filename, INL_HEADER, lines)

def writeExtensionFunctions (api, filename):

	def isInstanceExtension (ext):
		if ext.name and ext.functions:
			if ext.functions[0].getType() == Function.TYPE_INSTANCE:
				return True
			else:
				return False

	def isDeviceExtension (ext):
		if ext.name and ext.functions:
			if ext.functions[0].getType() == Function.TYPE_DEVICE:
				return True
			else:
				return False

	def writeExtensionNameArrays ():
		instanceExtensionNames = []
		deviceExtensionNames = []
		for ext in api.extensions:
			if ext.name and isInstanceExtension(ext):
				instanceExtensionNames += [ext.name]
			elif ext.name and isDeviceExtension(ext):
				deviceExtensionNames += [ext.name]
		yield '::std::string instanceExtensionNames[] =\n{'
		for instanceExtName in instanceExtensionNames:
			if (instanceExtName == instanceExtensionNames[len(instanceExtensionNames) - 1]):
				yield '\t"%s"' % instanceExtName
			else:
				yield '\t"%s",' % instanceExtName
		yield '};\n'
		yield '::std::string deviceExtensionNames[] =\n{'
		for deviceExtName in deviceExtensionNames:
			if (deviceExtName == deviceExtensionNames[len(deviceExtensionNames) - 1]):
				yield '\t"%s"' % deviceExtName
			else:
				yield '\t"%s",' % deviceExtName
		yield '};'

	def writeExtensionFunctions (functionType):
		isFirstWrite = True
		dg_list = []	# Device groups functions need special casing, as Vulkan 1.0 keeps them in VK_KHR_device_groups whereas 1.1 moved them into VK_KHR_swapchain
		if functionType == Function.TYPE_INSTANCE:
			yield 'void getInstanceExtensionFunctions (deUint32 apiVersion, ::std::string extName, ::std::vector<const char*>& functions)\n{'
			dg_list = ["vkGetPhysicalDevicePresentRectanglesKHR"]
		elif functionType == Function.TYPE_DEVICE:
			yield 'void getDeviceExtensionFunctions (deUint32 apiVersion, ::std::string extName, ::std::vector<const char*>& functions)\n{'
			dg_list = ["vkGetDeviceGroupPresentCapabilitiesKHR", "vkGetDeviceGroupSurfacePresentModesKHR", "vkAcquireNextImage2KHR"]
		for ext in api.extensions:
			funcNames = []
			if ext.name:
				for func in ext.functions:
					if func.getType() == functionType:
						funcNames.append(func.name)
			if ext.name:
				yield '\tif (extName == "%s")' % ext.name
				yield '\t{'
				for funcName in funcNames:
					if funcName in dg_list:
						yield '\t\tif(apiVersion >= VK_API_VERSION_1_1) functions.push_back("%s");' % funcName
					else:
						yield '\t\tfunctions.push_back("%s");' % funcName
				if ext.name == "VK_KHR_device_group":
					for dg_func in dg_list:
						yield '\t\tif(apiVersion < VK_API_VERSION_1_1) functions.push_back("%s");' % dg_func
				yield '\t\treturn;'
				yield '\t}'
				isFirstWrite = False
		if not isFirstWrite:
			yield '\tDE_FATAL("Extension name not found");'
			yield '}'

	lines = ['']
	for line in writeExtensionFunctions(Function.TYPE_INSTANCE):
		lines += [line]
	lines += ['']
	for line in writeExtensionFunctions(Function.TYPE_DEVICE):
		lines += [line]
	lines += ['']
	for line in writeExtensionNameArrays():
		lines += [line]

	writeInlFile(filename, INL_HEADER, lines)

def writeCoreFunctionalities(api, filename):
	functionOriginValues    = ["FUNCTIONORIGIN_PLATFORM", "FUNCTIONORIGIN_INSTANCE", "FUNCTIONORIGIN_DEVICE"]
	lines					= addVersionDefines(api.versions) + [
	"",
	'enum FunctionOrigin', '{'] + [line for line in indentLines([
	'\t' + functionOriginValues[0] + '\t= 0,',
	'\t' + functionOriginValues[1] + ',',
	'\t' + functionOriginValues[2]])] + [
	"};",
	"",
	"typedef ::std::pair<const char*, FunctionOrigin> FunctionInfo;",
	"typedef ::std::vector<FunctionInfo> FunctionInfosList;",
	"typedef ::std::map<deUint32, FunctionInfosList> ApisMap;",
	"",
	"void initApisMap (ApisMap& apis)",
	"{",
	"	apis.clear();"] + [
	"	apis.insert(::std::pair<deUint32, FunctionInfosList>(" + str(v) + ", FunctionInfosList()));" for v in api.versions] + [
	""]

	apiVersions = []
	for index, v in enumerate(api.versions):
		funcs = []
		apiVersions.append("VK_VERSION_{0}_{1}".format(v.major, v.minor))
		# iterate over all functions that are core in latest vulkan version
		# note that first item in api.extension array are actually all definitions that are in vulkan.h.in before section with extensions
		for fun in api.extensions[0].functions:
			if fun.apiVersion in apiVersions:
				funcs.append('	apis[' + str(v) + '].push_back(FunctionInfo("' + fun.name + '",\t' + functionOriginValues[fun.getType()] + '));')
		lines = lines + [line for line in indentLines(funcs)] + [""]

	lines = lines + ["}", ""] + removeVersionDefines(api.versions)
	writeInlFile(filename, INL_HEADER, lines)

def writeDeviceFeatures2(api, filename):
	# list of structures that should be tested with getPhysicalDeviceFeatures2
	# this is not posible to determine from vulkan_core.h, if new feature structures
	# are added they should be manualy added to this list
	testedStructures = [
		'VkPhysicalDeviceConditionalRenderingFeaturesEXT',
		'VkPhysicalDeviceScalarBlockLayoutFeatures',
		'VkPhysicalDevicePerformanceQueryFeaturesKHR',
		'VkPhysicalDevice16BitStorageFeatures',
		'VkPhysicalDeviceMultiviewFeatures',
		'VkPhysicalDeviceProtectedMemoryFeatures',
		'VkPhysicalDeviceSamplerYcbcrConversionFeatures',
		'VkPhysicalDeviceVariablePointersFeatures',
		'VkPhysicalDevice8BitStorageFeatures',
		'VkPhysicalDeviceShaderAtomicInt64Features',
		'VkPhysicalDeviceShaderFloat16Int8Features',
		'VkPhysicalDeviceBufferDeviceAddressFeaturesEXT',
		'VkPhysicalDeviceBufferDeviceAddressFeatures',
		'VkPhysicalDeviceDescriptorIndexingFeatures',
		'VkPhysicalDeviceTimelineSemaphoreFeatures',
		'VkPhysicalDeviceFragmentDensityMapFeaturesEXT',
		'VkPhysicalDeviceFragmentDensityMap2FeaturesEXT'
	]
	# helper class used to encapsulate all data needed during generation
	class StructureDetail:
		def __init__ (self, name):
			nameResult			= re.search('(.*)Features(.*)', name[len('VkPhysicalDevice'):])
			nameSplitUp			= ''
			# generate structure type name from structure name
			# note that sometimes digits are separated with '_':
			# VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT
			# but mostly they are not:
			# VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES
			if (nameResult.group(1) == 'FragmentDensityMap2'):
				nameSplitUp		= ['FRAGMENT', 'DENSITY', 'MAP', '2', 'FEATURES']
			else:
				nameSplit		= re.findall(r'[1-9A-Z]+(?:[a-z1-9]+|[A-Z]*(?=[A-Z]|$))', nameResult.group(1))
				nameSplitUp		= map(str.upper, nameSplit)
				nameSplitUp		= list(nameSplitUp) + ['FEATURES']
			# check if there is extension suffix
			if (len(nameResult.group(2)) != 0):
				nameSplitUp.append(nameResult.group(2))
			self.name			= name
			self.sType			= 'VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_' + '_'.join(nameSplitUp)
			self.instanceName	= 'd' + name[11:]
			self.flagName		= 'is' + name[16:]
			self.extension		= None
			self.major			= None
			self.minor			= None
			self.members		= []
	# helper extension class used in algorith below
	class StructureFoundContinueToNextOne(Exception):
		pass
	testedStructureDetail = [StructureDetail(struct) for struct in testedStructures]
	# iterate over all searched structures and find extensions that enable them
	for structureDetail in testedStructureDetail:
		try:
			# iterate over all extensions
			for extension in api.extensions[1:]:
				# check composite types and typedefs in case extension is part of core
				for structureList in [extension.compositeTypes, extension.typedefs]:
					# iterate over all structures added by extension
					for extensionStructure in structureList:
						# compare checked structure name to name of structure from extension
						if structureDetail.name == extensionStructure.name:
							structureDetail.extension = extension.name
							if extension.versionInCore is not None:
								structureDetail.major = extension.versionInCore[1]
								structureDetail.minor = extension.versionInCore[2]
							raise StructureFoundContinueToNextOne
		except StructureFoundContinueToNextOne:
			continue
	for structureDetail in testedStructureDetail:
		for compositeType in api.compositeTypes:
			if structureDetail.name != compositeType.name:
				continue
			structureMembers = compositeType.members[2:]
			structureDetail.members = [m.name for m in structureMembers]
			if structureDetail.major is not None:
				break
			# if structure was not added with extension then check if
			# it was added directly with one of vulkan versions
			apiVersion = compositeType.apiVersion
			if apiVersion is None:
				continue
			structureDetail.major = apiVersion.major
			structureDetail.minor = apiVersion.minor
			break
	# generate file content
	structureDefinitions = []
	featureEnabledFlags = []
	clearStructures = []
	structureChain = []
	logStructures = []
	verifyStructures = []
	for index, structureDetail in enumerate(testedStructureDetail):
		# create two instances of each structure
		nameSpacing = '\t' * int((55 - len(structureDetail.name)) / 4)
		structureDefinitions.append(structureDetail.name + nameSpacing + structureDetail.instanceName + '[count];')
		# create flags that check if proper extension or vulkan version is available
		condition	= ''
		extension	= structureDetail.extension
		major		= structureDetail.major
		if extension is not None:
			condition = ' checkExtension(properties, "' + extension + '")'
		if major is not None:
			if condition != '':
				condition += '\t' * int((39 - len(extension)) / 4) + '|| '
			else:
				condition += '\t' * 17 + '   '
			condition += 'context.contextSupports(vk::ApiVersion(' + str(major) + ', ' + str(structureDetail.minor) + ', 0))'
		condition += ';'
		nameSpacing = '\t' * int((40 - len(structureDetail.flagName)) / 4)
		featureEnabledFlags.append('const bool ' + structureDetail.flagName + nameSpacing + '=' + condition)
		# clear memory of each structure
		nameSpacing = '\t' * int((43 - len(structureDetail.instanceName)) / 4)
		clearStructures.append('\tdeMemset(&' + structureDetail.instanceName + '[ndx],' + nameSpacing + '0xFF * ndx, sizeof(' + structureDetail.name + '));')
		# construct structure chain
		nextInstanceName = 'DE_NULL';
		if index < len(testedStructureDetail)-1:
			nextInstanceName = '&' + testedStructureDetail[index+1].instanceName + '[ndx]'
		structureChain.append('\t' + structureDetail.instanceName + '[ndx].sType = ' + structureDetail.sType + ';')
		structureChain.append('\t' + structureDetail.instanceName + '[ndx].pNext = ' + nextInstanceName + ';\n')
		# construct log section
		logStructures.append('if (' + structureDetail.flagName + ')')
		logStructures.append('\tlog << TestLog::Message << ' + structureDetail.instanceName + '[0] << TestLog::EndMessage;')
		#construct verification section
		verifyStructures.append('if (' + structureDetail.flagName + ' &&')
		for index, m in enumerate(structureDetail.members):
			prefix = '\t(' if index == 0 else '\t '
			postfix = '))' if index == len(structureDetail.members)-1 else ' ||'
			verifyStructures.append(prefix + structureDetail.instanceName + '[0].' + m + ' != ' + structureDetail.instanceName + '[1].' + m + postfix)
		verifyStructures.append('{\n\t\tTCU_FAIL("Mismatch between ' + structureDetail.name + '");\n}')
	# construct file content
	stream = []
	stream.extend(structureDefinitions)
	stream.append('')
	stream.extend(featureEnabledFlags)
	stream.append('\nfor (int ndx = 0; ndx < count; ++ndx)\n{')
	stream.extend(clearStructures)
	stream.append('')
	stream.extend(structureChain)
	stream.append('\tdeMemset(&extFeatures.features, 0xcd, sizeof(extFeatures.features));\n'
				  '\textFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;\n'
				  '\textFeatures.pNext = &' + testedStructureDetail[0].instanceName + '[ndx];\n'
				  '\tvki.getPhysicalDeviceFeatures2(physicalDevice, &extFeatures);\n}\n')
	stream.extend(logStructures)
	stream.append('')
	stream.extend(verifyStructures)
	writeInlFile(filename, INL_HEADER, stream)

def generateDeviceFeaturesDefs(src):
	# look for definitions
	ptrnSType	= r'VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_(\w+)_FEATURES(\w*)\s*='
	matches		= re.findall(ptrnSType, src, re.M)
	matches		= sorted(matches, key=lambda m: m[0])
	# construct final list
	defs = []
	for sType, sSuffix in matches:
		structName			= re.sub("[_0-9][a-z]", lambda match: match.group(0).upper(), sType.capitalize()).replace('_', '')
		ptrnStructName		= r'\s*typedef\s+struct\s+(VkPhysicalDevice' + structName + 'Features' + sSuffix.replace('_', '') + ')'
		matchStructName		= re.search(ptrnStructName, src, re.IGNORECASE)
		if matchStructName:
			# handle special cases
			if sType == "EXCLUSIVE_SCISSOR":
				sType = "SCISSOR_EXCLUSIVE"
			elif sType == "ASTC_DECODE":
				sType = "ASTC_DECODE_MODE"
			if sType in {'VULKAN_1_1', 'VULKAN_1_2'}:
				continue
			# end handling special cases
			ptrnExtensionName	= r'^\s*#define\s+(\w+' + sSuffix + '_' + sType + '_EXTENSION_NAME).+$'
			matchExtensionName	= re.search(ptrnExtensionName, src, re.M)
			ptrnSpecVersion		= r'^\s*#define\s+(\w+' + sSuffix + '_' + sType + '_SPEC_VERSION).+$'
			matchSpecVersion	= re.search(ptrnSpecVersion, src, re.M)
			defs.append( (sType, '', sSuffix, matchStructName.group(1), \
							matchExtensionName.group(0)	if matchExtensionName	else None,
							matchExtensionName.group(1)	if matchExtensionName	else None,
							matchSpecVersion.group(1)	if matchSpecVersion		else '0') )
	return defs

def generateDevicePropertiesDefs(src):
	# look for definitions
	ptrnSType	= r'VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_(\w+)_PROPERTIES(\w*)\s*='
	matches		= re.findall(ptrnSType, src, re.M)
	matches		= sorted(matches, key=lambda m: m[0])
	# construct final list
	defs = []
	for sType, sSuffix in matches:
		# handle special cases
		if sType in {'VULKAN_1_1', 'VULKAN_1_2', 'GROUP', 'MEMORY_BUDGET', 'MEMORY', 'TOOL'}:
			continue
		# there are cases like VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD
		# where 2 is after PROPERTIES - to handle this we need to split suffix to two parts
		sVerSuffix = ''
		sExtSuffix = sSuffix
		suffixStart = sSuffix.rfind('_')
		if suffixStart > 0:
			sVerSuffix = sSuffix[:suffixStart]
			sExtSuffix = sSuffix[suffixStart:]
		# handle special case
		if sType == "ID":
			structName	= sType
		else:
			structName	= re.sub("[_0-9][a-z]", lambda match: match.group(0).upper(), sType.capitalize()).replace('_', '')
		ptrnStructName		= r'\s*typedef\s+struct\s+(VkPhysicalDevice' + structName + 'Properties' + sSuffix.replace('_', '') + ')'
		matchStructName		= re.search(ptrnStructName, src, re.M)
		if matchStructName:
			extType = sType
			if extType == "MAINTENANCE_3":
				extType = "MAINTENANCE3"
			elif extType == "DISCARD_RECTANGLE":
				extType = "DISCARD_RECTANGLES"
			elif extType == "DRIVER":
				extType = "DRIVER_PROPERTIES"
			elif extType == "POINT_CLIPPING":
				extType = "MAINTENANCE2"
			elif extType == "SHADER_CORE":
				extType = "SHADER_CORE_PROPERTIES"
			# end handling special cases
			ptrnExtensionName	= r'^\s*#define\s+(\w+' + sExtSuffix + '_' + extType + sVerSuffix +'[_0-9]*_EXTENSION_NAME).+$'
			matchExtensionName	= re.search(ptrnExtensionName, src, re.M)
			ptrnSpecVersion		= r'^\s*#define\s+(\w+' + sExtSuffix + '_' + extType + sVerSuffix + '[_0-9]*_SPEC_VERSION).+$'
			matchSpecVersion	= re.search(ptrnSpecVersion, src, re.M)
			defs.append( (sType, sVerSuffix, sExtSuffix, matchStructName.group(1), \
							matchExtensionName.group(0)	if matchExtensionName	else None,
							matchExtensionName.group(1)	if matchExtensionName	else None,
							matchSpecVersion.group	(1)	if matchSpecVersion		else '0') )
	return defs

def writeDeviceFeatures(api, dfDefs, filename):
	# find VkPhysicalDeviceVulkan[1-9][0-9]Features blob structurs
	# and construct dictionary with all of their attributes
	blobMembers = {}
	blobStructs = {}
	blobPattern = re.compile("^VkPhysicalDeviceVulkan([1-9][0-9])Features[0-9]*$")
	for structureType in api.compositeTypes:
		match = blobPattern.match(structureType.name)
		if match:
			allMembers = [member.name for member in structureType.members]
			vkVersion = match.group(1)
			blobMembers[vkVersion] = allMembers[2:]
			blobStructs[vkVersion] = set()
	initFromBlobDefinitions = []
	emptyInitDefinitions = []
	# iterate over all feature structures
	allFeaturesPattern = re.compile("^VkPhysicalDevice\w+Features[1-9]*")
	nonExtFeaturesPattern = re.compile("^VkPhysicalDevice\w+Features[1-9]*$")
	for structureType in api.compositeTypes:
		# skip structures that are not feature structures
		if not allFeaturesPattern.match(structureType.name):
			continue
		# skip structures that were previously identified as blobs
		if blobPattern.match(structureType.name):
			continue
		if structureType.isAlias:
			continue
		# skip sType and pNext and just grab third and next attributes
		structureMembers = structureType.members[2:]
		notPartOfBlob = True
		if nonExtFeaturesPattern.match(structureType.name):
			# check if this member is part of any of the blobs
			for blobName, blobMemberList in blobMembers.items():
				# if just one member is not part of this blob go to the next blob
				# (we asume that all members are part of blob - no need to check all)
				if structureMembers[0].name not in blobMemberList:
					continue
				# add another feature structure name to this blob
				blobStructs[blobName].add(structureType)
				# add specialization for this feature structure
				memberCopying = ""
				for member in structureMembers:
					memberCopying += "\tfeatureType.{0} = allFeaturesBlobs.vk{1}.{0};\n".format(member.name, blobName)
				wholeFunction = \
					"template<> void initFeatureFromBlob<{0}>({0}& featureType, const AllFeaturesBlobs& allFeaturesBlobs)\n" \
					"{{\n" \
					"{1}" \
					"}}".format(structureType.name, memberCopying)
				initFromBlobDefinitions.append(wholeFunction)
				notPartOfBlob = False
				# assuming that all members are part of blob, goto next
				break
		# add empty template definition as on Fedora there are issue with
		# linking using just generic template - all specializations are needed
		if notPartOfBlob:
			emptyFunction = "template<> void initFeatureFromBlob<{0}>({0}&, const AllFeaturesBlobs&) {{}}"
			emptyInitDefinitions.append(emptyFunction.format(structureType.name))
	extensionDefines = []
	makeFeatureDescDefinitions = []
	featureStructWrappers = []
	for idx, (sType, sVerSuffix, sExtSuffix, extStruct, extLine, extName, specVer) in enumerate(dfDefs):
		extensionNameDefinition = extName
		if not extensionNameDefinition:
			extensionNameDefinition = 'DECL{0}_{1}_EXTENSION_NAME'.format((sExtSuffix if sExtSuffix else ''), sType)
		# construct defines with names
		if extLine:
			extensionDefines.append(extLine)
		else:
			extensionDefines.append('#define {0} "not_existent_feature"'.format(extensionNameDefinition))
		# handle special cases
		if sType == "SCISSOR_EXCLUSIVE":
			sType = "EXCLUSIVE_SCISSOR"
		elif sType == "ASTC_DECODE_MODE":
			sType = "ASTC_DECODE"
		# end handling special cases
		# construct makeFeatureDesc template function definitions
		sTypeName = "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_{0}_FEATURES{1}".format(sType, sVerSuffix + sExtSuffix)
		makeFeatureDescDefinitions.append("template<> FeatureDesc makeFeatureDesc<{0}>(void) " \
			"{{ return FeatureDesc{{{1}, {2}, {3}, {4}}}; }}".format(extStruct, sTypeName, extensionNameDefinition, specVer, len(dfDefs)-idx))
		# construct CreateFeatureStruct wrapper block
		featureStructWrappers.append("\t{{ createFeatureStructWrapper<{0}>, {1}, {2} }},".format(extStruct, extensionNameDefinition, specVer))
	# construct method that will check if structure sType is part of blob
	blobChecker = "bool isPartOfBlobFeatures (VkStructureType sType)\n{\n" \
				  "\tconst std::vector<VkStructureType> sTypeVect =" \
				  "\t{\n"
	# iterate over blobs with list of structures
	for blobName in sorted(blobStructs.keys()):
		blobChecker += "\t\t// Vulkan{0}\n".format(blobName)
		# iterate over all feature structures in current blob
		structuresList = list(blobStructs[blobName])
		structuresList = sorted(structuresList, key=lambda s: s.name)
		for structType in structuresList:
			# find definition of this structure in dfDefs
			structName = structType.name
			# handle special cases
			if structName == 'VkPhysicalDeviceShaderDrawParameterFeatures':
				structName = 'VkPhysicalDeviceShaderDrawParametersFeatures'
			# end handling special cases
			structDef = [s for s in dfDefs if s[3] == structName][0]
			sType = structDef[0]
			sSuffix = structDef[1] + structDef[2]
			# handle special cases
			if sType == "SCISSOR_EXCLUSIVE":
				sType = "EXCLUSIVE_SCISSOR"
			# end handling special cases
			sTypeName = "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_{0}_FEATURES{1}".format(sType, sSuffix)
			blobChecker += "\t\t{0},\n".format(sTypeName)
	blobChecker += "\t};\n" \
				   "\treturn de::contains(sTypeVect.begin(), sTypeVect.end(), sType);\n" \
				   "}\n"
	# combine all definition lists
	stream = [
	'#include "vkDeviceFeatures.hpp"\n',
	'namespace vk\n{']
	stream.extend(extensionDefines)
	stream.append('\n')
	stream.extend(initFromBlobDefinitions)
	stream.append('\n// generic template is not enough for some compilers')
	stream.extend(emptyInitDefinitions)
	stream.append('\n')
	stream.extend(makeFeatureDescDefinitions)
	stream.append('\n')
	stream.append('static const FeatureStructCreationData featureStructCreationArray[] =\n{')
	stream.extend(featureStructWrappers)
	stream.append('};\n')
	stream.append(blobChecker)
	stream.append('} // vk\n')
	writeInlFile(filename, INL_HEADER, stream)

def writeDeviceProperties(api, dpDefs, filename):
	# find VkPhysicalDeviceVulkan[1-9][0-9]Features blob structurs
	# and construct dictionary with all of their attributes
	blobMembers = {}
	blobStructs = {}
	blobPattern = re.compile("^VkPhysicalDeviceVulkan([1-9][0-9])Properties[0-9]*$")
	for structureType in api.compositeTypes:
		match = blobPattern.match(structureType.name)
		if match:
			allMembers = [member.name for member in structureType.members]
			vkVersion = match.group(1)
			blobMembers[vkVersion] = allMembers[2:]
			blobStructs[vkVersion] = set()
	initFromBlobDefinitions = []
	emptyInitDefinitions = []
	# iterate over all property structures
	allPropertiesPattern = re.compile("^VkPhysicalDevice\w+Properties[1-9]*")
	nonExtPropertiesPattern = re.compile("^VkPhysicalDevice\w+Properties[1-9]*$")
	for structureType in api.compositeTypes:
		# skip structures that are not property structures
		if not allPropertiesPattern.match(structureType.name):
			continue
		# skip structures that were previously identified as blobs
		if blobPattern.match(structureType.name):
			continue
		if structureType.isAlias:
			continue
		# skip sType and pNext and just grab third and next attributes
		structureMembers = structureType.members[2:]
		notPartOfBlob = True
		if nonExtPropertiesPattern.match(structureType.name):
			# check if this member is part of any of the blobs
			for blobName, blobMemberList in blobMembers.items():
				# if just one member is not part of this blob go to the next blob
				# (we asume that all members are part of blob - no need to check all)
				if structureMembers[0].name not in blobMemberList:
					continue
				# add another property structure name to this blob
				blobStructs[blobName].add(structureType)
				# add specialization for this property structure
				memberCopying = ""
				for member in structureMembers:
					if not member.arraySize:
						# handle special case
						if structureType.name == "VkPhysicalDeviceSubgroupProperties" and "subgroup" not in member.name :
							blobMemberName = "subgroup" + member.name[0].capitalize() + member.name[1:]
							memberCopying += "\tpropertyType.{0} = allPropertiesBlobs.vk{1}.{2};\n".format(member.name, blobName, blobMemberName)
						# end handling special case
						else:
							memberCopying += "\tpropertyType.{0} = allPropertiesBlobs.vk{1}.{0};\n".format(member.name, blobName)
					else:
						memberCopying += "\tmemcpy(propertyType.{0}, allPropertiesBlobs.vk{1}.{0}, sizeof({2}) * {3});\n".format(member.name, blobName, member.type[0], member.arraySize[1:-1])
				wholeFunction = \
					"template<> void initPropertyFromBlob<{0}>({0}& propertyType, const AllPropertiesBlobs& allPropertiesBlobs)\n" \
					"{{\n" \
					"{1}" \
					"}}".format(structureType.name, memberCopying)
				initFromBlobDefinitions.append(wholeFunction)
				notPartOfBlob = False
				# assuming that all members are part of blob, goto next
				break
		# add empty template definition as on Fedora there are issue with
		# linking using just generic template - all specializations are needed
		if notPartOfBlob:
			emptyFunction = "template<> void initPropertyFromBlob<{0}>({0}&, const AllPropertiesBlobs&) {{}}"
			emptyInitDefinitions.append(emptyFunction.format(structureType.name))
	extensionDefines = []
	makePropertyDescDefinitions = []
	propertyStructWrappers = []
	for idx, (sType, sVerSuffix, sExtSuffix, extStruct, extLine, extName, specVer) in enumerate(dpDefs):
		extensionNameDefinition = extName
		if not extensionNameDefinition:
			extensionNameDefinition = 'DECL{0}_{1}_EXTENSION_NAME'.format((sExtSuffix if sExtSuffix else ''), sType)
		# construct defines with names
		if extLine:
			extensionDefines.append(extLine)
		else:
			extensionDefines.append('#define {0} "core_property"'.format(extensionNameDefinition))
		# construct makePropertyDesc template function definitions
		sTypeName = "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_{0}_PROPERTIES{1}".format(sType, sVerSuffix + sExtSuffix)
		makePropertyDescDefinitions.append("template<> PropertyDesc makePropertyDesc<{0}>(void) " \
			"{{ return PropertyDesc{{{1}, {2}, {3}, {4}}}; }}".format(extStruct, sTypeName, extensionNameDefinition, specVer, len(dpDefs)-idx))
		# construct CreateProperty struct wrapper block
		propertyStructWrappers.append("\t{{ createPropertyStructWrapper<{0}>, {1}, {2} }},".format(extStruct, extensionNameDefinition, specVer))
			# construct method that will check if structure sType is part of blob
	blobChecker = "bool isPartOfBlobProperties (VkStructureType sType)\n{\n" \
				  "\tconst std::vector<VkStructureType> sTypeVect =" \
				  "\t{\n"
	# iterate over blobs with list of structures
	for blobName in sorted(blobStructs.keys()):
		blobChecker += "\t\t// Vulkan{0}\n".format(blobName)
		# iterate over all feature structures in current blob
		structuresList = list(blobStructs[blobName])
		structuresList = sorted(structuresList, key=lambda s: s.name)
		for structType in structuresList:
			# find definition of this structure in dpDefs
			structName = structType.name
			structDef = [s for s in dpDefs if s[3] == structName][0]
			sType = structDef[0]
			sSuffix = structDef[1] + structDef[2]
			sTypeName = "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_{0}_PROPERTIES{1}".format(sType, sSuffix)
			blobChecker += "\t\t{0},\n".format(sTypeName)
	blobChecker += "\t};\n" \
				   "\treturn de::contains(sTypeVect.begin(), sTypeVect.end(), sType);\n" \
				   "}\n"
	# combine all definition lists
	stream = [
	'#include "vkDeviceProperties.hpp"\n',
	'namespace vk\n{']
	stream.extend(extensionDefines)
	stream.append('\n')
	stream.extend(initFromBlobDefinitions)
	stream.append('\n// generic template is not enough for some compilers')
	stream.extend(emptyInitDefinitions)
	stream.append('\n')
	stream.extend(makePropertyDescDefinitions)
	stream.append('\n')
	stream.append('static const PropertyStructCreationData propertyStructCreationArray[] =\n{')
	stream.extend(propertyStructWrappers)
	stream.append('};\n')
	stream.append(blobChecker)
	stream.append('} // vk\n')
	writeInlFile(filename, INL_HEADER, stream)

def genericDeviceFeaturesWriter(dfDefs, pattern, filename):
	stream = []
	for _, _, _, extStruct, _, _, _ in dfDefs:
		nameSubStr = extStruct.replace("VkPhysicalDevice", "").replace("KHR", "").replace("NV", "")
		stream.append(pattern.format(extStruct, nameSubStr))
	writeInlFile(filename, INL_HEADER, indentLines(stream))

def writeDeviceFeaturesDefaultDeviceDefs(dfDefs, filename):
	pattern = "const {0}&\tget{1}\t(void) const {{ return m_deviceFeatures.getFeatureType<{0}>();\t}}"
	genericDeviceFeaturesWriter(dfDefs, pattern, filename)

def writeDeviceFeaturesContextDecl(dfDefs, filename):
	pattern = "const vk::{0}&\tget{1}\t(void) const;"
	genericDeviceFeaturesWriter(dfDefs, pattern, filename)

def writeDeviceFeaturesContextDefs(dfDefs, filename):
	pattern = "const vk::{0}&\tContext::get{1}\t(void) const {{ return m_device->get{1}();\t}}"
	genericDeviceFeaturesWriter(dfDefs, pattern, filename)

def genericDevicePropertiesWriter(dfDefs, pattern, filename):
	stream = []
	for _, _, _, extStruct, _, _, _ in dfDefs:
		nameSubStr = extStruct.replace("VkPhysicalDevice", "").replace("KHR", "").replace("NV", "")
		if extStruct == "VkPhysicalDeviceRayTracingPropertiesNV":
			nameSubStr += "NV"
		stream.append(pattern.format(extStruct, nameSubStr))
	writeInlFile(filename, INL_HEADER, indentLines(stream))

def writeDevicePropertiesDefaultDeviceDefs(dfDefs, filename):
	pattern = "const {0}&\tget{1}\t(void) const {{ return m_deviceProperties.getPropertyType<{0}>();\t}}"
	genericDevicePropertiesWriter(dfDefs, pattern, filename)

def writeDevicePropertiesContextDecl(dfDefs, filename):
	pattern = "const vk::{0}&\tget{1}\t(void) const;"
	genericDevicePropertiesWriter(dfDefs, pattern, filename)

def writeDevicePropertiesContextDefs(dfDefs, filename):
	pattern = "const vk::{0}&\tContext::get{1}\t(void) const {{ return m_device->get{1}();\t}}"
	genericDevicePropertiesWriter(dfDefs, pattern, filename)

def splitWithQuotation(line):
	result = []
	splitted = re.findall(r'[^"\s]\S*|".+?"', line)
	for s in splitted:
		result.append(s.replace('"', ''))
	return result

def writeMandatoryFeatures(filename):
	stream = []
	pattern = r'\s*([\w]+)\s+FEATURES\s+\((.*)\)\s+REQUIREMENTS\s+\((.*)\)'
	mandatoryFeatures = readFile(os.path.join(VULKAN_H_DIR, "mandatory_features.txt"))
	matches = re.findall(pattern, mandatoryFeatures)
	dictStructs = {}
	dictData = []
	for m in matches:
		allRequirements = splitWithQuotation(m[2])
		dictData.append( [ m[0], m[1].strip(), allRequirements ] )
		if m[0] != 'VkPhysicalDeviceFeatures' :
			if (m[0] not in dictStructs):
				dictStructs[m[0]] = [m[0][2:3].lower() + m[0][3:]]
			if (allRequirements[0]):
				if (allRequirements[0] not in dictStructs[m[0]][1:]):
					dictStructs[m[0]].append(allRequirements[0])

	stream.extend(['bool checkMandatoryFeatures(const vkt::Context& context)\n{',
				   '\tif (!context.isInstanceFunctionalitySupported("VK_KHR_get_physical_device_properties2"))',
				   '\t\tTCU_THROW(NotSupportedError, "Extension VK_KHR_get_physical_device_properties2 is not present");',
				   '',
				   '\tVkPhysicalDevice\t\t\t\t\tphysicalDevice\t\t= context.getPhysicalDevice();',
				   '\tconst InstanceInterface&\t\t\tvki\t\t\t\t\t= context.getInstanceInterface();',
				   '\tconst vector<VkExtensionProperties>\tdeviceExtensions\t= enumerateDeviceExtensionProperties(vki, physicalDevice, DE_NULL);',
				   '',
				   '\ttcu::TestLog& log = context.getTestContext().getLog();',
				   '\tvk::VkPhysicalDeviceFeatures2 coreFeatures;',
				   '\tdeMemset(&coreFeatures, 0, sizeof(coreFeatures));',
				   '\tcoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;',
				   '\tvoid** nextPtr = &coreFeatures.pNext;',
				   ''])

	listStruct = sorted(dictStructs.items(), key=lambda tup: tup[0]) # sort to have same results for py2 and py3
	for k, v in listStruct:
		if (v[1].startswith("ApiVersion")):
			cond = '\tif (context.contextSupports(vk::' + v[1] + '))'
		else:
			cond = '\tif (vk::isDeviceExtensionSupported(context.getUsedApiVersion(), context.getDeviceExtensions(), "' + v[1] + '"))'
		stream.extend(['\tvk::' + k + ' ' + v[0]+ ';',
					'\tdeMemset(&' + v[0] + ', 0, sizeof(' + v[0] + '));',
					''])
		reqs = v[1:]
		if len(reqs) > 0 :
			cond = 'if ( '
			for i, req in enumerate(reqs) :
				if (req.startswith("ApiVersion")):
					cond = cond + 'context.contextSupports(vk::' + req + ')'
				else:
					cond = cond + 'isExtensionSupported(deviceExtensions, RequiredExtension("' + req + '"))'
				if i+1 < len(reqs) :
					cond = cond + ' || '
			cond = cond + ' )'
			stream.append('\t' + cond)
		stream.extend(['\t{',
					   '\t\t' + v[0] + '.sType = getStructureType<' + k + '>();',
					   '\t\t*nextPtr = &' + v[0] + ';',
					   '\t\tnextPtr  = &' + v[0] + '.pNext;',
					   '\t}',
					   ''])
	stream.extend(['\tcontext.getInstanceInterface().getPhysicalDeviceFeatures2(context.getPhysicalDevice(), &coreFeatures);',
				   '\tbool result = true;',
				   ''])

	for v in dictData:
		structType = v[0];
		structName = 'coreFeatures.features';
		if v[0] != 'VkPhysicalDeviceFeatures' :
			structName = dictStructs[v[0]][0]
		if len(v[2]) > 0 :
			condition = 'if ( '
			for i, req in enumerate(v[2]) :
				if (req.startswith("ApiVersion")):
					condition = condition + 'context.contextSupports(vk::' + req + ')'
				elif '.' in req:
					condition = condition + req
				else:
					condition = condition + 'isExtensionSupported(deviceExtensions, RequiredExtension("' + req + '"))'
				if i+1 < len(v[2]) :
					condition = condition + ' && '
			condition = condition + ' )'
			stream.append('\t' + condition)
		stream.append('\t{')
		# Don't need to support an AND case since that would just be another line in the .txt
		if len(v[1].split(" ")) == 1:
			stream.append('\t\tif ( ' + structName + '.' + v[1] + ' == VK_FALSE )')
		else:
			condition = 'if ( '
			for i, feature in enumerate(v[1].split(" ")):
				if i != 0:
					condition = condition + ' && '
				condition = condition + '( ' + structName + '.' + feature + ' == VK_FALSE )'
			condition = condition + ' )'
			stream.append('\t\t' + condition)
		featureSet = v[1].replace(" ", " or ")
		stream.extend(['\t\t{',
					   '\t\t\tlog << tcu::TestLog::Message << "Mandatory feature ' + featureSet + ' not supported" << tcu::TestLog::EndMessage;',
					   '\t\t\tresult = false;',
					   '\t\t}',
					   '\t}',
					   ''])
	stream.append('\treturn result;')
	stream.append('}\n')
	writeInlFile(filename, INL_HEADER, stream)

def writeExtensionList(filename, patternPart):
	stream = []
	stream.append('static const char* s_allowed{0}KhrExtensions[] =\n{{'.format(patternPart.title()))
	extensionsData = readFile(os.path.join(VULKAN_H_DIR, "extensions_data.txt"))
	pattern = r'\s*([^\s]+)\s+{0}\s*[0-9_]*'.format(patternPart)
	matches	= re.findall(pattern, extensionsData)
	for m in matches:
		stream.append('\t"' + m + '",')
	stream.append('};\n')
	writeInlFile(filename, INL_HEADER, stream)

if __name__ == "__main__":
	# Read all .h files, with vulkan_core.h first
	files			= os.listdir(VULKAN_H_DIR)
	files			= [f for f in files if f.endswith(".h")]
	files.sort()
	files.remove("vulkan_core.h")
	files.insert(0, "vulkan_core.h")
	src				= ""
	for file in files:
		src += readFile(os.path.join(VULKAN_H_DIR,file))
	api				= parseAPI(src)

	platformFuncs	= [Function.TYPE_PLATFORM]
	instanceFuncs	= [Function.TYPE_INSTANCE]
	deviceFuncs		= [Function.TYPE_DEVICE]

	dfd										= generateDeviceFeaturesDefs(src)
	writeDeviceFeatures						(api, dfd, os.path.join(VULKAN_DIR, "vkDeviceFeatures.inl"))
	writeDeviceFeaturesDefaultDeviceDefs	(dfd, os.path.join(VULKAN_DIR, "vkDeviceFeaturesForDefaultDeviceDefs.inl"))
	writeDeviceFeaturesContextDecl			(dfd, os.path.join(VULKAN_DIR, "vkDeviceFeaturesForContextDecl.inl"))
	writeDeviceFeaturesContextDefs			(dfd, os.path.join(VULKAN_DIR, "vkDeviceFeaturesForContextDefs.inl"))

	dpd										= generateDevicePropertiesDefs(src)
	writeDeviceProperties					(api, dpd, os.path.join(VULKAN_DIR, "vkDeviceProperties.inl"))

	writeDevicePropertiesDefaultDeviceDefs	(dpd, os.path.join(VULKAN_DIR, "vkDevicePropertiesForDefaultDeviceDefs.inl"))
	writeDevicePropertiesContextDecl		(dpd, os.path.join(VULKAN_DIR, "vkDevicePropertiesForContextDecl.inl"))
	writeDevicePropertiesContextDefs		(dpd, os.path.join(VULKAN_DIR, "vkDevicePropertiesForContextDefs.inl"))

	writeHandleType							(api, os.path.join(VULKAN_DIR, "vkHandleType.inl"))
	writeBasicTypes							(api, os.path.join(VULKAN_DIR, "vkBasicTypes.inl"))
	writeCompositeTypes						(api, os.path.join(VULKAN_DIR, "vkStructTypes.inl"))
	writeInterfaceDecl						(api, os.path.join(VULKAN_DIR, "vkVirtualPlatformInterface.inl"),		platformFuncs,	False)
	writeInterfaceDecl						(api, os.path.join(VULKAN_DIR, "vkVirtualInstanceInterface.inl"),		instanceFuncs,	False)
	writeInterfaceDecl						(api, os.path.join(VULKAN_DIR, "vkVirtualDeviceInterface.inl"),			deviceFuncs,	False)
	writeInterfaceDecl						(api, os.path.join(VULKAN_DIR, "vkConcretePlatformInterface.inl"),		platformFuncs,	True)
	writeInterfaceDecl						(api, os.path.join(VULKAN_DIR, "vkConcreteInstanceInterface.inl"),		instanceFuncs,	True)
	writeInterfaceDecl						(api, os.path.join(VULKAN_DIR, "vkConcreteDeviceInterface.inl"),		deviceFuncs,	True)
	writeFunctionPtrTypes					(api, os.path.join(VULKAN_DIR, "vkFunctionPointerTypes.inl"))
	writeFunctionPointers					(api, os.path.join(VULKAN_DIR, "vkPlatformFunctionPointers.inl"),		platformFuncs)
	writeFunctionPointers					(api, os.path.join(VULKAN_DIR, "vkInstanceFunctionPointers.inl"),		instanceFuncs)
	writeFunctionPointers					(api, os.path.join(VULKAN_DIR, "vkDeviceFunctionPointers.inl"),			deviceFuncs)
	writeInitFunctionPointers				(api, os.path.join(VULKAN_DIR, "vkInitPlatformFunctionPointers.inl"),	platformFuncs,	lambda f: f.name != "vkGetInstanceProcAddr")
	writeInitFunctionPointers				(api, os.path.join(VULKAN_DIR, "vkInitInstanceFunctionPointers.inl"),	instanceFuncs)
	writeInitFunctionPointers				(api, os.path.join(VULKAN_DIR, "vkInitDeviceFunctionPointers.inl"),		deviceFuncs)
	writeFuncPtrInterfaceImpl				(api, os.path.join(VULKAN_DIR, "vkPlatformDriverImpl.inl"),				platformFuncs,	"PlatformDriver")
	writeFuncPtrInterfaceImpl				(api, os.path.join(VULKAN_DIR, "vkInstanceDriverImpl.inl"),				instanceFuncs,	"InstanceDriver")
	writeFuncPtrInterfaceImpl				(api, os.path.join(VULKAN_DIR, "vkDeviceDriverImpl.inl"),				deviceFuncs,	"DeviceDriver")
	writeStrUtilProto						(api, os.path.join(VULKAN_DIR, "vkStrUtil.inl"))
	writeStrUtilImpl						(api, os.path.join(VULKAN_DIR, "vkStrUtilImpl.inl"))
	writeRefUtilProto						(api, os.path.join(VULKAN_DIR, "vkRefUtil.inl"))
	writeRefUtilImpl						(api, os.path.join(VULKAN_DIR, "vkRefUtilImpl.inl"))
	writeStructTraitsImpl					(api, os.path.join(VULKAN_DIR, "vkGetStructureTypeImpl.inl"))
	writeNullDriverImpl						(api, os.path.join(VULKAN_DIR, "vkNullDriverImpl.inl"))
	writeTypeUtil							(api, os.path.join(VULKAN_DIR, "vkTypeUtil.inl"))
	writeSupportedExtenions					(api, os.path.join(VULKAN_DIR, "vkSupportedExtensions.inl"))
	writeCoreFunctionalities				(api, os.path.join(VULKAN_DIR, "vkCoreFunctionalities.inl"))
	writeExtensionFunctions					(api, os.path.join(VULKAN_DIR, "vkExtensionFunctions.inl"))
	writeDeviceFeatures2					(api, os.path.join(VULKAN_DIR, "vkDeviceFeatures2.inl"))
	writeMandatoryFeatures					(     os.path.join(VULKAN_DIR, "vkMandatoryFeatures.inl"))
	writeExtensionList						(     os.path.join(VULKAN_DIR, "vkInstanceExtensions.inl"),				'INSTANCE')
	writeExtensionList						(     os.path.join(VULKAN_DIR, "vkDeviceExtensions.inl"),				'DEVICE')
	writeDriverIds							(     os.path.join(VULKAN_DIR, "vkKnownDriverIds.inl"))
	writeObjTypeImpl						(api, os.path.join(VULKAN_DIR, "vkObjTypeImpl.inl"))
