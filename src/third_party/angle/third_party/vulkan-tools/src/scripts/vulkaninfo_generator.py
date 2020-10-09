#!/usr/bin/python3
#
# Copyright (c) 2019 Valve Corporation
# Copyright (c) 2019 LunarG, Inc.
# Copyright (c) 2019 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Author: Charles Giessen <charles@lunarg.com>

import os
import re
import sys
import string
import xml.etree.ElementTree as etree
import generator as gen
import operator
import json
from collections import namedtuple
from collections import OrderedDict
from generator import *
from common_codegen import *

license_header = '''
/*
 * Copyright (c) 2019 The Khronos Group Inc.
 * Copyright (c) 2019 Valve Corporation
 * Copyright (c) 2019 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Charles Giessen <charles@lunarg.com>
 *
 */

/*
 * This file is generated from the Khronos Vulkan XML API Registry.
 */
'''

custom_formaters = r'''
void DumpVkConformanceVersion(Printer &p, std::string name, VkConformanceVersion &c, int width = 0) {
    p.PrintKeyString("conformanceVersion", std::to_string(c.major)+ "." + std::to_string(c.minor) + "." + std::to_string(c.subminor) + "."
             + std::to_string(c.patch), width);
}

template <typename T>
std::string to_hex_str(T i) {
    std::stringstream stream;
    stream << "0x" << std::setfill('0') << std::setw(sizeof(T)) << std::hex << i;
    return stream.str();
}

template <typename T>
std::string to_hex_str(Printer &p, T i) {
    if (p.Type() == OutputType::json)
        return std::to_string(i);
    else if (p.Type() == OutputType::vkconfig_output)
        return std::string("\"") + to_hex_str(i) + std::string("\"");
    else
        return to_hex_str(i);
}

'''


# used in the .cpp code
structures_to_gen = ['VkExtent3D', 'VkExtent2D', 'VkPhysicalDeviceLimits', 'VkPhysicalDeviceFeatures', 'VkPhysicalDeviceSparseProperties',
                     'VkSurfaceCapabilitiesKHR', 'VkSurfaceFormatKHR', 'VkLayerProperties', 'VkPhysicalDeviceToolPropertiesEXT']
enums_to_gen = ['VkResult', 'VkFormat', 'VkPresentModeKHR',
                'VkPhysicalDeviceType', 'VkImageTiling']
flags_to_gen = ['VkSurfaceTransformFlagsKHR', 'VkCompositeAlphaFlagsKHR', 'VkSurfaceCounterFlagsEXT',
                'VkDeviceGroupPresentModeFlagsKHR', 'VkFormatFeatureFlags', 'VkMemoryPropertyFlags', 'VkMemoryHeapFlags']
flags_strings_to_gen = ['VkQueueFlags']

struct_short_versions_to_gen = ['VkExtent3D']

struct_comparisons_to_gen = ['VkSurfaceFormatKHR', 'VkSurfaceFormat2KHR', 'VkSurfaceCapabilitiesKHR',
                             'VkSurfaceCapabilities2KHR', 'VkSurfaceCapabilities2EXT']
# don't generate these structures
struct_blacklist = ['VkConformanceVersion']

# iostream or custom outputter handles these types
predefined_types = ['char', 'VkBool32', 'uint32_t', 'uint8_t', 'int32_t',
                    'float', 'uint64_t', 'size_t', 'VkDeviceSize']

# Types that need pNext Chains built. 'extends' is the xml tag used in the structextends member. 'type' can be device, instance, or both
EXTENSION_CATEGORIES = OrderedDict((('phys_device_props2', {'extends': 'VkPhysicalDeviceProperties2', 'type': 'both'}),
                                   ('phys_device_mem_props2', {'extends': 'VkPhysicalDeviceMemoryProperties2', 'type': 'device'}),
                                   ('phys_device_features2', {'extends': 'VkPhysicalDeviceFeatures2,VkDeviceCreateInfo', 'type': 'device'}),
                                   ('surface_capabilities2', {'extends': 'VkSurfaceCapabilities2KHR', 'type': 'both'}),
                                   ('format_properties2', {'extends': 'VkFormatProperties2', 'type': 'device'})
                                   ))
