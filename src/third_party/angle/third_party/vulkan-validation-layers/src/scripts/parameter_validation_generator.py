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
# Author: Dustin Graves <dustin@lunarg.com>
# Author: Mark Lobodzinski <mark@lunarg.com>
# Author: Dave Houlton <daveh@lunarg.com>

import os,re,sys,string,json
import xml.etree.ElementTree as etree
from generator import *
from collections import namedtuple
from common_codegen import *

# This is a workaround to use a Python 2.7 and 3.x compatible syntax.
from io import open

# ParameterValidationGeneratorOptions - subclass of GeneratorOptions.
#
# Adds options used by ParameterValidationOutputGenerator object during Parameter validation layer generation.
#
# Additional members
#   prefixText - list of strings to prefix generated header with
#     (usually a copyright statement + calling convention macros).
#   protectFile - True if multiple inclusion protection should be
#     generated (based on the filename) around the entire header.
#   protectFeature - True if #ifndef..#endif protection should be
#     generated around a feature interface in the header file.
#   genFuncPointers - True if function pointer typedefs should be
#     generated
#   protectProto - If conditional protection should be generated
#     around prototype declarations, set to either '#ifdef'
#     to require opt-in (#ifdef protectProtoStr) or '#ifndef'
#     to require opt-out (#ifndef protectProtoStr). Otherwise
#     set to None.
#   protectProtoStr - #ifdef/#ifndef symbol to use around prototype
#     declarations, if protectProto is set
#   apicall - string to use for the function declaration prefix,
#     such as APICALL on Windows.
#   apientry - string to use for the calling convention macro,
#     in typedefs, such as APIENTRY.
#   apientryp - string to use for the calling convention macro
#     in function pointer typedefs, such as APIENTRYP.
#   indentFuncProto - True if prototype declarations should put each
#     parameter on a separate line
#   indentFuncPointer - True if typedefed function pointers should put each
#     parameter on a separate line
#   alignFuncParam - if nonzero and parameters are being put on a
#     separate line, align parameter names at the specified column
class ParameterValidationGeneratorOptions(GeneratorOptions):
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
                 apicall = '',
                 apientry = '',
                 apientryp = '',
                 indentFuncProto = True,
                 indentFuncPointer = False,
                 alignFuncParam = 0,
                 expandEnumerants = True,
                 valid_usage_path = ''):
        GeneratorOptions.__init__(self, conventions, filename, directory, apiname, profile,
                                  versions, emitversions, defaultExtensions,
                                  addExtensions, removeExtensions, emitExtensions, sortProcedure)
        self.prefixText      = prefixText
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam  = alignFuncParam
        self.expandEnumerants = expandEnumerants
        self.valid_usage_path = valid_usage_path

