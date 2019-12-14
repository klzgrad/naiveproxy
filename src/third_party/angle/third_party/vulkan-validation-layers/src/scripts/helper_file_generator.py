#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2019 The Khronos Group Inc.
# Copyright (c) 2015-2019 Valve Corporation
# Copyright (c) 2015-2019 LunarG, Inc.
# Copyright (c) 2015-2019 Google Inc.
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
# Author: Mark Lobodzinski <mark@lunarg.com>
# Author: Tobin Ehlis <tobine@google.com>
# Author: John Zulauf <jzulauf@lunarg.com>

import os,re,sys
import xml.etree.ElementTree as etree
from generator import *
from collections import namedtuple
from common_codegen import *

#
# HelperFileOutputGeneratorOptions - subclass of GeneratorOptions.
class HelperFileOutputGeneratorOptions(GeneratorOptions):
    def __init__(self,
                 conventions = None,
                 filename = None,
                 directory = '.',
                 apiname = None,
                 profile = None,
                 versions = '.*',
                 emitversions = '.*',
                 defaultExtensions = None,
                 addExtensions = None,
                 removeExtensions = None,
                 emitExtensions = None,
                 sortProcedure = regSortFeatures,
                 prefixText = "",
                 genFuncPointers = True,
                 protectFile = True,
                 protectFeature = True,
                 apicall = '',
                 apientry = '',
                 apientryp = '',
                 alignFuncParam = 0,
                 library_name = '',
                 expandEnumerants = True,
                 helper_file_type = ''):
        GeneratorOptions.__init__(self, conventions, filename, directory, apiname, profile,
                                  versions, emitversions, defaultExtensions,
                                  addExtensions, removeExtensions, emitExtensions, sortProcedure)
        self.prefixText       = prefixText
        self.genFuncPointers  = genFuncPointers
        self.protectFile      = protectFile
        self.protectFeature   = protectFeature
        self.apicall          = apicall
        self.apientry         = apientry
        self.apientryp        = apientryp
        self.alignFuncParam   = alignFuncParam
        self.library_name     = library_name
        self.helper_file_type = helper_file_type