class VulkanInfoGeneratorOptions(GeneratorOptions):
    def __init__(self,
                 conventions=None,
                 input=None,
                 filename=None,
                 directory='.',
                 genpath = None,
                 apiname=None,
                 profile=None,
                 versions='.*',
                 emitversions='.*',
                 defaultExtensions=None,
                 addExtensions=None,
                 removeExtensions=None,
                 emitExtensions=None,
                 sortProcedure=None,
                 prefixText="",
                 genFuncPointers=True,
                 protectFile=True,
                 protectFeature=True,
                 protectProto=None,
                 protectProtoStr=None,
                 apicall='',
                 apientry='',
                 apientryp='',
                 indentFuncProto=True,
                 indentFuncPointer=False,
                 alignFuncParam=0,
                 expandEnumerants=True,
                 ):
        GeneratorOptions.__init__(self,
                 conventions = conventions,
                 filename = filename,
                 directory = directory,
                 genpath = genpath,
                 apiname = apiname,
                 profile = profile,
                 versions = versions,
                 emitversions = emitversions,
                 defaultExtensions = defaultExtensions,
                 addExtensions = addExtensions,
                 removeExtensions = removeExtensions,
                 emitExtensions = emitExtensions,
                 sortProcedure = sortProcedure)
        self.input = input
        self.prefixText = prefixText
        self.genFuncPointers = genFuncPointers
        self.protectFile = protectFile
        self.protectFeature = protectFeature
        self.protectProto = protectProto
        self.protectProtoStr = protectProtoStr
        self.apicall = apicall
        self.apientry = apientry
        self.apientryp = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam = alignFuncParam

# VulkanInfoGenerator - subclass of OutputGenerator.
# Generates a vulkan info output helper function