# ParameterValidationOutputGenerator - subclass of OutputGenerator.
# Generates param checker layer code.
#
# ---- methods ----
# ParamCheckerOutputGenerator(errFile, warnFile, diagFile) - args as for
#   OutputGenerator. Defines additional internal state.
# ---- methods overriding base class ----
# beginFile(genOpts)
# endFile()
# beginFeature(interface, emit)
# endFeature()
# genType(typeinfo,name)
# genStruct(typeinfo,name)
# genGroup(groupinfo,name)
# genEnum(enuminfo, name)
# genCmd(cmdinfo)
class ParameterValidationOutputGenerator(OutputGenerator):
    """Generate Parameter Validation code based on XML element attributes"""
    # This is an ordered list of sections in the header file.
    ALL_SECTIONS = ['command']
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        self.INDENT_SPACES = 4
        self.declarations = []

        inline_custom_source_preamble = """
"""

        # These functions have additional, custom-written checks in the utils cpp file. CodeGen will automatically add a call
        # to those functions of the form 'bool manual_PreCallValidateAPIName', where the 'vk' is dropped.
        # see 'manual_PreCallValidateCreateGraphicsPipelines' as an example.
        self.functions_with_manual_checks = [
            'vkCreateInstance',
            'vkCreateDevice',
            'vkCreateQueryPool'
            'vkCreateRenderPass',
            'vkCreateRenderPass2KHR',
            'vkCreateBuffer',
            'vkCreateImage',
            'vkCreateGraphicsPipelines',
            'vkCreateComputePipelines',
            "vkCreateRayTracingPipelinesNV",
            'vkCreateSampler',
            'vkCreateDescriptorSetLayout',
            'vkFreeDescriptorSets',
            'vkUpdateDescriptorSets',
            'vkCreateRenderPass',
            'vkCreateRenderPass2KHR',
            'vkBeginCommandBuffer',
            'vkCmdSetViewport',
            'vkCmdSetScissor',
            'vkCmdSetLineWidth',
            'vkCmdDraw',
            'vkCmdDrawIndirect',
            'vkCmdDrawIndexedIndirect',
            'vkCmdClearAttachments',
            'vkCmdCopyImage',
            'vkCmdBindIndexBuffer',
            'vkCmdBlitImage',
            'vkCmdCopyBufferToImage',
            'vkCmdCopyImageToBuffer',
            'vkCmdUpdateBuffer',
            'vkCmdFillBuffer',
            'vkCreateSwapchainKHR',
            'vkQueuePresentKHR',
            'vkCreateDescriptorPool',
            'vkCmdDispatch',
            'vkCmdDispatchIndirect',
            'vkCmdDispatchBaseKHR',
            'vkCmdSetExclusiveScissorNV',
            'vkCmdSetViewportShadingRatePaletteNV',
            'vkCmdSetCoarseSampleOrderNV',
            'vkCmdDrawMeshTasksNV',
            'vkCmdDrawMeshTasksIndirectNV',
            'vkCmdDrawMeshTasksIndirectCountNV',
            'vkAllocateMemory',
            'vkCreateAccelerationStructureNV',
            'vkGetAccelerationStructureHandleNV',
            'vkCmdBuildAccelerationStructureNV',
            'vkCreateFramebuffer',
            'vkCmdSetLineStippleEXT',
            'vkSetDebugUtilsObjectNameEXT',
            'vkSetDebugUtilsObjectTagEXT',
            'vkCmdSetViewportWScalingNV',
            ]

        # Commands to ignore
        self.blacklist = [
            'vkGetInstanceProcAddr',
            'vkGetDeviceProcAddr',
            'vkEnumerateInstanceVersion',
            'vkEnumerateInstanceLayerProperties',
            'vkEnumerateInstanceExtensionProperties',
            'vkEnumerateDeviceLayerProperties',
            'vkEnumerateDeviceExtensionProperties',
            'vkGetDeviceGroupSurfacePresentModes2EXT'
            ]

        # Structure fields to ignore
        self.structMemberBlacklist = { 'VkWriteDescriptorSet' : ['dstSet'] }
        # Validation conditions for some special case struct members that are conditionally validated
        self.structMemberValidationConditions = { 'VkPipelineColorBlendStateCreateInfo' : { 'logicOp' : '{}logicOpEnable == VK_TRUE' } }
        # Header version
        self.headerVersion = None
        # Internal state - accumulators for different inner block text
        self.validation = []                              # Text comprising the main per-api parameter validation routines
        self.stypes = []                                  # Values from the VkStructureType enumeration
        self.structTypes = dict()                         # Map of Vulkan struct typename to required VkStructureType
        self.handleTypes = set()                          # Set of handle type names
        self.commands = []                                # List of CommandData records for all Vulkan commands
        self.structMembers = []                           # List of StructMemberData records for all Vulkan structs
        self.validatedStructs = dict()                    # Map of structs type names to generated validation code for that struct type
        self.enumRanges = dict()                          # Map of enum name to BEGIN/END range values
        self.enumValueLists = ''                          # String containing enumerated type map definitions
        self.flags = set()                                # Map of flags typenames
        self.flagBits = dict()                            # Map of flag bits typename to list of values
        self.newFlags = set()                             # Map of flags typenames /defined in the current feature/
        self.required_extensions = dict()                 # Dictionary of required extensions for each item in the current extension
        self.extension_type = ''                          # Type of active feature (extension), device or instance
        self.extension_names = dict()                     # Dictionary of extension names to extension name defines
        self.structextends_list = []                      # List of extensions which extend another struct
        self.struct_feature_protect = dict()              # Dictionary of structnames and FeatureExtraProtect strings
        self.valid_vuids = set()                          # Set of all valid VUIDs
        self.vuid_dict = dict()                           # VUID dictionary (from JSON)
        self.alias_dict = dict()                          # Dict of cmd|struct aliases
        self.header_file = False                          # Header file generation flag
        self.source_file = False                          # Source file generation flag
        self.returnedonly_structs = []
        # Named tuples to store struct and command data
        self.CommandParam = namedtuple('CommandParam', ['type', 'name', 'ispointer', 'isstaticarray', 'isbool', 'israngedenum',
                                                        'isconst', 'isoptional', 'iscount', 'noautovalidity',
                                                        'len', 'extstructs', 'condition', 'cdecl'])
        self.CommandData = namedtuple('CommandData', ['name', 'params', 'cdecl', 'extension_type', 'result'])
        self.StructMemberData = namedtuple('StructMemberData', ['name', 'members'])

    #
    # Generate Copyright comment block for file
    def GenerateCopyright(self):
        copyright  = '/* *** THIS FILE IS GENERATED - DO NOT EDIT! ***\n'
        copyright += ' * See parameter_validation_generator.py for modifications\n'
        copyright += ' *\n'
        copyright += ' * Copyright (c) 2015-2019 The Khronos Group Inc.\n'
        copyright += ' * Copyright (c) 2015-2019 LunarG, Inc.\n'
        copyright += ' * Copyright (C) 2015-2019 Google Inc.\n'
        copyright += ' *\n'
        copyright += ' * Licensed under the Apache License, Version 2.0 (the "License");\n'
        copyright += ' * you may not use this file except in compliance with the License.\n'
        copyright += ' * Copyright (c) 2015-2017 Valve Corporation\n'
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
        copyright += ' * Author: Mark Lobodzinski <mark@LunarG.com>\n'
        copyright += ' * Author: Dave Houlton <daveh@LunarG.com>\n'
        copyright += ' */\n\n'
        return copyright
    #
    # Increases the global indent variable
    def incIndent(self, indent):
        inc = ' ' * self.INDENT_SPACES
        if indent:
            return indent + inc
        return inc
    #
    # Decreases the global indent variable
    def decIndent(self, indent):
        if indent and (len(indent) > self.INDENT_SPACES):
            return indent[:-self.INDENT_SPACES]
        return ''
    #
    # Walk the JSON-derived dict and find all "vuid" key values
    def ExtractVUIDs(self, d):
        if hasattr(d, 'items'):
            for k, v in d.items():
                if k == "vuid":
                    yield v
                elif isinstance(v, dict):
                    for s in self.ExtractVUIDs(v):
                        yield s
                elif isinstance (v, list):
                    for l in v:
                        for s in self.ExtractVUIDs(l):
                            yield s
    #
    # Called at file creation time
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        self.header_file = (genOpts.filename == 'parameter_validation.h')
        self.source_file = (genOpts.filename == 'parameter_validation.cpp')

        if not self.header_file and not self.source_file:
            print("Error: Output Filenames have changed, update generator source.\n")
            sys.exit(1)

        if self.source_file or self.header_file:
            # Output Copyright text
            s = self.GenerateCopyright()
            write(s, file=self.outFile)

        if self.header_file:
            return

        # Build map of structure type names to VkStructureType enum values
        # Find all types of category "struct"
        for struct in self.registry.tree.iterfind('types/type[@category="struct"]'):
            # Check if struct has member named "sType" of type "VkStructureType" which has values defined
            stype = struct.find('member[name="sType"][type="VkStructureType"][@values]')
            if stype is not None:
                # Store VkStructureType value for this type
                self.structTypes[struct.get('name')] = stype.get('values')

        self.valid_usage_path = genOpts.valid_usage_path
        vu_json_filename = os.path.join(self.valid_usage_path + os.sep, 'validusage.json')
        if os.path.isfile(vu_json_filename):
            json_file = open(vu_json_filename, 'r')
            self.vuid_dict = json.load(json_file)
            json_file.close()
        if len(self.vuid_dict) == 0:
            print("Error: Could not find, or error loading %s/validusage.json\n", vu_json_filename)
            sys.exit(1)
        #
        # Build a set of all vuid text strings found in validusage.json
        for json_vuid_string in self.ExtractVUIDs(self.vuid_dict):
            self.valid_vuids.add(json_vuid_string)
        #
        # Headers
        write('#include "chassis.h"', file=self.outFile)
        self.newline()
        write('#include "stateless_validation.h"', file=self.outFile)
        self.newline()
    #
    # Called at end-time for final content output
    def endFile(self):
        if self.source_file:
            # C-specific
            self.newline()
            write(self.enumValueLists, file=self.outFile)
            self.newline()

            pnext_handler  = 'bool StatelessValidation::ValidatePnextStructContents(const char *api_name, const ParameterName &parameter_name, const VkBaseOutStructure* header) {\n'
            pnext_handler += '    bool skip = false;\n'
            pnext_handler += '    switch(header->sType) {\n'

            # Do some processing here to extract data from validatedstructs...
            for item in self.structextends_list:
                postProcSpec = {}
                postProcSpec['ppp'] = '' if not item else '{postProcPrefix}'
                postProcSpec['pps'] = '' if not item else '{postProcSuffix}'
                postProcSpec['ppi'] = '' if not item else '{postProcInsert}'

                pnext_case = '\n'
                protect = ''
                # Guard struct cases with feature ifdefs, if necessary
                if item in self.struct_feature_protect.keys():
                    protect = self.struct_feature_protect[item]
                    pnext_case += '#ifdef %s\n' % protect
                pnext_case += '        // Validation code for %s structure members\n' % item
                pnext_case += '        case %s: {\n' % self.structTypes[item]
                pnext_case += '            %s *structure = (%s *) header;\n' % (item, item)
                expr = self.expandStructCode(item, item, 'structure->', '', '            ', [], postProcSpec)
                struct_validation_source = self.ScrubStructCode(expr)
                pnext_case += '%s' % struct_validation_source
                pnext_case += '        } break;\n'
                if protect:
                    pnext_case += '#endif // %s\n' % protect
                # Skip functions containing no validation
                if struct_validation_source:
                    pnext_handler += pnext_case;
            pnext_handler += '        default:\n'
            pnext_handler += '            skip = false;\n'
            pnext_handler += '    }\n'
            pnext_handler += '    return skip;\n'
            pnext_handler += '}\n'
            write(pnext_handler, file=self.outFile)
            self.newline()

            ext_template  = 'bool StatelessValidation::OutputExtensionError(const std::string &api_name, const std::string &extension_name) {\n'
            ext_template += '    return log_msg(report_data, VK_DEBUG_REPORT_ERROR_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT, 0,\n'
            ext_template += '                   kVUID_PVError_ExtensionNotEnabled, "Attemped to call %s() but its required extension %s has not been enabled\\n",\n'
            ext_template += '                   api_name.c_str(), extension_name.c_str());\n'
            ext_template += '}\n'
            write(ext_template, file=self.outFile)
            self.newline()
            commands_text = '\n'.join(self.validation)
            write(commands_text, file=self.outFile)
            self.newline()
        if self.header_file:
            # Output declarations and record intercepted procedures
            write('\n'.join(self.declarations), file=self.outFile)
            # Finish processing in superclass
            OutputGenerator.endFile(self)
    #
    # Processing at beginning of each feature or extension
    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        # C-specific
        # Accumulate includes, defines, types, enums, function pointer typedefs, end function prototypes separately for this
        # feature. They're only printed in endFeature().
        self.headerVersion = None
        self.stypes = []
        self.commands = []
        self.structMembers = []
        self.newFlags = set()
        self.featureExtraProtect = GetFeatureProtect(interface)
        # Get base list of extension dependencies for all items in this extension
        base_required_extensions = []
        if "VK_VERSION_1" not in self.featureName:
            # Save Name Define to get correct enable name later
            nameElem = interface[0][1]
            name = nameElem.get('name')
            self.extension_names[self.featureName] = name
            # This extension is the first dependency for this command
            base_required_extensions.append(self.featureName)
        # Add any defined extension dependencies to the base dependency list for this extension
        requires = interface.get('requires')
        if requires is not None:
            base_required_extensions.extend(requires.split(','))
        # Build dictionary of extension dependencies for each item in this extension
        self.required_extensions = dict()
        for require_element in interface.findall('require'):
            # Copy base extension dependency list
            required_extensions = list(base_required_extensions)
            # Add any additional extension dependencies specified in this require block
            additional_extensions = require_element.get('extension')
            if additional_extensions:
                required_extensions.extend(additional_extensions.split(','))
            # Save full extension list for all named items
            for element in require_element.findall('*[@name]'):
                self.required_extensions[element.get('name')] = required_extensions

        # And note if this is an Instance or Device extension
        self.extension_type = interface.get('type')
    #
    # Called at the end of each extension (feature)
    def endFeature(self):
        if self.header_file:
            return
        # C-specific
        # Actually write the interface to the output file.
        if (self.emit):
            # If type declarations are needed by other features based on this one, it may be necessary to suppress the ExtraProtect,
            # or move it below the 'for section...' loop.
            ifdef = ''
            if (self.featureExtraProtect is not None):
                ifdef = '#ifdef %s\n' % self.featureExtraProtect
                self.validation.append(ifdef)
            # Generate the struct member checking code from the captured data
            self.processStructMemberData()
            # Generate the command parameter checking code from the captured data
            self.processCmdData()
            # Write the declaration for the HeaderVersion
            if self.headerVersion:
                write('const uint32_t GeneratedVulkanHeaderVersion = {};'.format(self.headerVersion), file=self.outFile)
                self.newline()
            # Write the declarations for the VkFlags values combining all flag bits
            for flag in sorted(self.newFlags):
                flagBits = flag.replace('Flags', 'FlagBits')
                if flagBits in self.flagBits:
                    bits = self.flagBits[flagBits]
                    decl = 'const {} All{} = {}'.format(flag, flagBits, bits[0])
                    for bit in bits[1:]:
                        decl += '|' + bit
                    decl += ';'
                    write(decl, file=self.outFile)
            endif = '\n'
            if (self.featureExtraProtect is not None):
                endif = '#endif // %s\n' % self.featureExtraProtect
            self.validation.append(endif)
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    # Type generation
    def genType(self, typeinfo, name, alias):
        # record the name/alias pair
        if alias is not None:
            self.alias_dict[name]=alias
        OutputGenerator.genType(self, typeinfo, name, alias)
        typeElem = typeinfo.elem
        # If the type is a struct type, traverse the embedded <member> tags generating a structure. Otherwise, emit the tag text.
        category = typeElem.get('category')
        if (category == 'struct' or category == 'union'):
            self.genStruct(typeinfo, name, alias)
        elif (category == 'handle'):
            self.handleTypes.add(name)
        elif (category == 'bitmask'):
            self.flags.add(name)
            self.newFlags.add(name)
        elif (category == 'define'):
            if name == 'VK_HEADER_VERSION':
                nameElem = typeElem.find('name')
                self.headerVersion = noneStr(nameElem.tail).strip()
    #
    # Struct parameter check generation.
    # This is a special case of the <type> tag where the contents are interpreted as a set of <member> tags instead of freeform C
    # type declarations. The <member> tags are just like <param> tags - they are a declaration of a struct or union member.
    # Only simple member declarations are supported (no nested structs etc.)
    def genStruct(self, typeinfo, typeName, alias):
        if not self.source_file:
            return
        # alias has already been recorded in genType, above
        OutputGenerator.genStruct(self, typeinfo, typeName, alias)
        conditions = self.structMemberValidationConditions[typeName] if typeName in self.structMemberValidationConditions else None
        members = typeinfo.elem.findall('.//member')
        if self.featureExtraProtect is not None:
            self.struct_feature_protect[typeName] = self.featureExtraProtect
        #
        # Iterate over members once to get length parameters for arrays
        lens = set()
        for member in members:
            len = self.getLen(member)
            if len:
                lens.add(len)
        #
        # Generate member info
        membersInfo = []
        for member in members:
            # Get the member's type and name
            info = self.getTypeNameTuple(member)
            type = info[0]
            name = info[1]
            stypeValue = ''
            cdecl = self.makeCParamDecl(member, 0)

            # Store pointer/array/string info -- Check for parameter name in lens set
            iscount = False
            if name in lens:
                iscount = True
            # The pNext members are not tagged as optional, but are treated as optional for parameter NULL checks.  Static array
            # members are also treated as optional to skip NULL pointer validation, as they won't be NULL.
            isstaticarray = self.paramIsStaticArray(member)
            isoptional = False
            if self.paramIsOptional(member) or (name == 'pNext') or (isstaticarray):
                isoptional = True
            # Determine if value should be ignored by code generation.
            noautovalidity = False
            if (member.attrib.get('noautovalidity') is not None) or ((typeName in self.structMemberBlacklist) and (name in self.structMemberBlacklist[typeName])):
                noautovalidity = True
            structextends = False
            membersInfo.append(self.CommandParam(type=type, name=name,
                                                ispointer=self.paramIsPointer(member),
                                                isstaticarray=isstaticarray,
                                                isbool=True if type == 'VkBool32' else False,
                                                israngedenum=True if type in self.enumRanges else False,
                                                isconst=True if 'const' in cdecl else False,
                                                isoptional=isoptional,
                                                iscount=iscount,
                                                noautovalidity=noautovalidity,
                                                len=self.getLen(member),
                                                extstructs=self.registry.validextensionstructs[typeName] if name == 'pNext' else None,
                                                condition=conditions[name] if conditions and name in conditions else None,
                                                cdecl=cdecl))
        # If this struct extends another, keep its name in list for further processing
        if typeinfo.elem.attrib.get('structextends') is not None:
            self.structextends_list.append(typeName)
        # Returnedonly structs should have most of their members ignored -- on entry, we only care about validating the sType and
        # pNext members. Everything else will be overwritten by the callee.
        if typeinfo.elem.attrib.get('returnedonly') is not None:
            self.returnedonly_structs.append(typeName)
            membersInfo = [m for m in membersInfo if m.name in ('sType', 'pNext')]
        self.structMembers.append(self.StructMemberData(name=typeName, members=membersInfo))
    #
    # Capture group (e.g. C "enum" type) info to be used for param check code generation.
    # These are concatenated together with other types.
    def genGroup(self, groupinfo, groupName, alias):
        if not self.source_file:
            return
        # record the name/alias pair
        if alias is not None:
            self.alias_dict[groupName]=alias
        OutputGenerator.genGroup(self, groupinfo, groupName, alias)
        groupElem = groupinfo.elem
        # Store the sType values
        if groupName == 'VkStructureType':
            for elem in groupElem.findall('enum'):
                self.stypes.append(elem.get('name'))
        elif 'FlagBits' in groupName:
            bits = []
            for elem in groupElem.findall('enum'):
                if elem.get('supported') != 'disabled':
                    bits.append(elem.get('name'))
            if bits:
                self.flagBits[groupName] = bits
        else:
            # Determine if begin/end ranges are needed (we don't do this for VkStructureType, which has a more finely grained check)
            expandName = re.sub(r'([0-9a-z_])([A-Z0-9][^A-Z0-9]?)',r'\1_\2',groupName).upper()
            expandPrefix = expandName
            expandSuffix = ''
            expandSuffixMatch = re.search(r'[A-Z][A-Z]+$',groupName)
            if expandSuffixMatch:
                expandSuffix = '_' + expandSuffixMatch.group()
                # Strip off the suffix from the prefix
                expandPrefix = expandName.rsplit(expandSuffix, 1)[0]
            isEnum = ('FLAG_BITS' not in expandPrefix)
            if isEnum:
                self.enumRanges[groupName] = (expandPrefix + '_BEGIN_RANGE' + expandSuffix, expandPrefix + '_END_RANGE' + expandSuffix)
                # Create definition for a list containing valid enum values for this enumerated type
                if self.featureExtraProtect is not None:
                    enum_entry = '\n#ifdef %s\n' % self.featureExtraProtect
                else:
                    enum_entry = ''
                enum_entry += 'const std::vector<%s> All%sEnums = {' % (groupName, groupName)
                for enum in groupElem:
                    name = enum.get('name')
                    if name is not None and enum.get('supported') != 'disabled':
                        enum_entry += '%s, ' % name
                enum_entry += '};\n'
                if self.featureExtraProtect is not None:
                    enum_entry += '#endif // %s\n' % self.featureExtraProtect
                self.enumValueLists += enum_entry
    #
    # Capture command parameter info to be used for param check code generation.
    def genCmd(self, cmdinfo, name, alias):
        # record the name/alias pair
        if alias is not None:
            self.alias_dict[name]=alias
        OutputGenerator.genCmd(self, cmdinfo, name, alias)
        decls = self.makeCDecls(cmdinfo.elem)
        typedef = decls[1]
        typedef = typedef.split(')',1)[1]
        if self.header_file:
            if name not in self.blacklist:
                if (self.featureExtraProtect is not None):
                    self.declarations += [ '#ifdef %s' % self.featureExtraProtect ]
                # Strip off 'vk' from API name
                self.declarations += [ '%s%s' % ('bool PreCallValidate', decls[0].split("VKAPI_CALL vk")[1])]
                if (self.featureExtraProtect is not None):
                    self.declarations += [ '#endif' ]
        if self.source_file:
            if name not in self.blacklist:
                params = cmdinfo.elem.findall('param')
                # Get list of array lengths
                lens = set()
                for param in params:
                    len = self.getLen(param)
                    if len:
                        lens.add(len)
                # Get param info
                paramsInfo = []
                for param in params:
                    paramInfo = self.getTypeNameTuple(param)
                    cdecl = self.makeCParamDecl(param, 0)
                    # Check for parameter name in lens set
                    iscount = False
                    if paramInfo[1] in lens:
                        iscount = True
                    paramsInfo.append(self.CommandParam(type=paramInfo[0], name=paramInfo[1],
                                                        ispointer=self.paramIsPointer(param),
                                                        isstaticarray=self.paramIsStaticArray(param),
                                                        isbool=True if paramInfo[0] == 'VkBool32' else False,
                                                        israngedenum=True if paramInfo[0] in self.enumRanges else False,
                                                        isconst=True if 'const' in cdecl else False,
                                                        isoptional=self.paramIsOptional(param),
                                                        iscount=iscount,
                                                        noautovalidity=True if param.attrib.get('noautovalidity') is not None else False,
                                                        len=self.getLen(param),
                                                        extstructs=None,
                                                        condition=None,
                                                        cdecl=cdecl))
                # Save return value information, if any
                result_type = ''
                resultinfo = cmdinfo.elem.find('proto/type')
                if (resultinfo is not None and resultinfo.text != 'void'):
                    result_type = resultinfo.text
                self.commands.append(self.CommandData(name=name, params=paramsInfo, cdecl=self.makeCDecls(cmdinfo.elem)[0], extension_type=self.extension_type, result=result_type))
    #
    # Check if the parameter passed in is a pointer
    def paramIsPointer(self, param):
        ispointer = 0
        paramtype = param.find('type')
        if (paramtype.tail is not None) and ('*' in paramtype.tail):
            ispointer = paramtype.tail.count('*')
        elif paramtype.text[:4] == 'PFN_':
            # Treat function pointer typedefs as a pointer to a single value
            ispointer = 1
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
    # Check if the parameter passed in is optional
    # Returns a list of Boolean values for comma separated len attributes (len='false,true')
    def paramIsOptional(self, param):
        # See if the handle is optional
        isoptional = False
        # Simple, if it's optional, return true
        optString = param.attrib.get('optional')
        if optString:
            if optString == 'true':
                isoptional = True
            elif ',' in optString:
                opts = []
                for opt in optString.split(','):
                    val = opt.strip()
                    if val == 'true':
                        opts.append(True)
                    elif val == 'false':
                        opts.append(False)
                    else:
                        print('Unrecognized len attribute value',val)
                isoptional = opts
        return isoptional
    #
    # Check if the handle passed in is optional
    # Uses the same logic as ValidityOutputGenerator.isHandleOptional
    def isHandleOptional(self, param, lenParam):
        # Simple, if it's optional, return true
        if param.isoptional:
            return True
        # If no validity is being generated, it usually means that validity is complex and not absolute, so let's say yes.
        if param.noautovalidity:
            return True
        # If the parameter is an array and we haven't already returned, find out if any of the len parameters are optional
        if lenParam and lenParam.isoptional:
            return True
        return False
    #
    # Retrieve the value of the len tag
    def getLen(self, param):
        result = None
        len = param.attrib.get('len')
        if len and len != 'null-terminated':
            # For string arrays, 'len' can look like 'count,null-terminated', indicating that we have a null terminated array of
            # strings.  We strip the null-terminated from the 'len' field and only return the parameter specifying the string count
            if 'null-terminated' in len:
                result = len.split(',')[0]
            else:
                result = len
            result = str(result).replace('::', '->')
        return result
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
    #
    # Find a named parameter in a parameter list
    def getParamByName(self, params, name):
        for param in params:
            if param.name == name:
                return param
        return None
    #
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
            decoratedName = '{}({}/{})'.format(*match.group(1, 2, 3))
        else:
            # Matches expressions similar to 'latexmath : [dataSize \over 4]'
            match = re.match(r'latexmath\s*\:\s*\[\s*(\\textrm\{)?(\w+)\}?\s*\\over\s*(\d+)\s*\]', source)
            name = match.group(2)
            decoratedName = '{}/{}'.format(*match.group(2, 3))
        return name, decoratedName
    #
    # Get the length paramater record for the specified parameter name
    def getLenParam(self, params, name):
        lenParam = None
        if name:
            if '->' in name:
                # The count is obtained by dereferencing a member of a struct parameter
                lenParam = self.CommandParam(name=name, iscount=True, ispointer=False, isbool=False, israngedenum=False, isconst=False,
                                             isstaticarray=None, isoptional=False, type=None, noautovalidity=False,
                                             len=None, extstructs=None, condition=None, cdecl=None)
            elif 'latexmath' in name:
                lenName, decoratedName = self.parseLateXMath(name)
                lenParam = self.getParamByName(params, lenName)
            else:
                lenParam = self.getParamByName(params, name)
        return lenParam
    #
    # Convert a vulkan.h command declaration into a parameter_validation.h definition
    def getCmdDef(self, cmd):
        # Strip the trailing ';' and split into individual lines
        lines = cmd.cdecl[:-1].split('\n')
        cmd_hdr = '\n'.join(lines)
        return cmd_hdr
    #
    # Generate the code to check for a NULL dereference before calling the
    # validation function
    def genCheckedLengthCall(self, name, exprs):
        count = name.count('->')
        if count:
            checkedExpr = []
            localIndent = ''
            elements = name.split('->')
            # Open the if expression blocks
            for i in range(0, count):
                checkedExpr.append(localIndent + 'if ({} != NULL) {{\n'.format('->'.join(elements[0:i+1])))
                localIndent = self.incIndent(localIndent)
            # Add the validation expression
            for expr in exprs:
                checkedExpr.append(localIndent + expr)
            # Close the if blocks
            for i in range(0, count):
                localIndent = self.decIndent(localIndent)
                checkedExpr.append(localIndent + '}\n')
            return [checkedExpr]
        # No if statements were required
        return exprs
    #
    # Generate code to check for a specific condition before executing validation code
    def genConditionalCall(self, prefix, condition, exprs):
        checkedExpr = []
        localIndent = ''
        formattedCondition = condition.format(prefix)
        checkedExpr.append(localIndent + 'if ({})\n'.format(formattedCondition))
        checkedExpr.append(localIndent + '{\n')
        localIndent = self.incIndent(localIndent)
        for expr in exprs:
            checkedExpr.append(localIndent + expr)
        localIndent = self.decIndent(localIndent)
        checkedExpr.append(localIndent + '}\n')
        return [checkedExpr]
    #
    # Get VUID identifier from implicit VUID tag
    def GetVuid(self, name, suffix):
        vuid_string = 'VUID-%s-%s' % (name, suffix)
        vuid = "kVUIDUndefined"
        if '->' in vuid_string:
           return vuid
        if vuid_string in self.valid_vuids:
            vuid = "\"%s\"" % vuid_string
        else:
            if name in self.alias_dict:
                alias_string = 'VUID-%s-%s' % (self.alias_dict[name], suffix)
                if alias_string in self.valid_vuids:
                    vuid = "\"%s\"" % alias_string
        return vuid
    #
    # Generate the sType check string
    def makeStructTypeCheck(self, prefix, value, lenValue, valueRequired, lenValueRequired, lenPtrRequired, funcPrintName, lenPrintName, valuePrintName, postProcSpec, struct_type_name):
        checkExpr = []
        stype = self.structTypes[value.type]
        vuid_name = struct_type_name if struct_type_name is not None else funcPrintName
        stype_vuid = self.GetVuid(value.type, "sType-sType")
        param_vuid = self.GetVuid(vuid_name, "%s-parameter" % value.name)

        if lenValue:
            count_required_vuid = self.GetVuid(vuid_name, "%s-arraylength" % lenValue.name)

            # This is an array with a pointer to a count value
            if lenValue.ispointer:
                # When the length parameter is a pointer, there is an extra Boolean parameter in the function call to indicate if it is required
                checkExpr.append('skip |= validate_struct_type_array("{}", {ppp}"{ldn}"{pps}, {ppp}"{dn}"{pps}, "{sv}", {pf}{ln}, {pf}{vn}, {sv}, {}, {}, {}, {}, {}, {});\n'.format(
                    funcPrintName, lenPtrRequired, lenValueRequired, valueRequired, stype_vuid, param_vuid, count_required_vuid, ln=lenValue.name, ldn=lenPrintName, dn=valuePrintName, vn=value.name, sv=stype, pf=prefix, **postProcSpec))
            # This is an array with an integer count value
            else:
                checkExpr.append('skip |= validate_struct_type_array("{}", {ppp}"{ldn}"{pps}, {ppp}"{dn}"{pps}, "{sv}", {pf}{ln}, {pf}{vn}, {sv}, {}, {}, {}, {}, {});\n'.format(
                    funcPrintName, lenValueRequired, valueRequired, stype_vuid, param_vuid, count_required_vuid, ln=lenValue.name, ldn=lenPrintName, dn=valuePrintName, vn=value.name, sv=stype, pf=prefix, **postProcSpec))
        # This is an individual struct
        else:
            checkExpr.append('skip |= validate_struct_type("{}", {ppp}"{}"{pps}, "{sv}", {}{vn}, {sv}, {}, {}, {});\n'.format(
                funcPrintName, valuePrintName, prefix, valueRequired, param_vuid, stype_vuid, vn=value.name, sv=stype, vt=value.type, **postProcSpec))
        return checkExpr
    #
    # Generate the handle check string
    def makeHandleCheck(self, prefix, value, lenValue, valueRequired, lenValueRequired, funcPrintName, lenPrintName, valuePrintName, postProcSpec):
        checkExpr = []
        if lenValue:
            if lenValue.ispointer:
                # This is assumed to be an output array with a pointer to a count value
                raise('Unsupported parameter validation case: Output handle array elements are not NULL checked')
            else:
                # This is an array with an integer count value
                checkExpr.append('skip |= validate_handle_array("{}", {ppp}"{ldn}"{pps}, {ppp}"{dn}"{pps}, {pf}{ln}, {pf}{vn}, {}, {});\n'.format(
                    funcPrintName, lenValueRequired, valueRequired, ln=lenValue.name, ldn=lenPrintName, dn=valuePrintName, vn=value.name, pf=prefix, **postProcSpec))
        else:
            # This is assumed to be an output handle pointer
            raise('Unsupported parameter validation case: Output handles are not NULL checked')
        return checkExpr
    #
    # Generate check string for an array of VkFlags values
    def makeFlagsArrayCheck(self, prefix, value, lenValue, valueRequired, lenValueRequired, funcPrintName, lenPrintName, valuePrintName, postProcSpec):
        checkExpr = []
        flagBitsName = value.type.replace('Flags', 'FlagBits')
        if not flagBitsName in self.flagBits:
            raise('Unsupported parameter validation case: array of reserved VkFlags')
        else:
            allFlags = 'All' + flagBitsName
            checkExpr.append('skip |= validate_flags_array("{}", {ppp}"{}"{pps}, {ppp}"{}"{pps}, "{}", {}, {pf}{}, {pf}{}, {}, {});\n'.format(funcPrintName, lenPrintName, valuePrintName, flagBitsName, allFlags, lenValue.name, value.name, lenValueRequired, valueRequired, pf=prefix, **postProcSpec))
        return checkExpr
    #
    # Generate pNext check string
    def makeStructNextCheck(self, prefix, value, funcPrintName, valuePrintName, postProcSpec, struct_type_name):
        checkExpr = []
        # Generate an array of acceptable VkStructureType values for pNext
        extStructCount = 0
        extStructVar = 'NULL'
        extStructNames = 'NULL'
        vuid = self.GetVuid(struct_type_name, "pNext-pNext")
        if value.extstructs:
            extStructVar = 'allowed_structs_{}'.format(struct_type_name)
            extStructCount = 'ARRAY_SIZE({})'.format(extStructVar)
            extStructNames = '"' + ', '.join(value.extstructs) + '"'
            checkExpr.append('const VkStructureType {}[] = {{ {} }};\n'.format(extStructVar, ', '.join([self.structTypes[s] for s in value.extstructs])))
        checkExpr.append('skip |= validate_struct_pnext("{}", {ppp}"{}"{pps}, {}, {}{}, {}, {}, GeneratedVulkanHeaderVersion, {});\n'.format(
            funcPrintName, valuePrintName, extStructNames, prefix, value.name, extStructCount, extStructVar, vuid, **postProcSpec))
        return checkExpr
    #
    # Generate the pointer check string
    def makePointerCheck(self, prefix, value, lenValue, valueRequired, lenValueRequired, lenPtrRequired, funcPrintName, lenPrintName, valuePrintName, postProcSpec, struct_type_name):
        checkExpr = []
        vuid_tag_name = struct_type_name if struct_type_name is not None else funcPrintName
        if lenValue:
            count_required_vuid = self.GetVuid(vuid_tag_name, "%s-arraylength" % (lenValue.name))
            array_required_vuid = self.GetVuid(vuid_tag_name, "%s-parameter" % (value.name))
            # TODO: Remove workaround for missing optional tag in vk.xml
            if array_required_vuid == '"VUID-VkFramebufferCreateInfo-pAttachments-parameter"':
                return []
            # This is an array with a pointer to a count value
            if lenValue.ispointer:
                # If count and array parameters are optional, there will be no validation
                if valueRequired == 'true' or lenPtrRequired == 'true' or lenValueRequired == 'true':
                    # When the length parameter is a pointer, there is an extra Boolean parameter in the function call to indicate if it is required
                    checkExpr.append('skip |= validate_array("{}", {ppp}"{ldn}"{pps}, {ppp}"{dn}"{pps}, {pf}{ln}, &{pf}{vn}, {}, {}, {}, {}, {});\n'.format(
                        funcPrintName, lenPtrRequired, lenValueRequired, valueRequired, count_required_vuid, array_required_vuid, ln=lenValue.name, ldn=lenPrintName, dn=valuePrintName, vn=value.name, pf=prefix, **postProcSpec))
            # This is an array with an integer count value
            else:
                # If count and array parameters are optional, there will be no validation
                if valueRequired == 'true' or lenValueRequired == 'true':
                    if value.type != 'char':
                        checkExpr.append('skip |= validate_array("{}", {ppp}"{ldn}"{pps}, {ppp}"{dn}"{pps}, {pf}{ln}, &{pf}{vn}, {}, {}, {}, {});\n'.format(
                            funcPrintName, lenValueRequired, valueRequired, count_required_vuid, array_required_vuid, ln=lenValue.name, ldn=lenPrintName, dn=valuePrintName, vn=value.name, pf=prefix, **postProcSpec))
                    else:
                        # Arrays of strings receive special processing
                        checkExpr.append('skip |= validate_string_array("{}", {ppp}"{ldn}"{pps}, {ppp}"{dn}"{pps}, {pf}{ln}, {pf}{vn}, {}, {}, {}, {});\n'.format(
                            funcPrintName, lenValueRequired, valueRequired, count_required_vuid, array_required_vuid, ln=lenValue.name, ldn=lenPrintName, dn=valuePrintName, vn=value.name, pf=prefix, **postProcSpec))
            if checkExpr:
                if lenValue and ('->' in lenValue.name):
                    # Add checks to ensure the validation call does not dereference a NULL pointer to obtain the count
                    checkExpr = self.genCheckedLengthCall(lenValue.name, checkExpr)
        # This is an individual struct that is not allowed to be NULL
        elif not value.isoptional:
            # Function pointers need a reinterpret_cast to void*
            ptr_required_vuid = self.GetVuid(vuid_tag_name, "%s-parameter" % (value.name))
            if value.type[:4] == 'PFN_':
                allocator_dict = {'pfnAllocation': '"VUID-VkAllocationCallbacks-pfnAllocation-00632"',
                                  'pfnReallocation': '"VUID-VkAllocationCallbacks-pfnReallocation-00633"',
                                  'pfnFree': '"VUID-VkAllocationCallbacks-pfnFree-00634"',
                                 }
                vuid = allocator_dict.get(value.name)
                if vuid is not None:
                    ptr_required_vuid = vuid
                checkExpr.append('skip |= validate_required_pointer("{}", {ppp}"{}"{pps}, reinterpret_cast<const void*>({}{}), {});\n'.format(funcPrintName, valuePrintName, prefix, value.name, ptr_required_vuid, **postProcSpec))
            else:
                checkExpr.append('skip |= validate_required_pointer("{}", {ppp}"{}"{pps}, {}{}, {});\n'.format(funcPrintName, valuePrintName, prefix, value.name, ptr_required_vuid, **postProcSpec))
        else:
            # Special case for optional internal allocation function pointers.
            if (value.type, value.name) == ('PFN_vkInternalAllocationNotification', 'pfnInternalAllocation'):
                checkExpr.extend(self.internalAllocationCheck(funcPrintName, prefix, value.name, 'pfnInternalFree', postProcSpec))
            elif (value.type, value.name) == ('PFN_vkInternalFreeNotification', 'pfnInternalFree'):
                checkExpr.extend(self.internalAllocationCheck(funcPrintName, prefix, value.name, 'pfnInternalAllocation', postProcSpec))
        return checkExpr

    #
    # Generate internal allocation function pointer check.
    def internalAllocationCheck(self, funcPrintName, prefix, name, complementaryName, postProcSpec):
        checkExpr = []
        vuid = '"VUID-VkAllocationCallbacks-pfnInternalAllocation-00635"'
        checkExpr.append('if ({}{} != NULL)'.format(prefix, name))
        checkExpr.append('{')
        local_indent = self.incIndent('')
        # Function pointers need a reinterpret_cast to void*
        checkExpr.append(local_indent + 'skip |= validate_required_pointer("{}", {ppp}"{}{}"{pps}, reinterpret_cast<const void*>({}{}), {});\n'.format(funcPrintName, prefix, complementaryName, prefix, complementaryName, vuid, **postProcSpec))
        checkExpr.append('}\n')
        return checkExpr

    #
    # Process struct member validation code, performing name substitution if required
    def processStructMemberCode(self, line, funcName, memberNamePrefix, memberDisplayNamePrefix, postProcSpec):
        # Build format specifier list
        kwargs = {}
        if '{postProcPrefix}' in line:
            # If we have a tuple that includes a format string and format parameters, need to use ParameterName class
            if type(memberDisplayNamePrefix) is tuple:
                kwargs['postProcPrefix'] = 'ParameterName('
            else:
                kwargs['postProcPrefix'] = postProcSpec['ppp']
        if '{postProcSuffix}' in line:
            # If we have a tuple that includes a format string and format parameters, need to use ParameterName class
            if type(memberDisplayNamePrefix) is tuple:
                kwargs['postProcSuffix'] = ', ParameterName::IndexVector{{ {}{} }})'.format(postProcSpec['ppi'], memberDisplayNamePrefix[1])
            else:
                kwargs['postProcSuffix'] = postProcSpec['pps']
        if '{postProcInsert}' in line:
            # If we have a tuple that includes a format string and format parameters, need to use ParameterName class
            if type(memberDisplayNamePrefix) is tuple:
                kwargs['postProcInsert'] = '{}{}, '.format(postProcSpec['ppi'], memberDisplayNamePrefix[1])
            else:
                kwargs['postProcInsert'] = postProcSpec['ppi']
        if '{funcName}' in line:
            kwargs['funcName'] = funcName
        if '{valuePrefix}' in line:
            kwargs['valuePrefix'] = memberNamePrefix
        if '{displayNamePrefix}' in line:
            # Check for a tuple that includes a format string and format parameters to be used with the ParameterName class
            if type(memberDisplayNamePrefix) is tuple:
                kwargs['displayNamePrefix'] = memberDisplayNamePrefix[0]
            else:
                kwargs['displayNamePrefix'] = memberDisplayNamePrefix

        if kwargs:
            # Need to escape the C++ curly braces
            if 'IndexVector' in line:
                line = line.replace('IndexVector{ ', 'IndexVector{{ ')
                line = line.replace(' }),', ' }}),')
            return line.format(**kwargs)
        return line
    #
    # Process struct member validation code, stripping metadata
    def ScrubStructCode(self, code):
        scrubbed_lines = ''
        for line in code:
            if 'validate_struct_pnext' in line:
                continue
            if 'allowed_structs' in line:
                continue
            if 'xml-driven validation' in line:
                continue
            line = line.replace('{postProcPrefix}', '')
            line = line.replace('{postProcSuffix}', '')
            line = line.replace('{postProcInsert}', '')
            line = line.replace('{funcName}', '')
            line = line.replace('{valuePrefix}', '')
            line = line.replace('{displayNamePrefix}', '')
            line = line.replace('{IndexVector}', '')
            line = line.replace('local_data->', '')
            scrubbed_lines += line
        return scrubbed_lines
    #
    # Process struct validation code for inclusion in function or parent struct validation code
    def expandStructCode(self, item_type, funcName, memberNamePrefix, memberDisplayNamePrefix, indent, output, postProcSpec):
        lines = self.validatedStructs[item_type]
        for line in lines:
            if output:
                output[-1] += '\n'
            if type(line) is list:
                for sub in line:
                    output.append(self.processStructMemberCode(indent + sub, funcName, memberNamePrefix, memberDisplayNamePrefix, postProcSpec))
            else:
                output.append(self.processStructMemberCode(indent + line, funcName, memberNamePrefix, memberDisplayNamePrefix, postProcSpec))
        return output
    #
    # Process struct pointer/array validation code, performing name substitution if required
    def expandStructPointerCode(self, prefix, value, lenValue, funcName, valueDisplayName, postProcSpec):
        expr = []
        expr.append('if ({}{} != NULL)\n'.format(prefix, value.name))
        expr.append('{')
        indent = self.incIndent(None)
        if lenValue:
            # Need to process all elements in the array
            indexName = lenValue.name.replace('Count', 'Index')
            expr[-1] += '\n'
            if lenValue.ispointer:
                # If the length value is a pointer, de-reference it for the count.
                expr.append(indent + 'for (uint32_t {iname} = 0; {iname} < *{}{}; ++{iname})\n'.format(prefix, lenValue.name, iname=indexName))
            else:
                expr.append(indent + 'for (uint32_t {iname} = 0; {iname} < {}{}; ++{iname})\n'.format(prefix, lenValue.name, iname=indexName))
            expr.append(indent + '{')
            indent = self.incIndent(indent)
            # Prefix for value name to display in error message
            if value.ispointer == 2:
                memberNamePrefix = '{}{}[{}]->'.format(prefix, value.name, indexName)
                memberDisplayNamePrefix = ('{}[%i]->'.format(valueDisplayName), indexName)
            else:
                memberNamePrefix = '{}{}[{}].'.format(prefix, value.name, indexName)
                memberDisplayNamePrefix = ('{}[%i].'.format(valueDisplayName), indexName)
        else:
            memberNamePrefix = '{}{}->'.format(prefix, value.name)
            memberDisplayNamePrefix = '{}->'.format(valueDisplayName)
        # Expand the struct validation lines
        expr = self.expandStructCode(value.type, funcName, memberNamePrefix, memberDisplayNamePrefix, indent, expr, postProcSpec)
        if lenValue:
            # Close if and for scopes
            indent = self.decIndent(indent)
            expr.append(indent + '}\n')
        expr.append('}\n')
        return expr
    #
    # Generate the parameter checking code
    def genFuncBody(self, funcName, values, valuePrefix, displayNamePrefix, structTypeName):
        lines = []    # Generated lines of code
        unused = []   # Unused variable names
        for value in values:
            usedLines = []
            lenParam = None
            #
            # Prefix and suffix for post processing of parameter names for struct members.  Arrays of structures need special processing to include the array index in the full parameter name.
            postProcSpec = {}
            postProcSpec['ppp'] = '' if not structTypeName else '{postProcPrefix}'
            postProcSpec['pps'] = '' if not structTypeName else '{postProcSuffix}'
            postProcSpec['ppi'] = '' if not structTypeName else '{postProcInsert}'
            #
            # Generate the full name of the value, which will be printed in the error message, by adding the variable prefix to the value name
            valueDisplayName = '{}{}'.format(displayNamePrefix, value.name)
            #
            # Check for NULL pointers, ignore the in-out count parameters that
            # will be validated with their associated array
            if (value.ispointer or value.isstaticarray) and not value.iscount:
                # Parameters for function argument generation
                req = 'true'    # Parameter cannot be NULL
                cpReq = 'true'  # Count pointer cannot be NULL
                cvReq = 'true'  # Count value cannot be 0
                lenDisplayName = None # Name of length parameter to print with validation messages; parameter name with prefix applied
                # Generate required/optional parameter strings for the pointer and count values
                if value.isoptional:
                    req = 'false'
                if value.len:
                    # The parameter is an array with an explicit count parameter
                    lenParam = self.getLenParam(values, value.len)
                    lenDisplayName = '{}{}'.format(displayNamePrefix, lenParam.name)
                    if lenParam.ispointer:
                        # Count parameters that are pointers are inout
                        if type(lenParam.isoptional) is list:
                            if lenParam.isoptional[0]:
                                cpReq = 'false'
                            if lenParam.isoptional[1]:
                                cvReq = 'false'
                        else:
                            if lenParam.isoptional:
                                cpReq = 'false'
                    else:
                        if lenParam.isoptional:
                            cvReq = 'false'
                #
                # The parameter will not be processed when tagged as 'noautovalidity'
                # For the pointer to struct case, the struct pointer will not be validated, but any
                # members not tagged as 'noautovalidity' will be validated
                # We special-case the custom allocator checks, as they are explicit but can be auto-generated.
                AllocatorFunctions = ['PFN_vkAllocationFunction', 'PFN_vkReallocationFunction', 'PFN_vkFreeFunction', 'PFN_vkInternalAllocationNotification', 'PFN_vkInternalFreeNotification']
                if value.noautovalidity and value.type not in AllocatorFunctions:
                    # Log a diagnostic message when validation cannot be automatically generated and must be implemented manually
                    self.logMsg('diag', 'ParameterValidation: No validation for {} {}'.format(structTypeName if structTypeName else funcName, value.name))
                else:
                    if value.type in self.structTypes:
                        # If this is a pointer to a struct with an sType field, verify the type
                        usedLines += self.makeStructTypeCheck(valuePrefix, value, lenParam, req, cvReq, cpReq, funcName, lenDisplayName, valueDisplayName, postProcSpec, structTypeName)
                    # If this is an input handle array that is not allowed to contain NULL handles, verify that none of the handles are VK_NULL_HANDLE
                    elif value.type in self.handleTypes and value.isconst and not self.isHandleOptional(value, lenParam):
                        usedLines += self.makeHandleCheck(valuePrefix, value, lenParam, req, cvReq, funcName, lenDisplayName, valueDisplayName, postProcSpec)
                    elif value.type in self.flags and value.isconst:
                        usedLines += self.makeFlagsArrayCheck(valuePrefix, value, lenParam, req, cvReq, funcName, lenDisplayName, valueDisplayName, postProcSpec)
                    elif value.isbool and value.isconst:
                        usedLines.append('skip |= validate_bool32_array("{}", {ppp}"{}"{pps}, {ppp}"{}"{pps}, {pf}{}, {pf}{}, {}, {});\n'.format(funcName, lenDisplayName, valueDisplayName, lenParam.name, value.name, cvReq, req, pf=valuePrefix, **postProcSpec))
                    elif value.israngedenum and value.isconst:
                        enum_value_list = 'All%sEnums' % value.type
                        usedLines.append('skip |= validate_ranged_enum_array("{}", {ppp}"{}"{pps}, {ppp}"{}"{pps}, "{}", {}, {pf}{}, {pf}{}, {}, {});\n'.format(funcName, lenDisplayName, valueDisplayName, value.type, enum_value_list, lenParam.name, value.name, cvReq, req, pf=valuePrefix, **postProcSpec))
                    elif value.name == 'pNext' and value.isconst:
                        usedLines += self.makeStructNextCheck(valuePrefix, value, funcName, valueDisplayName, postProcSpec, structTypeName)
                    else:
                        usedLines += self.makePointerCheck(valuePrefix, value, lenParam, req, cvReq, cpReq, funcName, lenDisplayName, valueDisplayName, postProcSpec, structTypeName)
                    # If this is a pointer to a struct (input), see if it contains members that need to be checked
                    if value.type in self.validatedStructs:
                        if value.isconst: # or value.type in self.returnedonly_structs:
                            usedLines.append(self.expandStructPointerCode(valuePrefix, value, lenParam, funcName, valueDisplayName, postProcSpec))
                        elif value.type in self.returnedonly_structs:
                            usedLines.append(self.expandStructPointerCode(valuePrefix, value, lenParam, funcName, valueDisplayName, postProcSpec))
            # Non-pointer types
            else:
                # The parameter will not be processes when tagged as 'noautovalidity'
                # For the struct case, the struct type will not be validated, but any
                # members not tagged as 'noautovalidity' will be validated
                if value.noautovalidity:
                    # Log a diagnostic message when validation cannot be automatically generated and must be implemented manually
                    self.logMsg('diag', 'ParameterValidation: No validation for {} {}'.format(structTypeName if structTypeName else funcName, value.name))
                else:
                    vuid_name_tag = structTypeName if structTypeName is not None else funcName
                    if value.type in self.structTypes:
                        stype = self.structTypes[value.type]
                        vuid = self.GetVuid(value.type, "sType-sType")
                        undefined_vuid = '"kVUIDUndefined"'
                        usedLines.append('skip |= validate_struct_type("{}", {ppp}"{}"{pps}, "{sv}", &({}{vn}), {sv}, false, kVUIDUndefined, {});\n'.format(
                            funcName, valueDisplayName, valuePrefix, vuid, vn=value.name, sv=stype, vt=value.type, **postProcSpec))
                    elif value.type in self.handleTypes:
                        if not self.isHandleOptional(value, None):
                            usedLines.append('skip |= validate_required_handle("{}", {ppp}"{}"{pps}, {}{});\n'.format(funcName, valueDisplayName, valuePrefix, value.name, **postProcSpec))
                    elif value.type in self.flags and value.type.replace('Flags', 'FlagBits') not in self.flagBits:
                        vuid = self.GetVuid(vuid_name_tag, "%s-zerobitmask" % (value.name))
                        usedLines.append('skip |= validate_reserved_flags("{}", {ppp}"{}"{pps}, {pf}{}, {});\n'.format(funcName, valueDisplayName, value.name, vuid, pf=valuePrefix, **postProcSpec))
                    elif value.type in self.flags or value.type in self.flagBits:
                        if value.type in self.flags:
                            flagBitsName = value.type.replace('Flags', 'FlagBits')
                            flagsType = 'kOptionalFlags' if value.isoptional else 'kRequiredFlags'
                            invalidVuid = self.GetVuid(vuid_name_tag, "%s-parameter" % (value.name))
                            zeroVuid = self.GetVuid(vuid_name_tag, "%s-requiredbitmask" % (value.name))
                        elif value.type in self.flagBits:
                            flagBitsName = value.type
                            flagsType = 'kOptionalSingleBit' if value.isoptional else 'kRequiredSingleBit'
                            invalidVuid = self.GetVuid(vuid_name_tag, "%s-parameter" % (value.name))
                            zeroVuid = invalidVuid
                        allFlagsName = 'All' + flagBitsName

                        invalid_vuid = self.GetVuid(vuid_name_tag, "%s-parameter" % (value.name))
                        allFlagsName = 'All' + flagBitsName
                        zeroVuidArg = '' if value.isoptional else ', ' + zeroVuid
                        usedLines.append('skip |= validate_flags("{}", {ppp}"{}"{pps}, "{}", {}, {pf}{}, {}, {}{});\n'.format(funcName, valueDisplayName, flagBitsName, allFlagsName, value.name, flagsType, invalidVuid, zeroVuidArg, pf=valuePrefix, **postProcSpec))
                    elif value.isbool:
                        usedLines.append('skip |= validate_bool32("{}", {ppp}"{}"{pps}, {}{});\n'.format(funcName, valueDisplayName, valuePrefix, value.name, **postProcSpec))
                    elif value.israngedenum:
                        vuid = self.GetVuid(vuid_name_tag, "%s-parameter" % (value.name))
                        enum_value_list = 'All%sEnums' % value.type
                        usedLines.append('skip |= validate_ranged_enum("{}", {ppp}"{}"{pps}, "{}", {}, {}{}, {});\n'.format(funcName, valueDisplayName, value.type, enum_value_list, valuePrefix, value.name, vuid, **postProcSpec))
                    # If this is a struct, see if it contains members that need to be checked
                    if value.type in self.validatedStructs:
                        memberNamePrefix = '{}{}.'.format(valuePrefix, value.name)
                        memberDisplayNamePrefix = '{}.'.format(valueDisplayName)
                        usedLines.append(self.expandStructCode(value.type, funcName, memberNamePrefix, memberDisplayNamePrefix, '', [], postProcSpec))
            # Append the parameter check to the function body for the current command
            if usedLines:
                # Apply special conditional checks
                if value.condition:
                    usedLines = self.genConditionalCall(valuePrefix, value.condition, usedLines)
                lines += usedLines
            elif not value.iscount:
                # If no expression was generated for this value, it is unreferenced by the validation function, unless
                # it is an array count, which is indirectly referenced for array valiadation.
                unused.append(value.name)
        if not lines:
            lines.append('// No xml-driven validation\n')
        return lines, unused
    #
    # Generate the struct member check code from the captured data
    def processStructMemberData(self):
        indent = self.incIndent(None)
        for struct in self.structMembers:
            #
            # The string returned by genFuncBody will be nested in an if check for a NULL pointer, so needs its indent incremented
            lines, unused = self.genFuncBody('{funcName}', struct.members, '{valuePrefix}', '{displayNamePrefix}', struct.name)
            if lines:
                self.validatedStructs[struct.name] = lines
    #
    # Generate the command param check code from the captured data
    def processCmdData(self):
        indent = self.incIndent(None)
        for command in self.commands:
            # Skip first parameter if it is a dispatch handle (everything except vkCreateInstance)
            startIndex = 0 if command.name == 'vkCreateInstance' else 1
            lines, unused = self.genFuncBody(command.name, command.params[startIndex:], '', '', None)
            # Cannot validate extension dependencies for device extension APIs having a physical device as their dispatchable object
            if (command.name in self.required_extensions) and (self.extension_type != 'device' or command.params[0].type != 'VkPhysicalDevice'):
                ext_test = ''
                if command.params[0].type in ["VkInstance", "VkPhysicalDevice"] or command.name == 'vkCreateInstance':
                    ext_table_type = 'instance'
                else:
                    ext_table_type = 'device'
                for ext in self.required_extensions[command.name]:
                    ext_name_define = ''
                    ext_enable_name = ''
                    for extension in self.registry.extensions:
                        if extension.attrib['name'] == ext:
                            ext_name_define = extension[0][1].get('name')
                            ext_enable_name = ext_name_define.lower()
                            ext_enable_name = re.sub('_extension_name', '', ext_enable_name)
                            break
                    ext_test = 'if (!%s_extensions.%s) skip |= OutputExtensionError("%s", %s);\n' % (ext_table_type, ext_enable_name, command.name, ext_name_define)
                    lines.insert(0, ext_test)
            if lines:
                func_sig = self.getCmdDef(command) + ' {\n'
                func_sig = func_sig.split('VKAPI_CALL vk')[1]
                cmdDef = 'bool StatelessValidation::PreCallValidate' + func_sig
                cmdDef += '%sbool skip = false;\n' % indent
                for line in lines:
                    if type(line) is list:
                        for sub in line:
                            cmdDef += indent + sub
                    else:
                        cmdDef += indent + line
                # Insert call to custom-written function if present
                if command.name in self.functions_with_manual_checks:
                    # Generate parameter list for manual fcn and down-chain calls
                    params_text = ''
                    for param in command.params:
                        params_text += '%s, ' % param.name
                    params_text = params_text[:-2] + ');\n'
                    cmdDef += '    if (!skip) skip |= manual_PreCallValidate'+ command.name[2:] + '(' + params_text
                cmdDef += '%sreturn skip;\n' % indent
                cmdDef += '}\n'
                self.validation.append(cmdDef)