#
# HelperFileOutputGenerator - subclass of OutputGenerator. Outputs Vulkan helper files
class HelperFileOutputGenerator(OutputGenerator):
    """Generate helper file based on XML element attributes"""
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        # Internal state - accumulators for different inner block text
        self.enum_output = ''                             # string built up of enum string routines
        # Internal state - accumulators for different inner block text
        self.structNames = []                             # List of Vulkan struct typenames
        self.structTypes = dict()                         # Map of Vulkan struct typename to required VkStructureType
        self.structMembers = []                           # List of StructMemberData records for all Vulkan structs
        self.object_types = []                            # List of all handle types
        self.object_type_aliases = []                     # Aliases to handles types (for handles that were extensions)
        self.debug_report_object_types = []               # Handy copy of debug_report_object_type enum data
        self.core_object_types = []                       # Handy copy of core_object_type enum data
        self.device_extension_info = dict()               # Dict of device extension name defines and ifdef values
        self.instance_extension_info = dict()             # Dict of instance extension name defines and ifdef values
        self.structextends_list = []                      # List of structs which extend another struct via pNext


        # Named tuples to store struct and command data
        self.StructType = namedtuple('StructType', ['name', 'value'])
        self.CommandParam = namedtuple('CommandParam', ['type', 'name', 'ispointer', 'isstaticarray', 'isconst', 'iscount', 'len', 'extstructs', 'cdecl'])
        self.StructMemberData = namedtuple('StructMemberData', ['name', 'members', 'ifdef_protect'])

        self.custom_construct_params = {
            # safe_VkGraphicsPipelineCreateInfo needs to know if subpass has color and\or depth\stencil attachments to use its pointers
            'VkGraphicsPipelineCreateInfo' :
                ', const bool uses_color_attachment, const bool uses_depthstencil_attachment',
            # safe_VkPipelineViewportStateCreateInfo needs to know if viewport and scissor is dynamic to use its pointers
            'VkPipelineViewportStateCreateInfo' :
                ', const bool is_dynamic_viewports, const bool is_dynamic_scissors',
        }
    #
    # Called once at the beginning of each run
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        # Initialize members that require the tree
        self.handle_types = GetHandleTypes(self.registry.tree)
        # User-supplied prefix text, if any (list of strings)
        self.helper_file_type = genOpts.helper_file_type
        self.library_name = genOpts.library_name
        # File Comment
        file_comment = '// *** THIS FILE IS GENERATED - DO NOT EDIT ***\n'
        file_comment += '// See helper_file_generator.py for modifications\n'
        write(file_comment, file=self.outFile)
        # Copyright Notice
        copyright = ''
        copyright += '\n'
        copyright += '/***************************************************************************\n'
        copyright += ' *\n'
        copyright += ' * Copyright (c) 2015-2019 The Khronos Group Inc.\n'
        copyright += ' * Copyright (c) 2015-2019 Valve Corporation\n'
        copyright += ' * Copyright (c) 2015-2019 LunarG, Inc.\n'
        copyright += ' * Copyright (c) 2015-2019 Google Inc.\n'
        copyright += ' *\n'
        copyright += ' * Licensed under the Apache License, Version 2.0 (the "License");\n'
        copyright += ' * you may not use this file except in compliance with the License.\n'
        copyright += ' * You may obtain a copy of the License at\n'
        copyright += ' *\n'
        copyright += ' *     http://www.apache.org/licenses/LICENSE-2.0\n'
        copyright += ' *\n'
        copyright += ' * Unless required by applicable law or agreed to in writing, software\n'
        copyright += ' * distributed under the License is distributed on an "AS IS" BASIS,\n'
        copyright += ' * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n'
        copyright += ' * See the License for the specific language governing permissions and\n'
        copyright += ' * limitations under the License.\n'
        copyright += ' *\n'
        copyright += ' * Author: Mark Lobodzinski <mark@lunarg.com>\n'
        copyright += ' * Author: Courtney Goeltzenleuchter <courtneygo@google.com>\n'
        copyright += ' * Author: Tobin Ehlis <tobine@google.com>\n'
        copyright += ' * Author: Chris Forbes <chrisforbes@google.com>\n'
        copyright += ' * Author: John Zulauf<jzulauf@lunarg.com>\n'
        copyright += ' *\n'
        copyright += ' ****************************************************************************/\n'
        write(copyright, file=self.outFile)
    #
    # Write generated file content to output file
    def endFile(self):
        dest_file = ''
        dest_file += self.OutputDestFile()
        # Remove blank lines at EOF
        if dest_file.endswith('\n'):
            dest_file = dest_file[:-1]
        write(dest_file, file=self.outFile);
        # Finish processing in superclass
        OutputGenerator.endFile(self)
    #
    # Override parent class to be notified of the beginning of an extension
    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        self.featureExtraProtect = GetFeatureProtect(interface)

        if self.featureName == 'VK_VERSION_1_0' or self.featureName == 'VK_VERSION_1_1':
            return
        name = self.featureName
        nameElem = interface[0][1]
        name_define = nameElem.get('name')
        if 'EXTENSION_NAME' not in name_define:
            print("Error in vk.xml file -- extension name is not available")
        requires = interface.get('requires')
        if requires is not None:
            required_extensions = requires.split(',')
        else:
            required_extensions = list()
        info = { 'define': name_define, 'ifdef':self.featureExtraProtect, 'reqs':required_extensions }
        if interface.get('type') == 'instance':
            self.instance_extension_info[name] = info
        else:
            self.device_extension_info[name] = info

    #
    # Override parent class to be notified of the end of an extension
    def endFeature(self):
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    # Grab group (e.g. C "enum" type) info to output for enum-string conversion helper
    def genGroup(self, groupinfo, groupName, alias):
        OutputGenerator.genGroup(self, groupinfo, groupName, alias)
        groupElem = groupinfo.elem
        # For enum_string_header
        if self.helper_file_type == 'enum_string_header':
            value_set = set()
            for elem in groupElem.findall('enum'):
                if elem.get('supported') != 'disabled' and elem.get('alias') is None:
                    value_set.add(elem.get('name'))
            if value_set != set():
                self.enum_output += self.GenerateEnumStringConversion(groupName, value_set)
        elif self.helper_file_type == 'object_types_header':
            if groupName == 'VkDebugReportObjectTypeEXT':
                for elem in groupElem.findall('enum'):
                    if elem.get('supported') != 'disabled':
                        if elem.get('alias') is None: # TODO: Strangely the "alias" fn parameter does not work
                            item_name = elem.get('name')
                            if self.debug_report_object_types.count(item_name) == 0: # TODO: Strangely there are duplicates
                                self.debug_report_object_types.append(item_name)
            elif groupName == 'VkObjectType':
                for elem in groupElem.findall('enum'):
                    if elem.get('supported') != 'disabled':
                        if elem.get('alias') is None: # TODO: Strangely the "alias" fn parameter does not work
                            item_name = elem.get('name')
                            self.core_object_types.append(item_name)

    #
    # Called for each type -- if the type is a struct/union, grab the metadata
    def genType(self, typeinfo, name, alias):
        OutputGenerator.genType(self, typeinfo, name, alias)
        typeElem = typeinfo.elem
        # If the type is a struct type, traverse the imbedded <member> tags generating a structure.
        # Otherwise, emit the tag text.
        category = typeElem.get('category')
        if category == 'handle':
            if alias:
                self.object_type_aliases.append((name,alias))
            else:
                self.object_types.append(name)
        elif (category == 'struct' or category == 'union'):
            self.structNames.append(name)
            self.genStruct(typeinfo, name, alias)
    #
    # Check if the parameter passed in is a pointer
    def paramIsPointer(self, param):
        ispointer = False
        for elem in param:
            if elem.tag == 'type' and elem.tail is not None and '*' in elem.tail:
                ispointer = True
        return ispointer
    #
    # Check if the parameter passed in is a static array
    def paramIsStaticArray(self, param):
        isstaticarray = 0
        paramname = param.find('name')
        if (paramname.tail is not None) and ('[' in paramname.tail):
            isstaticarray = paramname.tail.count('[')
        return isstaticarray
    #
    # Retrieve the type and name for a parameter
    def getTypeNameTuple(self, param):
        type = ''
        name = ''
        for elem in param:
            if elem.tag == 'type':
                type = noneStr(elem.text)
            elif elem.tag == 'name':
                name = noneStr(elem.text)
        return (type, name)
    # Extract length values from latexmath.  Currently an inflexible solution that looks for specific
    # patterns that are found in vk.xml.  Will need to be updated when new patterns are introduced.
    def parseLateXMath(self, source):
        name = 'ERROR'
        decoratedName = 'ERROR'
        if 'mathit' in source:
            # Matches expressions similar to 'latexmath:[\lceil{\mathit{rasterizationSamples} \over 32}\rceil]'
            match = re.match(r'latexmath\s*\:\s*\[\s*\\l(\w+)\s*\{\s*\\mathit\s*\{\s*(\w+)\s*\}\s*\\over\s*(\d+)\s*\}\s*\\r(\w+)\s*\]', source)
            if not match or match.group(1) != match.group(4):
                raise 'Unrecognized latexmath expression'
            name = match.group(2)
            # Need to add 1 for ceiling function; otherwise, the allocated packet
            # size will be less than needed during capture for some title which use
            # this in VkPipelineMultisampleStateCreateInfo. based on ceiling function
            # definition,it is '{0}%{1}?{0}/{1} + 1:{0}/{1}'.format(*match.group(2, 3)),
            # its value <= '{}/{} + 1'.
            if match.group(1) == 'ceil':
                decoratedName = '{}/{} + 1'.format(*match.group(2, 3))
            else:
                decoratedName = '{}/{}'.format(*match.group(2, 3))
        else:
            # Matches expressions similar to 'latexmath : [dataSize \over 4]'
            match = re.match(r'latexmath\s*\:\s*\[\s*(\\textrm\{)?(\w+)\}?\s*\\over\s*(\d+)\s*\]', source)
            name = match.group(2)
            decoratedName = '{}/{}'.format(*match.group(2, 3))
        return name, decoratedName
    #
    # Retrieve the value of the len tag
    def getLen(self, param):
        result = None
        len = param.attrib.get('len')
        if len and len != 'null-terminated':
            # For string arrays, 'len' can look like 'count,null-terminated', indicating that we
            # have a null terminated array of strings.  We strip the null-terminated from the
            # 'len' field and only return the parameter specifying the string count
            if 'null-terminated' in len:
                result = len.split(',')[0]
            else:
                result = len
            if 'latexmath' in len:
                param_type, param_name = self.getTypeNameTuple(param)
                len_name, result = self.parseLateXMath(len)
            # Spec has now notation for len attributes, using :: instead of platform specific pointer symbol
            result = str(result).replace('::', '->')
        return result
    #
    # Check if a structure is or contains a dispatchable (dispatchable = True) or
    # non-dispatchable (dispatchable = False) handle
    def TypeContainsObjectHandle(self, handle_type, dispatchable):
        if dispatchable:
            type_check = self.handle_types.IsDispatchable
        else:
            type_check = self.handle_types.IsNonDispatchable
        if type_check(handle_type):
            return True
        # if handle_type is a struct, search its members
        if handle_type in self.structNames:
            member_index = next((i for i, v in enumerate(self.structMembers) if v[0] == handle_type), None)
            if member_index is not None:
                for item in self.structMembers[member_index].members:
                    if type_check(item.type):
                        return True
        return False
    #
    # Generate local ready-access data describing Vulkan structures and unions from the XML metadata
    def genStruct(self, typeinfo, typeName, alias):
        OutputGenerator.genStruct(self, typeinfo, typeName, alias)
        members = typeinfo.elem.findall('.//member')
        # Iterate over members once to get length parameters for arrays
        lens = set()
        for member in members:
            len = self.getLen(member)
            if len:
                lens.add(len)
        # Generate member info
        membersInfo = []
        for member in members:
            # Get the member's type and name
            info = self.getTypeNameTuple(member)
            type = info[0]
            name = info[1]
            cdecl = self.makeCParamDecl(member, 1)
            # Process VkStructureType
            if type == 'VkStructureType':
                # Extract the required struct type value from the comments
                # embedded in the original text defining the 'typeinfo' element
                rawXml = etree.tostring(typeinfo.elem).decode('ascii')
                result = re.search(r'VK_STRUCTURE_TYPE_\w+', rawXml)
                if result:
                    value = result.group(0)
                    # Store the required type value
                    self.structTypes[typeName] = self.StructType(name=name, value=value)
            # Store pointer/array/string info
            isstaticarray = self.paramIsStaticArray(member)
            structextends = False
            membersInfo.append(self.CommandParam(type=type,
                                                 name=name,
                                                 ispointer=self.paramIsPointer(member),
                                                 isstaticarray=isstaticarray,
                                                 isconst=True if 'const' in cdecl else False,
                                                 iscount=True if name in lens else False,
                                                 len=self.getLen(member),
                                                 extstructs=self.registry.validextensionstructs[typeName] if name == 'pNext' else None,
                                                 cdecl=cdecl))
        # If this struct extends another, keep its name in list for further processing
        if typeinfo.elem.attrib.get('structextends') is not None:
            self.structextends_list.append(typeName)
        self.structMembers.append(self.StructMemberData(name=typeName, members=membersInfo, ifdef_protect=self.featureExtraProtect))
    #
    # Enum_string_header: Create a routine to convert an enumerated value into a string
    def GenerateEnumStringConversion(self, groupName, value_list):
        outstring = '\n'
        if self.featureExtraProtect is not None:
            outstring += '\n#ifdef %s\n\n' % self.featureExtraProtect
        outstring += 'static inline const char* string_%s(%s input_value)\n' % (groupName, groupName)
        outstring += '{\n'
        outstring += '    switch ((%s)input_value)\n' % groupName
        outstring += '    {\n'
        # Emit these in a repeatable order so file is generated with the same contents each time.
        # This helps compiler caching systems like ccache.
        for item in sorted(value_list):
            outstring += '        case %s:\n' % item
            outstring += '            return "%s";\n' % item
        outstring += '        default:\n'
        outstring += '            return "Unhandled %s";\n' % groupName
        outstring += '    }\n'
        outstring += '}\n'

        bitsIndex = groupName.find('Bits')
        if (bitsIndex != -1):
            outstring += '\n'
            flagsName = groupName[0:bitsIndex] + "s" +  groupName[bitsIndex+4:]
            outstring += 'static inline std::string string_%s(%s input_value)\n' % (flagsName, flagsName)
            outstring += '{\n'
            outstring += '    std::string ret;\n'
            outstring += '    int index = 0;\n'
            outstring += '    while(input_value) {\n'
            outstring += '        if (input_value & 1) {\n'
            outstring += '            if( !ret.empty()) ret.append("|");\n'
            outstring += '            ret.append(string_%s(static_cast<%s>(1 << index)));\n' % (groupName, groupName)
            outstring += '        }\n'
            outstring += '        ++index;\n'
            outstring += '        input_value >>= 1;\n'
            outstring += '    }\n'
            outstring += '    if( ret.empty()) ret.append(string_%s(static_cast<%s>(0)));\n' % (groupName, groupName)
            outstring += '    return ret;\n'
            outstring += '}\n'

        if self.featureExtraProtect is not None:
            outstring += '#endif // %s\n' % self.featureExtraProtect
        return outstring
    #
    # Tack on a helper which, given an index into a VkPhysicalDeviceFeatures structure, will print the corresponding feature name
    def DeIndexPhysDevFeatures(self):
        pdev_members = None
        for name, members, ifdef in self.structMembers:
            if name == 'VkPhysicalDeviceFeatures':
                pdev_members = members
                break
        deindex = '\n'
        deindex += 'static inline const char * GetPhysDevFeatureString(uint32_t index) {\n'
        deindex += '    const char * IndexToPhysDevFeatureString[] = {\n'
        for feature in pdev_members:
            deindex += '        "%s",\n' % feature.name
        deindex += '    };\n\n'
        deindex += '    return IndexToPhysDevFeatureString[index];\n'
        deindex += '}\n'
        return deindex
    #
    # Combine enum string helper header file preamble with body text and return
    def GenerateEnumStringHelperHeader(self):
            enum_string_helper_header = '\n'
            enum_string_helper_header += '#pragma once\n'
            enum_string_helper_header += '#ifdef _WIN32\n'
            enum_string_helper_header += '#pragma warning( disable : 4065 )\n'
            enum_string_helper_header += '#endif\n'
            enum_string_helper_header += '\n'
            enum_string_helper_header += '#include <string>\n'
            enum_string_helper_header += '#include <vulkan/vulkan.h>\n'
            enum_string_helper_header += '\n'
            enum_string_helper_header += self.enum_output
            enum_string_helper_header += self.DeIndexPhysDevFeatures()
            return enum_string_helper_header
    #
    # Helper function for declaring a counter variable only once
    def DeclareCounter(self, string_var, declare_flag):
        if declare_flag == False:
            string_var += '        uint32_t i = 0;\n'
            declare_flag = True
        return string_var, declare_flag
    #
    # Combine safe struct helper header file preamble with body text and return
    def GenerateSafeStructHelperHeader(self):
        safe_struct_helper_header = '\n'
        safe_struct_helper_header += '#pragma once\n'
        safe_struct_helper_header += '#include <vulkan/vulkan.h>\n'
        safe_struct_helper_header += '\n'
        safe_struct_helper_header += 'void *SafePnextCopy(const void *pNext);\n'
        safe_struct_helper_header += 'void FreePnextChain(const void *pNext);\n'
        safe_struct_helper_header += 'char *SafeStringCopy(const char *in_string);\n'
        safe_struct_helper_header += '\n'
        safe_struct_helper_header += self.GenerateSafeStructHeader()
        return safe_struct_helper_header
    #
    # safe_struct header: build function prototypes for header file
    def GenerateSafeStructHeader(self):
        safe_struct_header = ''
        for item in self.structMembers:
            if self.NeedSafeStruct(item) == True:
                safe_struct_header += '\n'
                if item.ifdef_protect is not None:
                    safe_struct_header += '#ifdef %s\n' % item.ifdef_protect
                safe_struct_header += 'struct safe_%s {\n' % (item.name)
                for member in item.members:
                    if member.type in self.structNames:
                        member_index = next((i for i, v in enumerate(self.structMembers) if v[0] == member.type), None)
                        if member_index is not None and self.NeedSafeStruct(self.structMembers[member_index]) == True:
                            if member.ispointer:
                                safe_struct_header += '    safe_%s* %s;\n' % (member.type, member.name)
                            else:
                                safe_struct_header += '    safe_%s %s;\n' % (member.type, member.name)
                            continue
                    if member.len is not None and (self.TypeContainsObjectHandle(member.type, True) or self.TypeContainsObjectHandle(member.type, False)):
                            safe_struct_header += '    %s* %s;\n' % (member.type, member.name)
                    else:
                        safe_struct_header += '%s;\n' % member.cdecl
                safe_struct_header += '    safe_%s(const %s* in_struct%s);\n' % (item.name, item.name, self.custom_construct_params.get(item.name, ''))
                safe_struct_header += '    safe_%s(const safe_%s& src);\n' % (item.name, item.name)
                safe_struct_header += '    safe_%s& operator=(const safe_%s& src);\n' % (item.name, item.name)
                safe_struct_header += '    safe_%s();\n' % item.name
                safe_struct_header += '    ~safe_%s();\n' % item.name
                safe_struct_header += '    void initialize(const %s* in_struct%s);\n' % (item.name, self.custom_construct_params.get(item.name, ''))
                safe_struct_header += '    void initialize(const safe_%s* src);\n' % (item.name)
                safe_struct_header += '    %s *ptr() { return reinterpret_cast<%s *>(this); }\n' % (item.name, item.name)
                safe_struct_header += '    %s const *ptr() const { return reinterpret_cast<%s const *>(this); }\n' % (item.name, item.name)
                safe_struct_header += '};\n'
                if item.ifdef_protect is not None:
                    safe_struct_header += '#endif // %s\n' % item.ifdef_protect
        return safe_struct_header
    #
    # Generate extension helper header file
    def GenerateExtensionHelperHeader(self):

        V_1_1_level_feature_set = [
            'VK_VERSION_1_1',
            ]

        V_1_0_instance_extensions_promoted_to_V_1_1_core = [
            'vk_khr_device_group_creation',
            'vk_khr_external_fence_capabilities',
            'vk_khr_external_memory_capabilities',
            'vk_khr_external_semaphore_capabilities',
            'vk_khr_get_physical_device_properties_2',
            ]

        V_1_0_device_extensions_promoted_to_V_1_1_core = [
            'vk_khr_16bit_storage',
            'vk_khr_bind_memory_2',
            'vk_khr_dedicated_allocation',
            'vk_khr_descriptor_update_template',
            'vk_khr_device_group',
            'vk_khr_external_fence',
            'vk_khr_external_memory',
            'vk_khr_external_semaphore',
            'vk_khr_get_memory_requirements_2',
            'vk_khr_maintenance1',
            'vk_khr_maintenance2',
            'vk_khr_maintenance3',
            'vk_khr_multiview',
            'vk_khr_relaxed_block_layout',
            'vk_khr_sampler_ycbcr_conversion',
            'vk_khr_shader_draw_parameters',
            'vk_khr_storage_buffer_storage_class',
            'vk_khr_variable_pointers',
            ]

        output = [
            '',
            '#ifndef VK_EXTENSION_HELPER_H_',
            '#define VK_EXTENSION_HELPER_H_',
            '#include <unordered_set>',
            '#include <string>',
            '#include <unordered_map>',
            '#include <utility>',
            '#include <set>',
            '#include <vector>',
            '',
            '#include <vulkan/vulkan.h>',
            '']

        def guarded(ifdef, value):
            if ifdef is not None:
                return '\n'.join([ '#ifdef %s' % ifdef, value, '#endif' ])
            else:
                return value

        for type in ['Instance', 'Device']:
            struct_type = '%sExtensions' % type
            if type == 'Instance':
                extension_dict = self.instance_extension_info
                promoted_ext_list = V_1_0_instance_extensions_promoted_to_V_1_1_core
                struct_decl = 'struct %s {' % struct_type
                instance_struct_type = struct_type
            else:
                extension_dict = self.device_extension_info
                promoted_ext_list = V_1_0_device_extensions_promoted_to_V_1_1_core
                struct_decl = 'struct %s : public %s {' % (struct_type, instance_struct_type)

            extension_items = sorted(extension_dict.items())

            field_name = { ext_name: re.sub('_extension_name', '', info['define'].lower()) for ext_name, info in extension_items }

            if type == 'Instance':
                instance_field_name = field_name
                instance_extension_dict = extension_dict
            else:
                # Get complete field name and extension data for both Instance and Device extensions
                field_name.update(instance_field_name)
                extension_dict = extension_dict.copy()  # Don't modify the self.<dict> we're pointing to
                extension_dict.update(instance_extension_dict)

            # Output the data member list
            struct  = [struct_decl]
            struct.extend([ '    bool vk_feature_version_1_1{false};'])
            struct.extend([ '    bool %s{false};' % field_name[ext_name] for ext_name, info in extension_items])

            # Construct the extension information map -- mapping name to data member (field), and required extensions
            # The map is contained within a static function member for portability reasons.
            info_type = '%sInfo' % type
            info_map_type = '%sMap' % info_type
            req_type = '%sReq' % type
            req_vec_type = '%sVec' % req_type
            struct.extend([
                '',
                '    struct %s {' % req_type,
                '        const bool %s::* enabled;' % struct_type,
                '        const char *name;',
                '    };',
                '    typedef std::vector<%s> %s;' % (req_type, req_vec_type),
                '    struct %s {' % info_type,
                '       %s(bool %s::* state_, const %s requires_): state(state_), requires(requires_) {}' % ( info_type, struct_type, req_vec_type),
                '       bool %s::* state;' % struct_type,
                '       %s requires;' % req_vec_type,
                '    };',
                '',
                '    typedef std::unordered_map<std::string,%s> %s;' % (info_type, info_map_type),
                '    static const %s &get_info(const char *name) {' %info_type,
                '        static const %s info_map = {' % info_map_type ])
            struct.extend([
                '            std::make_pair("VK_VERSION_1_1", %sInfo(&%sExtensions::vk_feature_version_1_1, {})),' % (type, type)])

            field_format = '&' + struct_type + '::%s'
            req_format = '{' + field_format+ ', %s}'
            req_indent = '\n                           '
            req_join = ',' + req_indent
            info_format = ('            std::make_pair(%s, ' + info_type + '(' + field_format + ', {%s})),')
            def format_info(ext_name, info):
                reqs = req_join.join([req_format % (field_name[req], extension_dict[req]['define']) for req in info['reqs']])
                return info_format % (info['define'], field_name[ext_name], '{%s}' % (req_indent + reqs) if reqs else '')

            struct.extend([guarded(info['ifdef'], format_info(ext_name, info)) for ext_name, info in extension_items])
            struct.extend([
                '        };',
                '',
                '        static const %s empty_info {nullptr, %s()};' % (info_type, req_vec_type),
                '        %s::const_iterator info = info_map.find(name);' % info_map_type,
                '        if ( info != info_map.cend()) {',
                '            return info->second;',
                '        }',
                '        return empty_info;',
                '    }',
                ''])

            if type == 'Instance':
                struct.extend([
                    '    uint32_t NormalizeApiVersion(uint32_t specified_version) {',
                    '        uint32_t api_version = (specified_version < VK_API_VERSION_1_1) ? VK_API_VERSION_1_0 : VK_API_VERSION_1_1;',
                    '        return api_version;',
                    '    }',
                    '',
                    '    uint32_t InitFromInstanceCreateInfo(uint32_t requested_api_version, const VkInstanceCreateInfo *pCreateInfo) {'])
            else:
                struct.extend([
                    '    %s() = default;' % struct_type,
                    '    %s(const %s& instance_ext) : %s(instance_ext) {}' % (struct_type, instance_struct_type, instance_struct_type),
                    '',
                    '    uint32_t InitFromDeviceCreateInfo(const %s *instance_extensions, uint32_t requested_api_version,' % instance_struct_type,
                    '                                      const VkDeviceCreateInfo *pCreateInfo) {',
                    '        // Initialize: this to defaults,  base class fields to input.',
                    '        assert(instance_extensions);',
                    '        *this = %s(*instance_extensions);' % struct_type,
                    '']),
            struct.extend([
                '',
                '        static const std::vector<const char *> V_1_1_promoted_%s_apis = {' % type.lower() ])
            struct.extend(['            %s_EXTENSION_NAME,' % ext_name.upper() for ext_name in promoted_ext_list])
            struct.extend(['            "VK_VERSION_1_1",'])
            struct.extend([
                '        };',
                '',
                '        // Initialize struct data, robust to invalid pCreateInfo',
                '        if (pCreateInfo->ppEnabledExtensionNames) {',
                '            for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {',
                '                if (!pCreateInfo->ppEnabledExtensionNames[i]) continue;',
                '                auto info = get_info(pCreateInfo->ppEnabledExtensionNames[i]);',
                '                if(info.state) this->*(info.state) = true;',
                '            }',
                '        }',
                '        uint32_t api_version = NormalizeApiVersion(requested_api_version);',
                '        if (api_version >= VK_API_VERSION_1_1) {',
                '            for (auto promoted_ext : V_1_1_promoted_%s_apis) {' % type.lower(),
                '                auto info = get_info(promoted_ext);',
                '                assert(info.state);',
                '                if (info.state) this->*(info.state) = true;',
                '            }',
                '        }',
                '        return api_version;',
                '    }',
                '};'])

            # Output reference lists of instance/device extension names
            struct.extend(['', 'static const std::set<std::string> k%sExtensionNames = {' % type])
            struct.extend([guarded(info['ifdef'], '    %s,' % info['define']) for ext_name, info in extension_items])
            struct.extend(['};', ''])
            output.extend(struct)

        output.extend(['', '#endif // VK_EXTENSION_HELPER_H_'])
        return '\n'.join(output)
    #
    # Combine object types helper header file preamble with body text and return
    def GenerateObjectTypesHelperHeader(self):
        object_types_helper_header = '\n'
        object_types_helper_header += '#pragma once\n'
        object_types_helper_header += '\n'
        object_types_helper_header += self.GenerateObjectTypesHeader()
        return object_types_helper_header
    #
    # Object types header: create object enum type header file
    def GenerateObjectTypesHeader(self):
        object_types_header = '#include "cast_utils.h"\n'
        object_types_header += '\n'
        object_types_header += '// Object Type enum for validation layer internal object handling\n'
        object_types_header += 'typedef enum VulkanObjectType {\n'
        object_types_header += '    kVulkanObjectTypeUnknown = 0,\n'
        enum_num = 1
        type_list = [];
        enum_entry_map = {}
        non_dispatchable = {}
        dispatchable = {}
        object_type_info = {}

        # Output enum definition as each handle is processed, saving the names to use for the conversion routine
        for item in self.object_types:
            fixup_name = item[2:]
            enum_entry = 'kVulkanObjectType%s' % fixup_name
            enum_entry_map[item] = enum_entry
            object_types_header += '    ' + enum_entry
            object_types_header += ' = %d,\n' % enum_num
            enum_num += 1
            type_list.append(enum_entry)
            object_type_info[enum_entry] = { 'VkType': item }
            # We'll want lists of the dispatchable and non dispatchable handles below with access to the same info
            if self.handle_types.IsNonDispatchable(item):
                non_dispatchable[item] = enum_entry
            else:
                dispatchable[item] = enum_entry

        object_types_header += '    kVulkanObjectTypeMax = %d,\n' % enum_num
        object_types_header += '    // Aliases for backwards compatibilty of "promoted" types\n'
        for (name, alias) in self.object_type_aliases:
            fixup_name = name[2:]
            object_types_header += '    kVulkanObjectType{} = {},\n'.format(fixup_name, enum_entry_map[alias])
        object_types_header += '} VulkanObjectType;\n\n'

        # Output name string helper
        object_types_header += '// Array of object name strings for OBJECT_TYPE enum conversion\n'
        object_types_header += 'static const char * const object_string[kVulkanObjectTypeMax] = {\n'
        object_types_header += '    "VkNonDispatchableHandle",\n'
        for item in self.object_types:
            object_types_header += '    "%s",\n' % item
        object_types_header += '};\n'

        # Helpers to create unified dict key from k<Name>, VK_OBJECT_TYPE_<Name>, and VK_DEBUG_REPORT_OBJECT_TYPE_<Name>
        def dro_to_key(raw_key): return re.search('^VK_DEBUG_REPORT_OBJECT_TYPE_(.*)_EXT$', raw_key).group(1).lower().replace("_","")
        def vko_to_key(raw_key): return re.search('^VK_OBJECT_TYPE_(.*)', raw_key).group(1).lower().replace("_","")
        def kenum_to_key(raw_key): return re.search('^kVulkanObjectType(.*)', raw_key).group(1).lower()

        dro_dict = {dro_to_key(dro) : dro for dro in self.debug_report_object_types}
        vko_dict = {vko_to_key(vko) : vko for vko in self.core_object_types}

        # Output a conversion routine from the layer object definitions to the debug report definitions
        object_types_header += '\n'
        object_types_header += '// Helper array to get Vulkan VK_EXT_debug_report object type enum from the internal layers version\n'
        object_types_header += 'const VkDebugReportObjectTypeEXT get_debug_report_enum[] = {\n'
        object_types_header += '    VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT, // kVulkanObjectTypeUnknown\n' # no unknown handle, so this must be here explicitly

        for object_type in type_list:
            # VK_DEBUG_REPORT is not updated anymore; there might be missing object types
            kenum_type = dro_dict.get(kenum_to_key(object_type), 'VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT')
            object_types_header += '    %s,   // %s\n' % (kenum_type, object_type)
            object_type_info[object_type]['DbgType'] = kenum_type
        object_types_header += '};\n'

        # Output a conversion routine from the layer object definitions to the core object type definitions
        # This will intentionally *fail* for unmatched types as the VK_OBJECT_TYPE list should match the kVulkanObjectType list
        object_types_header += '\n'
        object_types_header += '// Helper array to get Official Vulkan VkObjectType enum from the internal layers version\n'
        object_types_header += 'const VkObjectType get_object_type_enum[] = {\n'
        object_types_header += '    VK_OBJECT_TYPE_UNKNOWN, // kVulkanObjectTypeUnknown\n' # no unknown handle, so must be here explicitly

        for object_type in type_list:
            kenum_type = vko_dict[kenum_to_key(object_type)]
            object_types_header += '    %s,   // %s\n' % (kenum_type, object_type)
            object_type_info[object_type]['VkoType'] = kenum_type
        object_types_header += '};\n'

        # Output a function converting from core object type definitions to the Vulkan object type enums
        object_types_header += '\n'
        object_types_header += '// Helper function to get internal layers object ids from the official Vulkan VkObjectType enum\n'
        object_types_header += 'static inline VulkanObjectType ConvertCoreObjectToVulkanObject(VkObjectType vulkan_object_type) {\n'
        object_types_header += '    switch (vulkan_object_type) {\n'

        for object_type in type_list:
            kenum_type = vko_dict[kenum_to_key(object_type)]
            object_types_header += '        case %s: return %s;\n' % (kenum_type, object_type)
        object_types_header += '        default: return kVulkanObjectTypeUnknown;\n'
        object_types_header += '    }\n'
        object_types_header += '};\n'

        # Create a functions to convert between VkDebugReportObjectTypeEXT and VkObjectType
        object_types_header +=     '\n'
        object_types_header +=     'static inline VkObjectType convertDebugReportObjectToCoreObject(VkDebugReportObjectTypeEXT debug_report_obj) {\n'
        object_types_header +=     '    switch (debug_report_obj) {\n'
        for dr_object_type in self.debug_report_object_types:
            object_types_header += '        case %s: return %s;\n' % (dr_object_type, vko_dict[dro_to_key(dr_object_type)])
        object_types_header +=     '        default: return VK_OBJECT_TYPE_UNKNOWN;\n'
        object_types_header +=     '    }\n'
        object_types_header +=     '}\n'

        object_types_header +=         '\n'
        object_types_header +=         'static inline VkDebugReportObjectTypeEXT convertCoreObjectToDebugReportObject(VkObjectType core_report_obj) {\n'
        object_types_header +=         '    switch (core_report_obj) {\n'
        for core_object_type in self.core_object_types:
            # VK_DEBUG_REPORT is not updated anymore; there might be missing object types
            dr_object_type = dro_dict.get(vko_to_key(core_object_type))
            if dr_object_type is not None:
                object_types_header += '        case %s: return %s;\n' % (core_object_type, dr_object_type)
        object_types_header +=         '        default: return VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT;\n'
        object_types_header +=         '    }\n'
        object_types_header +=         '}\n'

        #
        object_types_header += '\n'
        traits_format = Outdent('''
            template <> struct VkHandleInfo<{vk_type}> {{
                static const VulkanObjectType kVulkanObjectType = {obj_type};
                static const VkDebugReportObjectTypeEXT kDebugReportObjectType = {dbg_type};
                static const VkObjectType kVkObjectType = {vko_type};
                static const char* Typename() {{
                    return "{vk_type}";
                }}
            }};
            template <> struct VulkanObjectTypeInfo<{obj_type}> {{
                typedef {vk_type} Type;
            }};
            ''')

        object_types_header += Outdent('''
            // Traits objects from each type statically map from Vk<handleType> to the various enums
            template <typename VkType> struct VkHandleInfo {};
            template <VulkanObjectType id> struct VulkanObjectTypeInfo {};

            // The following line must match the vulkan_core.h condition guarding VK_DEFINE_NON_DISPATCHABLE_HANDLE
            #if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || \
                defined(_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
            #define TYPESAFE_NONDISPATCHABLE_HANDLES
            #else
            VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkNonDispatchableHandle)
            ''')  +'\n'
        object_types_header += traits_format.format(vk_type='VkNonDispatchableHandle', obj_type='kVulkanObjectTypeUnknown',
                                                  dbg_type='VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT',
                                                  vko_type='VK_OBJECT_TYPE_UNKNOWN') + '\n'
        object_types_header += '#endif //  VK_DEFINE_HANDLE logic duplication\n'

        for vk_type, object_type in sorted(dispatchable.items()):
            info = object_type_info[object_type]
            object_types_header += traits_format.format(vk_type=vk_type, obj_type=object_type, dbg_type=info['DbgType'],
                                                      vko_type=info['VkoType'])
        object_types_header += '#ifdef TYPESAFE_NONDISPATCHABLE_HANDLES\n'
        for vk_type, object_type in sorted(non_dispatchable.items()):
            info = object_type_info[object_type]
            object_types_header += traits_format.format(vk_type=vk_type, obj_type=object_type, dbg_type=info['DbgType'],
                                                      vko_type=info['VkoType'])
        object_types_header += '#endif // TYPESAFE_NONDISPATCHABLE_HANDLES\n'

        object_types_header += Outdent('''
            struct VulkanTypedHandle {
                uint64_t handle;
                VulkanObjectType type;
                template <typename Handle>
                VulkanTypedHandle(Handle handle_, VulkanObjectType type_) :
                    handle(CastToUint64(handle_)),
                    type(type_) {
            #ifdef TYPESAFE_NONDISPATCHABLE_HANDLES
                    // For 32 bit it's not always safe to check for traits <-> type
                    // as all non-dispatchable handles have the same type-id and thus traits,
                    // but on 64 bit we can validate the passed type matches the passed handle
                    assert(type == VkHandleInfo<Handle>::kVulkanObjectType);
            #endif // TYPESAFE_NONDISPATCHABLE_HANDLES
                }
                template <typename Handle>
                Handle Cast() const {
            #ifdef TYPESAFE_NONDISPATCHABLE_HANDLES
                    assert(type == VkHandleInfo<Handle>::kVulkanObjectType);
            #endif // TYPESAFE_NONDISPATCHABLE_HANDLES
                    return CastFromUint64<Handle>(handle);
                }
                VulkanTypedHandle() :
                    handle(VK_NULL_HANDLE),
                    type(kVulkanObjectTypeUnknown) {}
            }; ''')  +'\n'

        return object_types_header
    #
    # Generate pNext handling function
    def build_safe_struct_utility_funcs(self):
        # Construct Safe-struct helper functions

        string_copy_proc = '\n\n'
        string_copy_proc += 'char *SafeStringCopy(const char *in_string) {\n'
        string_copy_proc += '    if (nullptr == in_string) return nullptr;\n'
        string_copy_proc += '    char* dest = new char[std::strlen(in_string) + 1];\n'
        string_copy_proc += '    return std::strcpy(dest, in_string);\n'
        string_copy_proc += '}\n'

        build_pnext_proc = '\n'
        build_pnext_proc += 'void *SafePnextCopy(const void *pNext) {\n'
        build_pnext_proc += '    if (!pNext) return nullptr;\n'
        build_pnext_proc += '\n'
        build_pnext_proc += '    void *safe_pNext;\n'
        build_pnext_proc += '    const VkBaseOutStructure *header = reinterpret_cast<const VkBaseOutStructure *>(pNext);\n'
        build_pnext_proc += '\n'
        build_pnext_proc += '    switch (header->sType) {\n'
        # Add special-case code to copy beloved secret loader structs
        build_pnext_proc += '        // Special-case Loader Instance Struct passed to/from layer in pNext chain\n'
        build_pnext_proc += '        case VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO: {\n'
        build_pnext_proc += '            VkLayerInstanceCreateInfo *struct_copy = new VkLayerInstanceCreateInfo;\n'
        build_pnext_proc += '            // TODO: Uses original VkLayerInstanceLink* chain, which should be okay for our uses\n'
        build_pnext_proc += '            memcpy(struct_copy, pNext, sizeof(VkLayerInstanceCreateInfo));\n'
        build_pnext_proc += '            struct_copy->pNext = SafePnextCopy(header->pNext);\n'
        build_pnext_proc += '            safe_pNext = struct_copy;\n'
        build_pnext_proc += '            break;\n'
        build_pnext_proc += '        }\n'
        build_pnext_proc += '        // Special-case Loader Device Struct passed to/from layer in pNext chain\n'
        build_pnext_proc += '        case VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO: {\n'
        build_pnext_proc += '            VkLayerDeviceCreateInfo *struct_copy = new VkLayerDeviceCreateInfo;\n'
        build_pnext_proc += '            // TODO: Uses original VkLayerDeviceLink*, which should be okay for our uses\n'
        build_pnext_proc += '            memcpy(struct_copy, pNext, sizeof(VkLayerDeviceCreateInfo));\n'
        build_pnext_proc += '            struct_copy->pNext = SafePnextCopy(header->pNext);\n'
        build_pnext_proc += '            safe_pNext = struct_copy;\n'
        build_pnext_proc += '            break;\n'
        build_pnext_proc += '        }\n'

        free_pnext_proc = '\n'
        free_pnext_proc += 'void FreePnextChain(const void *pNext) {\n'
        free_pnext_proc += '    if (!pNext) return;\n'
        free_pnext_proc += '\n'
        free_pnext_proc += '    auto header = reinterpret_cast<const VkBaseOutStructure *>(pNext);\n'
        free_pnext_proc += '\n'
        free_pnext_proc += '    switch (header->sType) {\n'
        free_pnext_proc += '        // Special-case Loader Instance Struct passed to/from layer in pNext chain\n'
        free_pnext_proc += '        case VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO:\n'
        free_pnext_proc += '            FreePnextChain(header->pNext);\n'
        free_pnext_proc += '            delete reinterpret_cast<const VkLayerInstanceCreateInfo *>(pNext);\n'
        free_pnext_proc += '            break;\n'
        free_pnext_proc += '        // Special-case Loader Device Struct passed to/from layer in pNext chain\n'
        free_pnext_proc += '        case VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO:\n'
        free_pnext_proc += '            FreePnextChain(header->pNext);\n'
        free_pnext_proc += '            delete reinterpret_cast<const VkLayerDeviceCreateInfo *>(pNext);\n'
        free_pnext_proc += '            break;\n'

        chain_structs = tuple(s for s in self.structMembers if s.name in self.structextends_list)
        ifdefs = sorted({cs.ifdef_protect for cs in chain_structs}, key = lambda i : i if i is not None else '')
        for ifdef in ifdefs:
            if ifdef is not None:
                build_pnext_proc += '#ifdef %s\n' % ifdef
                free_pnext_proc += '#ifdef %s\n' % ifdef

            assorted_chain_structs = tuple(s for s in chain_structs if s.ifdef_protect == ifdef)
            for struct in assorted_chain_structs:
                build_pnext_proc += '        case %s:\n' % self.structTypes[struct.name].value
                build_pnext_proc += '            safe_pNext = new safe_%s(reinterpret_cast<const %s *>(pNext));\n' % (struct.name, struct.name)
                build_pnext_proc += '            break;\n'

                free_pnext_proc += '        case %s:\n' % self.structTypes[struct.name].value
                free_pnext_proc += '            delete reinterpret_cast<const safe_%s *>(header);\n' % struct.name
                free_pnext_proc += '            break;\n'

            if ifdef is not None:
                build_pnext_proc += '#endif // %s\n' % ifdef
                free_pnext_proc += '#endif // %s\n' % ifdef

        build_pnext_proc += '        default: // Encountered an unknown sType -- skip (do not copy) this entry in the chain\n'
        build_pnext_proc += '            safe_pNext = SafePnextCopy(header->pNext);\n'
        build_pnext_proc += '            break;\n'
        build_pnext_proc += '    }\n'
        build_pnext_proc += '\n'
        build_pnext_proc += '    return safe_pNext;\n'
        build_pnext_proc += '}\n'

        free_pnext_proc += '        default: // Encountered an unknown sType -- panic, there should be none such in safe chain\n'
        free_pnext_proc += '            assert(false);\n'
        free_pnext_proc += '            FreePnextChain(header->pNext);\n'
        free_pnext_proc += '            break;\n'
        free_pnext_proc += '    }\n'
        free_pnext_proc += '}\n'

        pnext_procs = string_copy_proc + build_pnext_proc + free_pnext_proc
        return pnext_procs
    #
    # Determine if a structure needs a safe_struct helper function
    # That is, it has an sType or one of its members is a pointer
    def NeedSafeStruct(self, structure):
        if 'VkBase' in structure.name:
            return False
        if 'sType' == structure.name:
            return True
        for member in structure.members:
            if member.ispointer == True:
                return True
        return False
    #
    # Combine safe struct helper source file preamble with body text and return
    def GenerateSafeStructHelperSource(self):
        safe_struct_helper_source = '\n'
        safe_struct_helper_source += '#include "vk_safe_struct.h"\n'
        safe_struct_helper_source += '\n'
        safe_struct_helper_source += '#include <string.h>\n'
        safe_struct_helper_source += '#include <cassert>\n'
        safe_struct_helper_source += '#include <cstring>\n'
        safe_struct_helper_source += '\n'
        safe_struct_helper_source += '#include <vulkan/vk_layer.h>\n'
        safe_struct_helper_source += '\n'
        safe_struct_helper_source += self.GenerateSafeStructSource()
        safe_struct_helper_source += self.build_safe_struct_utility_funcs()

        return safe_struct_helper_source
    #
    # safe_struct source -- create bodies of safe struct helper functions
    def GenerateSafeStructSource(self):
        safe_struct_body = []
        wsi_structs = ['VkXlibSurfaceCreateInfoKHR',
                       'VkXcbSurfaceCreateInfoKHR',
                       'VkWaylandSurfaceCreateInfoKHR',
                       'VkAndroidSurfaceCreateInfoKHR',
                       'VkWin32SurfaceCreateInfoKHR'
                       ]

        # For abstract types just want to save the pointer away
        # since we cannot make a copy.
        abstract_types = ['AHardwareBuffer',
                          'ANativeWindow',
                         ]
        for item in self.structMembers:
            if self.NeedSafeStruct(item) == False:
                continue
            if item.name in wsi_structs:
                continue
            if item.ifdef_protect is not None:
                safe_struct_body.append("#ifdef %s\n" % item.ifdef_protect)
            ss_name = "safe_%s" % item.name
            init_list = ''          # list of members in struct constructor initializer
            default_init_list = ''  # Default constructor just inits ptrs to nullptr in initializer
            init_func_txt = ''      # Txt for initialize() function that takes struct ptr and inits members
            construct_txt = ''      # Body of constuctor as well as body of initialize() func following init_func_txt
            destruct_txt = ''

            custom_construct_txt = {
                # VkWriteDescriptorSet is special case because pointers may be non-null but ignored
                'VkWriteDescriptorSet' :
                    '    switch (descriptorType) {\n'
                    '        case VK_DESCRIPTOR_TYPE_SAMPLER:\n'
                    '        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:\n'
                    '        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:\n'
                    '        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:\n'
                    '        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:\n'
                    '        if (descriptorCount && in_struct->pImageInfo) {\n'
                    '            pImageInfo = new VkDescriptorImageInfo[descriptorCount];\n'
                    '            for (uint32_t i = 0; i < descriptorCount; ++i) {\n'
                    '                pImageInfo[i] = in_struct->pImageInfo[i];\n'
                    '            }\n'
                    '        }\n'
                    '        break;\n'
                    '        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:\n'
                    '        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:\n'
                    '        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:\n'
                    '        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:\n'
                    '        if (descriptorCount && in_struct->pBufferInfo) {\n'
                    '            pBufferInfo = new VkDescriptorBufferInfo[descriptorCount];\n'
                    '            for (uint32_t i = 0; i < descriptorCount; ++i) {\n'
                    '                pBufferInfo[i] = in_struct->pBufferInfo[i];\n'
                    '            }\n'
                    '        }\n'
                    '        break;\n'
                    '        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:\n'
                    '        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:\n'
                    '        if (descriptorCount && in_struct->pTexelBufferView) {\n'
                    '            pTexelBufferView = new VkBufferView[descriptorCount];\n'
                    '            for (uint32_t i = 0; i < descriptorCount; ++i) {\n'
                    '                pTexelBufferView[i] = in_struct->pTexelBufferView[i];\n'
                    '            }\n'
                    '        }\n'
                    '        break;\n'
                    '        default:\n'
                    '        break;\n'
                    '    }\n',
                'VkShaderModuleCreateInfo' :
                    '    if (in_struct->pCode) {\n'
                    '        pCode = reinterpret_cast<uint32_t *>(new uint8_t[codeSize]);\n'
                    '        memcpy((void *)pCode, (void *)in_struct->pCode, codeSize);\n'
                    '    }\n',
                # VkGraphicsPipelineCreateInfo is special case because its pointers may be non-null but ignored
                'VkGraphicsPipelineCreateInfo' :
                    '    if (stageCount && in_struct->pStages) {\n'
                    '        pStages = new safe_VkPipelineShaderStageCreateInfo[stageCount];\n'
                    '        for (uint32_t i = 0; i < stageCount; ++i) {\n'
                    '            pStages[i].initialize(&in_struct->pStages[i]);\n'
                    '        }\n'
                    '    }\n'
                    '    if (in_struct->pVertexInputState)\n'
                    '        pVertexInputState = new safe_VkPipelineVertexInputStateCreateInfo(in_struct->pVertexInputState);\n'
                    '    else\n'
                    '        pVertexInputState = NULL;\n'
                    '    if (in_struct->pInputAssemblyState)\n'
                    '        pInputAssemblyState = new safe_VkPipelineInputAssemblyStateCreateInfo(in_struct->pInputAssemblyState);\n'
                    '    else\n'
                    '        pInputAssemblyState = NULL;\n'
                    '    bool has_tessellation_stage = false;\n'
                    '    if (stageCount && pStages)\n'
                    '        for (uint32_t i = 0; i < stageCount && !has_tessellation_stage; ++i)\n'
                    '            if (pStages[i].stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || pStages[i].stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)\n'
                    '                has_tessellation_stage = true;\n'
                    '    if (in_struct->pTessellationState && has_tessellation_stage)\n'
                    '        pTessellationState = new safe_VkPipelineTessellationStateCreateInfo(in_struct->pTessellationState);\n'
                    '    else\n'
                    '        pTessellationState = NULL; // original pTessellationState pointer ignored\n'
                    '    bool has_rasterization = in_struct->pRasterizationState ? !in_struct->pRasterizationState->rasterizerDiscardEnable : false;\n'
                    '    if (in_struct->pViewportState && has_rasterization) {\n'
                    '        bool is_dynamic_viewports = false;\n'
                    '        bool is_dynamic_scissors = false;\n'
                    '        if (in_struct->pDynamicState && in_struct->pDynamicState->pDynamicStates) {\n'
                    '            for (uint32_t i = 0; i < in_struct->pDynamicState->dynamicStateCount && !is_dynamic_viewports; ++i)\n'
                    '                if (in_struct->pDynamicState->pDynamicStates[i] == VK_DYNAMIC_STATE_VIEWPORT)\n'
                    '                    is_dynamic_viewports = true;\n'
                    '            for (uint32_t i = 0; i < in_struct->pDynamicState->dynamicStateCount && !is_dynamic_scissors; ++i)\n'
                    '                if (in_struct->pDynamicState->pDynamicStates[i] == VK_DYNAMIC_STATE_SCISSOR)\n'
                    '                    is_dynamic_scissors = true;\n'
                    '        }\n'
                    '        pViewportState = new safe_VkPipelineViewportStateCreateInfo(in_struct->pViewportState, is_dynamic_viewports, is_dynamic_scissors);\n'
                    '    } else\n'
                    '        pViewportState = NULL; // original pViewportState pointer ignored\n'
                    '    if (in_struct->pRasterizationState)\n'
                    '        pRasterizationState = new safe_VkPipelineRasterizationStateCreateInfo(in_struct->pRasterizationState);\n'
                    '    else\n'
                    '        pRasterizationState = NULL;\n'
                    '    if (in_struct->pMultisampleState && has_rasterization)\n'
                    '        pMultisampleState = new safe_VkPipelineMultisampleStateCreateInfo(in_struct->pMultisampleState);\n'
                    '    else\n'
                    '        pMultisampleState = NULL; // original pMultisampleState pointer ignored\n'
                    '    // needs a tracked subpass state uses_depthstencil_attachment\n'
                    '    if (in_struct->pDepthStencilState && has_rasterization && uses_depthstencil_attachment)\n'
                    '        pDepthStencilState = new safe_VkPipelineDepthStencilStateCreateInfo(in_struct->pDepthStencilState);\n'
                    '    else\n'
                    '        pDepthStencilState = NULL; // original pDepthStencilState pointer ignored\n'
                    '    // needs a tracked subpass state usesColorAttachment\n'
                    '    if (in_struct->pColorBlendState && has_rasterization && uses_color_attachment)\n'
                    '        pColorBlendState = new safe_VkPipelineColorBlendStateCreateInfo(in_struct->pColorBlendState);\n'
                    '    else\n'
                    '        pColorBlendState = NULL; // original pColorBlendState pointer ignored\n'
                    '    if (in_struct->pDynamicState)\n'
                    '        pDynamicState = new safe_VkPipelineDynamicStateCreateInfo(in_struct->pDynamicState);\n'
                    '    else\n'
                    '        pDynamicState = NULL;\n',
                 # VkPipelineViewportStateCreateInfo is special case because its pointers may be non-null but ignored
                'VkPipelineViewportStateCreateInfo' :
                    '    if (in_struct->pViewports && !is_dynamic_viewports) {\n'
                    '        pViewports = new VkViewport[in_struct->viewportCount];\n'
                    '        memcpy ((void *)pViewports, (void *)in_struct->pViewports, sizeof(VkViewport)*in_struct->viewportCount);\n'
                    '    }\n'
                    '    else\n'
                    '        pViewports = NULL;\n'
                    '    if (in_struct->pScissors && !is_dynamic_scissors) {\n'
                    '        pScissors = new VkRect2D[in_struct->scissorCount];\n'
                    '        memcpy ((void *)pScissors, (void *)in_struct->pScissors, sizeof(VkRect2D)*in_struct->scissorCount);\n'
                    '    }\n'
                    '    else\n'
                    '        pScissors = NULL;\n',
                # VkDescriptorSetLayoutBinding is special case because its pImmutableSamplers pointer may be non-null but ignored
                'VkDescriptorSetLayoutBinding' :
                    '    const bool sampler_type = in_struct->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER || in_struct->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;\n'
                    '    if (descriptorCount && in_struct->pImmutableSamplers && sampler_type) {\n'
                    '        pImmutableSamplers = new VkSampler[descriptorCount];\n'
                    '        for (uint32_t i = 0; i < descriptorCount; ++i) {\n'
                    '            pImmutableSamplers[i] = in_struct->pImmutableSamplers[i];\n'
                    '        }\n'
                    '    }\n',
            }

            custom_copy_txt = {
                # VkGraphicsPipelineCreateInfo is special case because it has custom construct parameters
                'VkGraphicsPipelineCreateInfo' :
                    '    pNext = SafePnextCopy(src.pNext);\n'
                    '    if (stageCount && src.pStages) {\n'
                    '        pStages = new safe_VkPipelineShaderStageCreateInfo[stageCount];\n'
                    '        for (uint32_t i = 0; i < stageCount; ++i) {\n'
                    '            pStages[i].initialize(&src.pStages[i]);\n'
                    '        }\n'
                    '    }\n'
                    '    if (src.pVertexInputState)\n'
                    '        pVertexInputState = new safe_VkPipelineVertexInputStateCreateInfo(*src.pVertexInputState);\n'
                    '    else\n'
                    '        pVertexInputState = NULL;\n'
                    '    if (src.pInputAssemblyState)\n'
                    '        pInputAssemblyState = new safe_VkPipelineInputAssemblyStateCreateInfo(*src.pInputAssemblyState);\n'
                    '    else\n'
                    '        pInputAssemblyState = NULL;\n'
                    '    bool has_tessellation_stage = false;\n'
                    '    if (stageCount && pStages)\n'
                    '        for (uint32_t i = 0; i < stageCount && !has_tessellation_stage; ++i)\n'
                    '            if (pStages[i].stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT || pStages[i].stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)\n'
                    '                has_tessellation_stage = true;\n'
                    '    if (src.pTessellationState && has_tessellation_stage)\n'
                    '        pTessellationState = new safe_VkPipelineTessellationStateCreateInfo(*src.pTessellationState);\n'
                    '    else\n'
                    '        pTessellationState = NULL; // original pTessellationState pointer ignored\n'
                    '    bool has_rasterization = src.pRasterizationState ? !src.pRasterizationState->rasterizerDiscardEnable : false;\n'
                    '    if (src.pViewportState && has_rasterization) {\n'
                    '        pViewportState = new safe_VkPipelineViewportStateCreateInfo(*src.pViewportState);\n'
                    '    } else\n'
                    '        pViewportState = NULL; // original pViewportState pointer ignored\n'
                    '    if (src.pRasterizationState)\n'
                    '        pRasterizationState = new safe_VkPipelineRasterizationStateCreateInfo(*src.pRasterizationState);\n'
                    '    else\n'
                    '        pRasterizationState = NULL;\n'
                    '    if (src.pMultisampleState && has_rasterization)\n'
                    '        pMultisampleState = new safe_VkPipelineMultisampleStateCreateInfo(*src.pMultisampleState);\n'
                    '    else\n'
                    '        pMultisampleState = NULL; // original pMultisampleState pointer ignored\n'
                    '    if (src.pDepthStencilState && has_rasterization)\n'
                    '        pDepthStencilState = new safe_VkPipelineDepthStencilStateCreateInfo(*src.pDepthStencilState);\n'
                    '    else\n'
                    '        pDepthStencilState = NULL; // original pDepthStencilState pointer ignored\n'
                    '    if (src.pColorBlendState && has_rasterization)\n'
                    '        pColorBlendState = new safe_VkPipelineColorBlendStateCreateInfo(*src.pColorBlendState);\n'
                    '    else\n'
                    '        pColorBlendState = NULL; // original pColorBlendState pointer ignored\n'
                    '    if (src.pDynamicState)\n'
                    '        pDynamicState = new safe_VkPipelineDynamicStateCreateInfo(*src.pDynamicState);\n'
                    '    else\n'
                    '        pDynamicState = NULL;\n',
                 # VkPipelineViewportStateCreateInfo is special case because it has custom construct parameters
                'VkPipelineViewportStateCreateInfo' :
                    '    pNext = SafePnextCopy(src.pNext);\n'
                    '    if (src.pViewports) {\n'
                    '        pViewports = new VkViewport[src.viewportCount];\n'
                    '        memcpy ((void *)pViewports, (void *)src.pViewports, sizeof(VkViewport)*src.viewportCount);\n'
                    '    }\n'
                    '    else\n'
                    '        pViewports = NULL;\n'
                    '    if (src.pScissors) {\n'
                    '        pScissors = new VkRect2D[src.scissorCount];\n'
                    '        memcpy ((void *)pScissors, (void *)src.pScissors, sizeof(VkRect2D)*src.scissorCount);\n'
                    '    }\n'
                    '    else\n'
                    '        pScissors = NULL;\n',
            }

            custom_destruct_txt = {'VkShaderModuleCreateInfo' :
                                   '    if (pCode)\n'
                                   '        delete[] reinterpret_cast<const uint8_t *>(pCode);\n' }
            copy_pnext = ''
            copy_strings = ''
            for member in item.members:
                m_type = member.type
                if member.name == 'pNext':
                    copy_pnext = '    pNext = SafePnextCopy(in_struct->pNext);\n'
                if member.type in self.structNames:
                    member_index = next((i for i, v in enumerate(self.structMembers) if v[0] == member.type), None)
                    if member_index is not None and self.NeedSafeStruct(self.structMembers[member_index]) == True:
                        m_type = 'safe_%s' % member.type
                if member.ispointer and 'safe_' not in m_type and self.TypeContainsObjectHandle(member.type, False) == False:
                    # Ptr types w/o a safe_struct, for non-null case need to allocate new ptr and copy data in
                    if m_type in ['void', 'char']:
                        if member.name != 'pNext':
                            if m_type == 'char':
                                # Create deep copies of strings
                                if member.len:
                                    copy_strings += '    char **tmp_%s = new char *[in_struct->%s];\n' % (member.name, member.len)
                                    copy_strings += '    for (uint32_t i = 0; i < %s; ++i) {\n' % member.len
                                    copy_strings += '        tmp_%s[i] = SafeStringCopy(in_struct->%s[i]);\n' % (member.name, member.name)
                                    copy_strings += '    }\n'
                                    copy_strings += '    %s = tmp_%s;\n' % (member.name, member.name)

                                    destruct_txt += '    if (%s) {\n' % member.name
                                    destruct_txt += '        for (uint32_t i = 0; i < %s; ++i) {\n' % member.len
                                    destruct_txt += '            delete [] %s[i];\n' % member.name
                                    destruct_txt += '        }\n'
                                    destruct_txt += '        delete [] %s;\n' % member.name
                                    destruct_txt += '    }\n'
                                else:
                                    copy_strings += '    %s = SafeStringCopy(in_struct->%s);\n' % (member.name, member.name)
                                    destruct_txt += '    if (%s) delete [] %s;\n' % (member.name, member.name)
                            else:
                                # For these exceptions just copy initial value over for now
                                init_list += '\n    %s(in_struct->%s),' % (member.name, member.name)
                                init_func_txt += '    %s = in_struct->%s;\n' % (member.name, member.name)
                        default_init_list += '\n    %s(nullptr),' % (member.name)
                    else:
                        default_init_list += '\n    %s(nullptr),' % (member.name)
                        init_list += '\n    %s(nullptr),' % (member.name)
                        if m_type in abstract_types:
                            construct_txt += '    %s = in_struct->%s;\n' % (member.name, member.name)
                        else:
                            init_func_txt += '    %s = nullptr;\n' % (member.name)
                            if not member.isstaticarray and (member.len is None or '/' in member.len):
                                construct_txt += '    if (in_struct->%s) {\n' % member.name
                                construct_txt += '        %s = new %s(*in_struct->%s);\n' % (member.name, m_type, member.name)
                                construct_txt += '    }\n'
                                destruct_txt += '    if (%s)\n' % member.name
                                destruct_txt += '        delete %s;\n' % member.name
                            else:
                                construct_txt += '    if (in_struct->%s) {\n' % member.name
                                construct_txt += '        %s = new %s[in_struct->%s];\n' % (member.name, m_type, member.len)
                                construct_txt += '        memcpy ((void *)%s, (void *)in_struct->%s, sizeof(%s)*in_struct->%s);\n' % (member.name, member.name, m_type, member.len)
                                construct_txt += '    }\n'
                                destruct_txt += '    if (%s)\n' % member.name
                                destruct_txt += '        delete[] %s;\n' % member.name
                elif member.isstaticarray or member.len is not None:
                    if member.len is None:
                        # Extract length of static array by grabbing val between []
                        static_array_size = re.match(r"[^[]*\[([^]]*)\]", member.cdecl)
                        construct_txt += '    for (uint32_t i = 0; i < %s; ++i) {\n' % static_array_size.group(1)
                        construct_txt += '        %s[i] = in_struct->%s[i];\n' % (member.name, member.name)
                        construct_txt += '    }\n'
                    else:
                        # Init array ptr to NULL
                        default_init_list += '\n    %s(nullptr),' % member.name
                        init_list += '\n    %s(nullptr),' % member.name
                        init_func_txt += '    %s = nullptr;\n' % member.name
                        array_element = 'in_struct->%s[i]' % member.name
                        if member.type in self.structNames:
                            member_index = next((i for i, v in enumerate(self.structMembers) if v[0] == member.type), None)
                            if member_index is not None and self.NeedSafeStruct(self.structMembers[member_index]) == True:
                                array_element = '%s(&in_struct->safe_%s[i])' % (member.type, member.name)
                        construct_txt += '    if (%s && in_struct->%s) {\n' % (member.len, member.name)
                        construct_txt += '        %s = new %s[%s];\n' % (member.name, m_type, member.len)
                        destruct_txt += '    if (%s)\n' % member.name
                        destruct_txt += '        delete[] %s;\n' % member.name
                        construct_txt += '        for (uint32_t i = 0; i < %s; ++i) {\n' % (member.len)
                        if 'safe_' in m_type:
                            construct_txt += '            %s[i].initialize(&in_struct->%s[i]);\n' % (member.name, member.name)
                        else:
                            construct_txt += '            %s[i] = %s;\n' % (member.name, array_element)
                        construct_txt += '        }\n'
                        construct_txt += '    }\n'
                elif member.ispointer == True:
                    default_init_list += '\n    %s(nullptr),' % (member.name)
                    init_list += '\n    %s(nullptr),' % (member.name)
                    init_func_txt += '    %s = nullptr;\n' % (member.name)
                    construct_txt += '    if (in_struct->%s)\n' % member.name
                    construct_txt += '        %s = new %s(in_struct->%s);\n' % (member.name, m_type, member.name)
                    destruct_txt += '    if (%s)\n' % member.name
                    destruct_txt += '        delete %s;\n' % member.name
                elif 'safe_' in m_type:
                    init_list += '\n    %s(&in_struct->%s),' % (member.name, member.name)
                    init_func_txt += '    %s.initialize(&in_struct->%s);\n' % (member.name, member.name)
                else:
                    init_list += '\n    %s(in_struct->%s),' % (member.name, member.name)
                    init_func_txt += '    %s = in_struct->%s;\n' % (member.name, member.name)
            if '' != init_list:
                init_list = init_list[:-1] # hack off final comma


            if item.name in custom_construct_txt:
                construct_txt = custom_construct_txt[item.name]

            construct_txt = copy_pnext + copy_strings + construct_txt

            if item.name in custom_destruct_txt:
                destruct_txt = custom_destruct_txt[item.name]

            if copy_pnext:
                destruct_txt += '    if (pNext)\n        FreePnextChain(pNext);\n'

            safe_struct_body.append("\n%s::%s(const %s* in_struct%s) :%s\n{\n%s}" % (ss_name, ss_name, item.name, self.custom_construct_params.get(item.name, ''), init_list, construct_txt))
            if '' != default_init_list:
                default_init_list = " :%s" % (default_init_list[:-1])
            safe_struct_body.append("\n%s::%s()%s\n{}" % (ss_name, ss_name, default_init_list))
            # Create slight variation of init and construct txt for copy constructor that takes a src object reference vs. struct ptr
            copy_construct_init = init_func_txt.replace('in_struct->', 'src.')
            copy_construct_txt = construct_txt.replace(' (in_struct->', ' (src.')            # Exclude 'if' blocks from next line
            copy_construct_txt = construct_txt.replace(' (in_struct->', ' (src.')               # Exclude 'if' blocks from next line
            copy_construct_txt = re.sub('(new \\w+)\\(in_struct->', '\\1(*src.', construct_txt) # Pass object to copy constructors
            copy_construct_txt = copy_construct_txt.replace('in_struct->', 'src.')              # Modify remaining struct refs for src object
            if item.name in custom_copy_txt:
                copy_construct_txt = custom_copy_txt[item.name]
            copy_assign_txt = '    if (&src == this) return *this;\n\n' + destruct_txt + '\n' + copy_construct_init + copy_construct_txt + '\n    return *this;'
            safe_struct_body.append("\n%s::%s(const %s& src)\n{\n%s%s}" % (ss_name, ss_name, ss_name, copy_construct_init, copy_construct_txt)) # Copy constructor
            safe_struct_body.append("\n%s& %s::operator=(const %s& src)\n{\n%s\n}" % (ss_name, ss_name, ss_name, copy_assign_txt)) # Copy assignment operator
            safe_struct_body.append("\n%s::~%s()\n{\n%s}" % (ss_name, ss_name, destruct_txt))
            safe_struct_body.append("\nvoid %s::initialize(const %s* in_struct%s)\n{\n%s%s}" % (ss_name, item.name, self.custom_construct_params.get(item.name, ''), init_func_txt, construct_txt))
            # Copy initializer uses same txt as copy constructor but has a ptr and not a reference
            init_copy = copy_construct_init.replace('src.', 'src->')
            init_construct = copy_construct_txt.replace('src.', 'src->')
            safe_struct_body.append("\nvoid %s::initialize(const %s* src)\n{\n%s%s}" % (ss_name, ss_name, init_copy, init_construct))
            if item.ifdef_protect is not None:
                safe_struct_body.append("#endif // %s\n" % item.ifdef_protect)
        return "\n".join(safe_struct_body)
    #
    # Generate the type map
    def GenerateTypeMapHelperHeader(self):
        prefix = 'Lvl'
        fprefix = 'lvl_'
        typemap = prefix + 'TypeMap'
        idmap = prefix + 'STypeMap'
        type_member = 'Type'
        id_member = 'kSType'
        id_decl = 'static const VkStructureType '
        generic_header = 'VkBaseOutStructure'
        typename_func = fprefix + 'typename'
        idname_func = fprefix + 'stype_name'
        find_func = fprefix + 'find_in_chain'
        init_func = fprefix + 'init_struct'

        explanatory_comment = '\n'.join((
                '// These empty generic templates are specialized for each type with sType',
                '// members and for each sType -- providing a two way map between structure',
                '// types and sTypes'))

        empty_typemap = 'template <typename T> struct ' + typemap + ' {};'
        typemap_format  = 'template <> struct {template}<{typename}> {{\n'
        typemap_format += '    {id_decl}{id_member} = {id_value};\n'
        typemap_format += '}};\n'

        empty_idmap = 'template <VkStructureType id> struct ' + idmap + ' {};'
        idmap_format = ''.join((
            'template <> struct {template}<{id_value}> {{\n',
            '    typedef {typename} {typedef};\n',
            '}};\n'))

        # Define the utilities (here so any renaming stays consistent), if this grows large, refactor to a fixed .h file
        utilities_format = '\n'.join((
            '// Find an entry of the given type in the pNext chain',
            'template <typename T> const T *{find_func}(const void *next) {{',
            '    const {header} *current = reinterpret_cast<const {header} *>(next);',
            '    const T *found = nullptr;',
            '    while (current) {{',
            '        if ({type_map}<T>::{id_member} == current->sType) {{',
            '            found = reinterpret_cast<const T*>(current);',
            '            current = nullptr;',
            '        }} else {{',
            '            current = current->pNext;',
            '        }}',
            '    }}',
            '    return found;',
            '}}',
            '',
            '// Init the header of an sType struct with pNext',
            'template <typename T> T {init_func}(void *p_next) {{',
            '    T out = {{}};',
            '    out.sType = {type_map}<T>::kSType;',
            '    out.pNext = p_next;',
            '    return out;',
            '}}',
                        '',
            '// Init the header of an sType struct',
            'template <typename T> T {init_func}() {{',
            '    T out = {{}};',
            '    out.sType = {type_map}<T>::kSType;',
            '    return out;',
            '}}',

            ''))

        code = []

        # Generate header
        code.append('\n'.join((
            '#pragma once',
            '#include <vulkan/vulkan.h>\n',
            explanatory_comment, '',
            empty_idmap,
            empty_typemap, '')))

        # Generate the specializations for each type and stype
        for item in self.structMembers:
            typename = item.name
            info = self.structTypes.get(typename)
            if not info:
                continue

            if item.ifdef_protect is not None:
                code.append('#ifdef %s' % item.ifdef_protect)

            code.append('// Map type {} to id {}'.format(typename, info.value))
            code.append(typemap_format.format(template=typemap, typename=typename, id_value=info.value,
                id_decl=id_decl, id_member=id_member))
            code.append(idmap_format.format(template=idmap, typename=typename, id_value=info.value, typedef=type_member))

            if item.ifdef_protect is not None:
                code.append('#endif // %s' % item.ifdef_protect)

        # Generate utilities for all types
        code.append('\n'.join((
            utilities_format.format(id_member=id_member, id_map=idmap, type_map=typemap,
                type_member=type_member, header=generic_header, typename_func=typename_func, idname_func=idname_func,
                find_func=find_func, init_func=init_func), ''
            )))

        return "\n".join(code)

    #
    # Create a helper file and return it as a string
    def OutputDestFile(self):
        if self.helper_file_type == 'enum_string_header':
            return self.GenerateEnumStringHelperHeader()
        elif self.helper_file_type == 'safe_struct_header':
            return self.GenerateSafeStructHelperHeader()
        elif self.helper_file_type == 'safe_struct_source':
            return self.GenerateSafeStructHelperSource()
        elif self.helper_file_type == 'object_types_header':
            return self.GenerateObjectTypesHelperHeader()
        elif self.helper_file_type == 'extension_helper_header':
            return self.GenerateExtensionHelperHeader()
        elif self.helper_file_type == 'typemap_helper_header':
            return self.GenerateTypeMapHelperHeader()
        else:
            return 'Bad Helper File Generator Option %s' % self.helper_file_type