class VulkanInfoGenerator(OutputGenerator):

    def __init__(self,
                 errFile=sys.stderr,
                 warnFile=sys.stderr,
                 diagFile=sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)

        self.constants = OrderedDict()

        self.types_to_gen = set()

        self.extension_sets = OrderedDict()
        for ext_cat in EXTENSION_CATEGORIES.keys():
            self.extension_sets[ext_cat] = set()

        self.enums = []
        self.flags = []
        self.bitmasks = []
        self.all_structures = []
        self.aliases = OrderedDict()

        self.extFuncs = OrderedDict()
        self.extTypes = OrderedDict()

        self.vendor_abbreviations = []
        self.vulkan_versions = []

    def beginFile(self, genOpts):
        gen.OutputGenerator.beginFile(self, genOpts)

        for node in self.registry.reg.findall('enums'):
            if node.get('name') == 'API Constants':
                for item in node.findall('enum'):
                    self.constants[item.get('name')] = item.get('value')

        for node in self.registry.reg.find('extensions').findall('extension'):
            ext = VulkanExtension(node)
            for item in ext.vktypes:
                self.extTypes[item] = ext
            for item in ext.vkfuncs:
                self.extFuncs[item] = ext

        # need list of venders to blacklist vendor extensions
        for tag in self.registry.reg.find('tags'):
            if tag.get("name") not in ["KHR", "EXT"]:
                self.vendor_abbreviations.append("_" + tag.get('name'))

        for ver in self.registry.reg.findall('feature'):
            self.vulkan_versions.append(VulkanVersion(ver))

    def endFile(self):
        # gather the types that are needed to generate
        types_to_gen = set()
        for s in enums_to_gen:
            types_to_gen.add(s)

        for f in flags_to_gen:
            types_to_gen.add(f)

        types_to_gen.update(
            GatherTypesToGen(self.all_structures, structures_to_gen))
        for key in EXTENSION_CATEGORIES.keys():
            types_to_gen.update(
                GatherTypesToGen(self.all_structures, self.extension_sets[key]))
        types_to_gen = sorted(types_to_gen)

        names_of_structures_to_gen = set()
        for s in self.all_structures:
            if s.name in types_to_gen:
                names_of_structures_to_gen.add(s.name)
        names_of_structures_to_gen = sorted(names_of_structures_to_gen)

        structs_to_comp = set()
        for s in struct_comparisons_to_gen:
            structs_to_comp.add(s)
        structs_to_comp.update(
            GatherTypesToGen(self.all_structures, struct_comparisons_to_gen))

        for key, value in self.extension_sets.items():
            self.extension_sets[key] = sorted(value)

        alias_versions = OrderedDict()
        for version in self.vulkan_versions:
            for aliased_type, aliases in self.aliases.items():
                for alias in aliases:
                    if alias in version.names:
                        alias_versions[alias] = version.minorVersion

        self.enums = sorted(self.enums, key=operator.attrgetter('name'))
        self.flags = sorted(self.flags, key=operator.attrgetter('name'))
        self.bitmasks = sorted(self.bitmasks, key=operator.attrgetter('name'))
        self.all_structures = sorted(self.all_structures, key=operator.attrgetter('name'))

        # print the types gathered
        out = ''
        out += license_header + "\n"
        out += "#include \"vulkaninfo.h\"\n"
        out += "#include \"outputprinter.h\"\n"
        out += custom_formaters

        for enum in (e for e in self.enums if e.name in types_to_gen):
            out += PrintEnumToString(enum, self)
            out += PrintEnum(enum, self)

        for flag in self.flags:
            if flag.name in types_to_gen:
                for bitmask in (b for b in self.bitmasks if b.name == flag.enum):
                    out += PrintBitMask(bitmask, flag.name, self)

            if flag.name in flags_strings_to_gen:
                for bitmask in (b for b in self.bitmasks if b.name == flag.enum):
                    out += PrintBitMaskToString(bitmask, flag.name, self)

        for s in (x for x in self.all_structures if x.name in types_to_gen and x.name not in struct_blacklist):
            out += PrintStructure(s, types_to_gen, names_of_structures_to_gen)

        out += "pNextChainInfos get_chain_infos() {\n"
        out += "    pNextChainInfos infos;\n"
        for key in EXTENSION_CATEGORIES.keys():
            out += PrintChainBuilders(key,
                                      self.extension_sets[key], self.all_structures)
        out += "    return infos;\n}\n"

        for key, value in EXTENSION_CATEGORIES.items():
            out += PrintChainIterator(key,
                                      self.extension_sets[key], self.all_structures, value.get('type'), self.extTypes, self.aliases, self.vulkan_versions)

        for s in (x for x in self.all_structures if x.name in structs_to_comp):
            out += PrintStructComparisonForwardDecl(s)
        for s in (x for x in self.all_structures if x.name in structs_to_comp):
            out += PrintStructComparison(s)
        for s in (x for x in self.all_structures if x.name in struct_short_versions_to_gen):
            out += PrintStructShort(s)

        gen.write(out, file=self.outFile)

        gen.OutputGenerator.endFile(self)

    def genCmd(self, cmd, name, alias):
        gen.OutputGenerator.genCmd(self, cmd, name, alias)

    # These are actually constants
    def genEnum(self, enuminfo, name, alias):
        gen.OutputGenerator.genEnum(self, enuminfo, name, alias)

    # These are actually enums
    def genGroup(self, groupinfo, groupName, alias):
        gen.OutputGenerator.genGroup(self, groupinfo, groupName, alias)

        if alias is not None:
            if alias in self.aliases.keys():
                self.aliases[alias].append(groupName)
            else:
                self.aliases[alias] = [groupName, ]
            return

        if groupinfo.elem.get('type') == 'bitmask':
            self.bitmasks.append(VulkanBitmask(groupinfo.elem))
        elif groupinfo.elem.get('type') == 'enum':
            self.enums.append(VulkanEnum(groupinfo.elem))

    def genType(self, typeinfo, name, alias):
        gen.OutputGenerator.genType(self, typeinfo, name, alias)

        if alias is not None:
            if alias in self.aliases.keys():
                self.aliases[alias].append(name)
            else:
                self.aliases[alias] = [name, ]
            return

        if typeinfo.elem.get('category') == 'bitmask':
            self.flags.append(VulkanFlags(typeinfo.elem))

        if typeinfo.elem.get('category') == 'struct':
            self.all_structures.append(VulkanStructure(
                name, typeinfo.elem, self.constants, self.extTypes))

        for vendor in self.vendor_abbreviations:
            for node in typeinfo.elem.findall('member'):
                if(node.get('values') is not None):
                    if(node.get('values').find(vendor)) != -1:
                        return

        for key, value in EXTENSION_CATEGORIES.items():
            if typeinfo.elem.get('structextends') == value.get('extends'):
                self.extension_sets[key].add(name)


def GatherTypesToGen(structure_list, structures):
    types = set()
    added_stuff = True  # repeat until no new types are added
    while added_stuff == True:
        added_stuff = False
        for s in (x for x in structure_list if x.name in structures):
            size = len(types)
            types.add(s.name)
            if len(types) != size:
                added_stuff = True
            for m in s.members:
                if m.typeID not in predefined_types and m.name not in ['sType', 'pNext']:
                    types.add(m.typeID)
    types = sorted(types)
    return types


def GetExtension(name, generator):
    if name in generator.extFuncs:
        return generator.extFuncs[name]
    elif name in generator.extTypes:
        return generator.extTypes[name]
    else:
        return None


def AddGuardHeader(obj):
    if obj is not None and obj.guard is not None:
        return "#ifdef {}\n".format(obj.guard)
    else:
        return ""


def AddGuardFooter(obj):
    if obj is not None and obj.guard is not None:
        return "#endif  // {}\n".format(obj.guard)
    else:
        return ""


def PrintEnumToString(enum, gen):
    out = ''
    out += AddGuardHeader(GetExtension(enum.name, gen))

    out += "static const char *" + enum.name + \
        "String(" + enum.name + " value) {\n"
    out += "    switch (value) {\n"
    for v in enum.options:
        out += "        case (" + str(v.value) + \
            "): return \"" + v.name[3:] + "\";\n"
    out += "        default: return \"UNKNOWN_" + enum.name + "\";\n"
    out += "    }\n}\n"
    out += AddGuardFooter(GetExtension(enum.name, gen))
    return out


def PrintEnum(enum, gen):
    out = ''
    out += AddGuardHeader(GetExtension(enum.name, gen))
    out += "void Dump" + enum.name + \
        "(Printer &p, std::string name, " + \
        enum.name + " value, int width = 0) {\n"
    out += "    if (p.Type() == OutputType::json) {\n"
    out += "        p.PrintKeyValue(name, value, width);\n"
    out += "    } else {\n"
    out += "        p.PrintKeyString(name, " + \
        enum.name + "String(value), width);\n    }\n"
    out += "}\n"
    out += AddGuardFooter(GetExtension(enum.name, gen))
    return out


def PrintGetFlagStrings(name, bitmask):
    out = ''
    out += "std::vector<const char *>" + name + \
        "GetStrings(" + name + " value) {\n"

    out += "    std::vector<const char *> strings;\n"
    out += "    if (value == 0) strings.push_back(\"None\");\n"
    for v in bitmask.options:
        val = v.value if isinstance(v.value, str) else str(hex(v.value))
        out += "    if (" + val + " & value) strings.push_back(\"" + \
            str(v.name[3:]) + "\");\n"
    out += "    return strings;\n}\n"
    return out


def PrintFlags(bitmask, name):
    out = "void Dump" + name + \
        "(Printer &p, std::string name, " + name + " value, int width = 0) {\n"
    out += "    if (p.Type() == OutputType::json) { p.PrintKeyValue(name, value); return; }\n"
    out += "    auto strings = " + bitmask.name + \
        "GetStrings(static_cast<" + bitmask.name + ">(value));\n"
    out += "    if (static_cast<" + bitmask.name + ">(value) == 0) {\n"
    out += "        ArrayWrapper arr(p, name, 0);\n"
    out += "        p.SetAsType().PrintString(\"None\");\n"
    out += "        return;\n"
    out += "    }\n"
    out += "    ArrayWrapper arr(p, name, strings.size());\n"
    out += "    for(auto& str : strings){\n"
    out += "        p.SetAsType().PrintString(str);\n"
    out += "    }\n"
    out += "}\n"
    return out


def PrintFlagBits(bitmask):
    out = "void Dump" + bitmask.name + \
        "(Printer &p, std::string name, " + \
        bitmask.name + " value, int width = 0) {\n"
    out += "    auto strings = " + bitmask.name + "GetStrings(value);\n"
    out += "    p.PrintKeyString(name, strings.at(0), width);\n"
    out += "}\n"
    return out


def PrintBitMask(bitmask, name, gen):
    out = PrintGetFlagStrings(bitmask.name, bitmask)
    out += AddGuardHeader(GetExtension(bitmask.name, gen))
    out += PrintFlags(bitmask, name)
    out += PrintFlagBits(bitmask)
    out += AddGuardFooter(GetExtension(bitmask.name, gen))
    out += "\n"
    return out


def PrintBitMaskToString(bitmask, name, gen):
    out = AddGuardHeader(GetExtension(bitmask.name, gen))
    out += "std::string " + name + \
        "String(" + name + " value, int width = 0) {\n"
    out += "    std::string out;\n"
    out += "    bool is_first = true;\n"
    for v in bitmask.options:
        out += "    if (" + str(v.value) + " & value) {\n"
        out += "        if (is_first) { is_first = false; } else { out += \" | \"; }\n"
        out += "        out += \"" + \
            str(v.name).strip("VK_").strip("_BIT") + "\";\n"
        out += "    }\n"
    out += "    return out;\n"
    out += "}\n"
    out += AddGuardFooter(GetExtension(bitmask.name, gen))
    return out


def PrintStructure(struct, types_to_gen, structure_names):
    if len(struct.members) == 0:
        return ""
    out = ''
    out += AddGuardHeader(struct)
    max_key_len = 0
    for v in struct.members:
        if v.arrayLength is not None:
            if len(v.name) + len(v.arrayLength) + 2 > max_key_len:
                max_key_len = len(v.name) + len(v.arrayLength) + 2
        elif v.typeID in predefined_types or v.typeID in struct_blacklist:
            if len(v.name) > max_key_len:
                max_key_len = len(v.name)

    out += "void Dump" + struct.name + \
        "(Printer &p, std::string name, " + struct.name + " &obj) {\n"
    if struct.name == "VkPhysicalDeviceLimits":
        out += "    if (p.Type() == OutputType::json)\n"
        out += "        p.ObjectStart(\"limits\");\n"
        out += "    else\n"
        out += "        p.SetSubHeader().ObjectStart(name);\n"
    elif struct.name == "VkPhysicalDeviceSparseProperties":
        out += "    if (p.Type() == OutputType::json)\n"
        out += "        p.ObjectStart(\"sparseProperties\");\n"
        out += "    else\n"
        out += "        p.SetSubHeader().ObjectStart(name);\n"
    else:
        out += "    ObjectWrapper object{p, name};\n"

    for v in struct.members:
        # arrays
        if v.arrayLength is not None:
            # strings
            if v.typeID == "char":
                out += "    p.PrintKeyString(\"" + v.name + "\", obj." + \
                    v.name + ", " + str(max_key_len) + ");\n"
            # uuid's
            elif (v.arrayLength == str(16) and v.typeID == "uint8_t"):  # VK_UUID_SIZE
                out += "    p.PrintKeyString(\"" + v.name + "\", to_string_16(obj." + \
                    v.name + "), " + str(max_key_len) + ");\n"
            elif (v.arrayLength == str(8) and v.typeID == "uint8_t"):  # VK_LUID_SIZE
                out += "    if (obj.deviceLUIDValid)"  # special case
                out += " p.PrintKeyString(\"" + v.name + "\", to_string_8(obj." + \
                    v.name + "), " + str(max_key_len) + ");\n"
            elif v.arrayLength.isdigit():
                out += "    {   ArrayWrapper arr(p,\"" + v.name + \
                    "\", "+v.arrayLength+");\n"
                for i in range(0, int(v.arrayLength)):
                    out += "        p.PrintElement(obj." + \
                        v.name + "[" + str(i) + "]);\n"
                out += "    }\n"
            else:  # dynamic array length based on other member
                out += "    ArrayWrapper arr(p,\"" + v.name + \
                    "\", obj."+v.arrayLength+");\n"
                out += "    for (uint32_t i = 0; i < obj." + \
                    v.arrayLength+"; i++) {\n"
                if v.typeID in types_to_gen:
                    out += "        if (obj." + v.name + " != nullptr) {\n"
                    out += "            p.SetElementIndex(i);\n"
                    out += "            Dump" + v.typeID + \
                        "(p, \"" + v.name + "\", obj." + v.name + "[i]);\n"
                    out += "        }\n"
                else:
                    out += "        p.PrintElement(obj." + v.name + "[i]);\n"
                out += "    }\n"
        elif v.typeID == "VkBool32":
            out += "    p.PrintKeyBool(\"" + v.name + "\", static_cast<bool>(obj." + \
                v.name + "), " + str(max_key_len) + ");\n"
        elif v.typeID == "VkConformanceVersion":
            out += "    DumpVkConformanceVersion(p, \"conformanceVersion\", obj." + \
                v.name + ", " + str(max_key_len) + ");\n"
        elif v.typeID == "VkDeviceSize":
            out += "    p.PrintKeyValue(\"" + v.name + "\", to_hex_str(p, obj." + \
                v.name + "), " + str(max_key_len) + ");\n"
        elif v.typeID in predefined_types:
            out += "    p.PrintKeyValue(\"" + v.name + "\", obj." + \
                v.name + ", " + str(max_key_len) + ");\n"
        elif v.name not in ['sType', 'pNext']:
            # if it is an enum/flag/bitmask, add the calculated width
            if v.typeID not in structure_names:
                out += "    Dump" + v.typeID + \
                    "(p, \"" + v.name + "\", obj." + \
                    v.name + ", " + str(max_key_len) + ");\n"
            else:
                out += "    Dump" + v.typeID + \
                    "(p, \"" + v.name + "\", obj." + v.name + ");\n"
    if struct.name in ["VkPhysicalDeviceLimits", "VkPhysicalDeviceSparseProperties"]:
        out += "    p.ObjectEnd();\n"
    out += "}\n"

    out += AddGuardFooter(struct)
    return out


def PrintStructShort(struct):
    out = ''
    out += AddGuardHeader(struct)
    out += "std::ostream &operator<<(std::ostream &o, " + \
        struct.name + " &obj) {\n"
    out += "    return o << \"(\" << "

    first = True
    for v in struct.members:
        if first:
            first = False
            out += "obj." + v.name + " << "
        else:
            out += "\',\' << obj." + v.name + " << "
    out += "\")\";\n"
    out += "}\n"
    out += AddGuardFooter(struct)
    return out


def PrintChainBuilders(listName, structures, all_structures):
    sorted_structures = sorted(
        all_structures, key=operator.attrgetter('name'))

    out = ''
    out += "    infos." + listName + " = {\n"
    for s in sorted_structures:
        if s.name in structures:
            out += AddGuardHeader(s)
            if s.sTypeName is not None:
                out += "        {" + s.sTypeName + \
                    ", sizeof(" + s.name + ")},\n"
            out += AddGuardFooter(s)
    out += "    };\n"
    return out


def PrintChainIterator(listName, structures, all_structures, checkExtLoc, extTypes, aliases, versions):
    out = ''
    out += "void chain_iterator_" + listName + "(Printer &p, "
    if checkExtLoc == "device":
        out += "AppGpu &gpu"
    elif checkExtLoc == "instance":
        out += "AppInstance &inst"
    elif checkExtLoc == "both":
        out += "AppInstance &inst, AppGpu &gpu"
    out += ", void * place, VulkanVersion version) {\n"

    out += "    while (place) {\n"
    out += "        struct VkStructureHeader *structure = (struct VkStructureHeader *)place;\n"
    out += "        p.SetSubHeader();\n"
    sorted_structures = sorted(
        all_structures, key=operator.attrgetter('name'))
    for s in sorted_structures:
        if s.sTypeName is None:
            continue

        extNameStr = None
        extType = None
        for k, e in extTypes.items():
            if k == s.name or (s.name in aliases.keys() and k in aliases[s.name]):
                if e.extNameStr is not None:
                    extNameStr = e.extNameStr
                if e.type is not None:
                    extType = e.type
                break
        version = None
        oldVersionName = None
        for v in versions:
            if s.name in v.names:
                version = v.minorVersion
        if s.name in aliases.keys():
            for alias in aliases[s.name]:
                oldVersionName = alias

        if s.name in structures:
            out += AddGuardHeader(s)
            out += "        if (structure->sType == " + s.sTypeName
            has_version = version is not None
            has_extNameStr = extNameStr is not None or s.name in aliases.keys()

            if has_version or has_extNameStr:
                out += " && \n           ("
                if has_extNameStr:
                    if extType == "device":
                        out += "gpu.CheckPhysicalDeviceExtensionIncluded(" + \
                            extNameStr + ")"
                    elif extType == "instance":
                        out += "inst.CheckExtensionEnabled(" + extNameStr + ")"
                    if has_version and extType is not None:
                        out += " ||\n            "
                if has_version:
                    out += "version.minor >= " + str(version)
                out += ")"
            out += ") {\n"
            out += "            " + s.name + "* props = " + \
                "("+s.name+"*)structure;\n"

            out += "            Dump" + s.name + "(p, "
            if s.name in aliases.keys() and version is not None:
                out += "version.minor >= " + version + " ?\"" + \
                    s.name + "\":\"" + oldVersionName + "\""
            else:
                out += "\"" + s.name + "\""
            out += ", *props);\n"
            out += "            p.AddNewline();\n"
            out += "        }\n"
            out += AddGuardFooter(s)
    out += "        place = structure->pNext;\n"
    out += "    }\n"
    out += "}\n"
    return out

def PrintStructComparisonForwardDecl(structure):
    out = ''
    out += "bool operator==(const " + structure.name + \
        " & a, const " + structure.name + " b);\n"
    return out


def PrintStructComparison(structure):
    out = ''
    out += "bool operator==(const " + structure.name + \
        " & a, const " + structure.name + " b) {\n"
    out += "    return "
    is_first = True
    for m in structure.members:
        if m.name not in ['sType', 'pNext']:
            if not is_first:
                out += "\n        && "
            else:
                is_first = False
            out += "a." + m.name + " == b." + m.name
    out += ";\n"
    out += "}\n"
    return out


class VulkanEnum:
    class Option:

        def __init__(self, name, value, bitpos, comment):
            self.name = name
            self.comment = comment

            if value == 0 or value is None:
                value = 1 << int(bitpos)

            self.value = value

        def values(self):
            return {
                'optName': self.name,
                'optValue': self.value,
                'optComment': self.comment,
            }

    def __init__(self, rootNode):
        self.name = rootNode.get('name')
        self.type = rootNode.get('type')
        self.options = []

        for child in rootNode:
            childName = child.get('name')
            childValue = child.get('value')
            childBitpos = child.get('bitpos')
            childComment = child.get('comment')
            childExtends = child.get('extends')
            childOffset = child.get('offset')
            childExtNum = child.get('extnumber')
            support = child.get('supported')
            if(support == "disabled"):
                continue

            if childName is None:
                continue
            if (childValue is None and childBitpos is None and childOffset is None):
                continue

            if childExtends is not None and childExtNum is not None and childOffset is not None:
                enumNegative = False
                extNum = int(childExtNum)
                extOffset = int(childOffset)
                extBase = 1000000000
                extBlockSize = 1000
                childValue = extBase + (extNum - 1) * extBlockSize + extOffset
                if ('dir' in child.keys()):
                    childValue = -childValue
            duplicate = False
            for o in self.options:
                if o.values()['optName'] == childName:
                    duplicate = True
            if duplicate:
                continue

            self.options.append(VulkanEnum.Option(
                childName, childValue, childBitpos, childComment))


class VulkanBitmask:

    def __init__(self, rootNode):
        self.name = rootNode.get('name')
        self.type = rootNode.get('type')

        # Read each value that the enum contains
        self.options = []
        for child in rootNode:
            childName = child.get('name')
            childValue = child.get('value')
            childBitpos = child.get('bitpos')
            childComment = child.get('comment')
            support = child.get('supported')
            if childName is None or (childValue is None and childBitpos is None):
                continue
            if(support == "disabled"):
                continue

            duplicate = False
            for option in self.options:
                if option.name == childName:
                    duplicate = True
            if duplicate:
                continue

            self.options.append(VulkanEnum.Option(
                childName, childValue, childBitpos, childComment))


class VulkanFlags:

    def __init__(self, rootNode):
        self.name = rootNode.get('name')
        self.type = rootNode.get('type')
        self.enum = rootNode.get('requires')


class VulkanVariable:
    def __init__(self, rootNode, constants, parentName):
        self.name = rootNode.find('name').text
        # Typename, dereferenced and converted to a useable C++ token
        self.typeID = rootNode.find('type').text
        self.baseType = self.typeID
        self.childType = None
        self.arrayLength = None

        self.text = ''
        for node in rootNode.itertext():
            comment = rootNode.find('comment')
            if comment is not None and comment.text == node:
                continue
            self.text += node

        typeMatch = re.search('.+?(?=' + self.name + ')', self.text)
        self.type = typeMatch.string[typeMatch.start():typeMatch.end()]
        self.type = ' '.join(self.type.split())
        bracketMatch = re.search('(?<=\\[)[a-zA-Z0-9_]+(?=\\])', self.text)
        if bracketMatch is not None:
            matchText = bracketMatch.string[bracketMatch.start(
            ):bracketMatch.end()]
            self.childType = self.type
            self.type += '[' + matchText + ']'
            if matchText in constants:
                self.arrayLength = constants[matchText]
            else:
                self.arrayLength = matchText

        self.lengthMember = False
        lengthString = rootNode.get('len')
        lengths = []
        if lengthString is not None:
            lengths = re.split(',', lengthString)
            lengths = list(filter(('null-terminated').__ne__, lengths))
        assert(len(lengths) <= 1)
        if self.arrayLength is None and len(lengths) > 0:
            self.childType = '*'.join(self.type.split('*')[0:-1])
            self.arrayLength = lengths[0]
            self.lengthMember = True
        if self.arrayLength is not None and self.arrayLength.startswith('latexmath'):
            code = self.arrayLength[10:len(self.arrayLength)]
            code = re.sub('\\[', '', code)
            code = re.sub('\\]', '', code)
            code = re.sub('\\\\(lceil|rceil)', '', code)
            code = re.sub('{|}', '', code)
            code = re.sub('\\\\mathit', '', code)
            code = re.sub('\\\\over', '/', code)
            code = re.sub('\\\\textrm', '', code)
            self.arrayLength = code

        # Dereference if necessary and handle members of variables
        if self.arrayLength is not None:
            self.arrayLength = re.sub('::', '->', self.arrayLength)
            sections = self.arrayLength.split('->')
            if sections[-1][0] == 'p' and sections[0][1].isupper():
                self.arrayLength = '*' + self.arrayLength


class VulkanStructure:
    def __init__(self, name, rootNode, constants, extTypes):
        self.name = name
        self.members = []
        self.guard = None
        self.sTypeName = None
        self.extendsStruct = rootNode.get('structextends')

        for node in rootNode.findall('member'):
            if(node.get('values') is not None):
                self.sTypeName = node.get('values')
            self.members.append(VulkanVariable(
                node, constants, self.name))

        for k, e in extTypes.items():
            if k == self.name:
                if e.guard is not None:
                    self.guard = e.guard


class VulkanExtension:
    def __init__(self, rootNode):
        self.name = rootNode.get('name')
        self.number = int(rootNode.get('number'))
        self.type = rootNode.get('type')
        self.dependency = rootNode.get('requires')
        self.guard = GetFeatureProtect(rootNode)
        self.supported = rootNode.get('supported')
        self.extNameStr = None
        self.vktypes = []
        self.vkfuncs = []
        self.constants = OrderedDict()
        self.enumValues = OrderedDict()
        self.version = 0
        self.node = rootNode

        promotedto = rootNode.get('promotedto')
        if promotedto != None:
            # get last char of VK_VERSION_1_1 or VK_VERSION_1_2
            minorVersion = promotedto[-1:]
            if minorVersion.isdigit():
                self.version = minorVersion

        for req in rootNode.findall('require'):
            for ty in req.findall('type'):
                self.vktypes.append(ty.get('name'))

            for func in req.findall('command'):
                self.vkfuncs.append(func.get('name'))

            for enum in req.findall('enum'):
                base = enum.get('extends')
                name = enum.get('name')
                value = enum.get('value')
                bitpos = enum.get('bitpos')
                offset = enum.get('offset')
                # gets the VK_XXX_EXTENSION_NAME string
                if value == "\"" + self.name + "\"":
                    self.extNameStr = name

                if value is None and bitpos is not None:
                    value = 1 << int(bitpos)

                if offset is not None:
                    offset = int(offset)
                if base is not None and offset is not None:
                    enumValue = 1000000000 + 1000*(self.number - 1) + offset
                    if enum.get('dir') == '-':
                        enumValue = -enumValue
                    self.enumValues[base] = (name, enumValue)
                else:
                    self.constants[name] = value


class VulkanVersion:
    def __init__(self, rootNode):
        self.name = rootNode.get('name')
        version_str = rootNode.get('number').split('.')
        self.majorVersion = version_str[0]
        self.minorVersion = version_str[1]
        self.names = set()

        for req in rootNode.findall('require'):
            for ty in req.findall('type'):
                self.names.add(ty.get('name'))
            for func in req.findall('command'):
                self.names.add(func.get('name'))
            for enum in req.findall('enum'):
                self.names.add(enum.get('name'))
        self.names = sorted(self.names)
