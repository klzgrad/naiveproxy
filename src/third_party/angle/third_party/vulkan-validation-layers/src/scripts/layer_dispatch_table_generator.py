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
# Author: Mark Young <marky@lunarg.com>
# Author: Mark Lobodzinski <mark@lunarg.com>

import os,re,sys
import xml.etree.ElementTree as etree
from generator import *
from collections import namedtuple
from common_codegen import *

ADD_INST_CMDS = ['vkCreateInstance',
                 'vkEnumerateInstanceExtensionProperties',
                 'vkEnumerateInstanceLayerProperties',
                 'vkEnumerateInstanceVersion']

#
# LayerDispatchTableGeneratorOptions - subclass of GeneratorOptions.
class LayerDispatchTableGeneratorOptions(GeneratorOptions):
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
                 indentFuncProto = True,
                 indentFuncPointer = False,
                 alignFuncParam = 0,
                 expandEnumerants = True):
        GeneratorOptions.__init__(self, conventions, filename, directory, apiname, profile,
                                  versions, emitversions, defaultExtensions,
                                  addExtensions, removeExtensions, emitExtensions, sortProcedure)
        self.prefixText      = prefixText
        self.prefixText      = None
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.alignFuncParam  = alignFuncParam
        self.expandEnumerants = expandEnumerants

#
# LayerDispatchTableOutputGenerator - subclass of OutputGenerator.
# Generates dispatch table helper header files for LVL
class LayerDispatchTableOutputGenerator(OutputGenerator):
    """Generate dispatch tables header based on XML element attributes"""
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)

        # Internal state - accumulators for different inner block text
        self.ext_instance_dispatch_list = []  # List of extension entries for instance dispatch list
        self.ext_device_dispatch_list = []    # List of extension entries for device dispatch list
        self.core_commands = []               # List of CommandData records for core Vulkan commands
        self.ext_commands = []                # List of CommandData records for extension Vulkan commands
        self.CommandParam = namedtuple('CommandParam', ['type', 'name', 'cdecl'])
        self.CommandData = namedtuple('CommandData', ['name', 'ext_name', 'ext_type', 'protect', 'return_type', 'handle_type', 'params', 'cdecl'])

    #
    # Called once at the beginning of each run
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)

        # Initialize members that require the tree
        self.handle_types = GetHandleTypes(self.registry.tree)

        # User-supplied prefix text, if any (list of strings)
        if (genOpts.prefixText):
            for s in genOpts.prefixText:
                write(s, file=self.outFile)

        # File Comment
        file_comment = '// *** THIS FILE IS GENERATED - DO NOT EDIT ***\n'
        file_comment += '// See layer_dispatch_table_generator.py for modifications\n'
        write(file_comment, file=self.outFile)

        # Copyright Notice
        copyright =  '/*\n'
        copyright += ' * Copyright (c) 2015-2019 The Khronos Group Inc.\n'
        copyright += ' * Copyright (c) 2015-2019 Valve Corporation\n'
        copyright += ' * Copyright (c) 2015-2019 LunarG, Inc.\n'
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
        copyright += ' * Author: Mark Young <marky@lunarg.com>\n'
        copyright += ' */\n'

        preamble = ''
        if self.genOpts.filename == 'vk_layer_dispatch_table.h':
            preamble += '#pragma once\n'
            preamble += '\n'
            preamble += 'typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_GetPhysicalDeviceProcAddr)(VkInstance instance, const char* pName);\n'

        write(copyright, file=self.outFile)
        write(preamble, file=self.outFile)

    #
    # Write generate and write dispatch tables to output file
    def endFile(self):
        file_data = ''
        if self.genOpts.filename == 'vk_layer_dispatch_table.h':
            file_data += self.OutputLayerInstanceDispatchTable()
            file_data += self.OutputLayerDeviceDispatchTable()

        write(file_data, file=self.outFile);

        # Finish processing in superclass
        OutputGenerator.endFile(self)

    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        self.featureExtraProtect = GetFeatureProtect(interface)

        enums = interface[0].findall('enum')
        self.currentExtension = ''

        self.type = interface.get('type')
        self.num_commands = 0
        name = interface.get('name')
        self.currentExtension = name

    #
    # Process commands, adding to appropriate dispatch tables
    def genCmd(self, cmdinfo, name, alias):
        OutputGenerator.genCmd(self, cmdinfo, name, alias)

        # Get first param type
        params = cmdinfo.elem.findall('param')
        info = self.getTypeNameTuple(params[0])

        self.num_commands += 1

        if 'android' not in name:
            self.AddCommandToDispatchList(self.currentExtension, self.type, name, cmdinfo, info[0])

    def endFeature(self):
        # Finish processing in superclass
        OutputGenerator.endFeature(self)

    #
    # Retrieve the value of the len tag
    def getLen(self, param):
        result = None
        len = param.attrib.get('len')
        if len and len != 'null-terminated':
            # For string arrays, 'len' can look like 'count,null-terminated',
            # indicating that we have a null terminated array of strings.  We
            # strip the null-terminated from the 'len' field and only return
            # the parameter specifying the string count
            if 'null-terminated' in len:
                result = len.split(',')[0]
            else:
                result = len
            result = str(result).replace('::', '->')
        return result

    #
    # Determine if this API should be ignored or added to the instance or device dispatch table
    def AddCommandToDispatchList(self, extension_name, extension_type, name, cmdinfo, handle_type):
        return_type =  cmdinfo.elem.find('proto/type')
        if (return_type is not None and return_type.text == 'void'):
           return_type = None

        cmd_params = []

        # Generate a list of commands for use in printing the necessary
        # core instance terminator prototypes
        params = cmdinfo.elem.findall('param')
        lens = set()
        for param in params:
            len = self.getLen(param)
            if len:
                lens.add(len)
        paramsInfo = []
        for param in params:
            paramInfo = self.getTypeNameTuple(param)
            param_type = paramInfo[0]
            param_name = paramInfo[1]
            param_cdecl = self.makeCParamDecl(param, 0)
            cmd_params.append(self.CommandParam(type=param_type, name=param_name,
                                                cdecl=param_cdecl))

        if handle_type in self.handle_types and handle_type != 'VkInstance' and handle_type != 'VkPhysicalDevice':
            # The Core Vulkan code will be wrapped in a feature called VK_VERSION_#_#
            # For example: VK_VERSION_1_0 wraps the core 1.0 Vulkan functionality
            if 'VK_VERSION_' in extension_name:
                self.core_commands.append(
                    self.CommandData(name=name, ext_name=extension_name,
                                     ext_type='device',
                                     protect=self.featureExtraProtect,
                                     return_type = return_type,
                                     handle_type = handle_type,
                                     params = cmd_params,
                                     cdecl=self.makeCDecls(cmdinfo.elem)[0]))
            else:
                self.ext_device_dispatch_list.append((name, self.featureExtraProtect))
                self.ext_commands.append(
                    self.CommandData(name=name, ext_name=extension_name,
                                     ext_type=extension_type,
                                     protect=self.featureExtraProtect,
                                     return_type = return_type,
                                     handle_type = handle_type,
                                     params = cmd_params,
                                     cdecl=self.makeCDecls(cmdinfo.elem)[0]))
        else:
            # The Core Vulkan code will be wrapped in a feature called VK_VERSION_#_#
            # For example: VK_VERSION_1_0 wraps the core 1.0 Vulkan functionality
            if 'VK_VERSION_' in extension_name:
                self.core_commands.append(
                    self.CommandData(name=name, ext_name=extension_name,
                                     ext_type='instance',
                                     protect=self.featureExtraProtect,
                                     return_type = return_type,
                                     handle_type = handle_type,
                                     params = cmd_params,
                                     cdecl=self.makeCDecls(cmdinfo.elem)[0]))

            else:
                self.ext_instance_dispatch_list.append((name, self.featureExtraProtect))
                self.ext_commands.append(
                    self.CommandData(name=name, ext_name=extension_name,
                                     ext_type=extension_type,
                                     protect=self.featureExtraProtect,
                                     return_type = return_type,
                                     handle_type = handle_type,
                                     params = cmd_params,
                                     cdecl=self.makeCDecls(cmdinfo.elem)[0]))

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
    # Create a layer instance dispatch table from the appropriate list and return it as a string
    def OutputLayerInstanceDispatchTable(self):
        commands = []
        table = ''
        cur_extension_name = ''

        table += '// Instance function pointer dispatch table\n'
        table += 'typedef struct VkLayerInstanceDispatchTable_ {\n'

        # First add in an entry for GetPhysicalDeviceProcAddr.  This will not
        # ever show up in the XML or header, so we have to manually add it.
        table += '    // Manually add in GetPhysicalDeviceProcAddr entry\n'
        table += '    PFN_GetPhysicalDeviceProcAddr GetPhysicalDeviceProcAddr;\n'

        for x in range(0, 2):
            if x == 0:
                commands = self.core_commands
            else:
                commands = self.ext_commands

            for cur_cmd in commands:
                is_inst_handle_type = cur_cmd.name in ADD_INST_CMDS or cur_cmd.handle_type == 'VkInstance' or cur_cmd.handle_type == 'VkPhysicalDevice'
                if is_inst_handle_type:

                    if cur_cmd.ext_name != cur_extension_name:
                        if 'VK_VERSION_' in cur_cmd.ext_name:
                            table += '\n    // ---- Core %s commands\n' % cur_cmd.ext_name[11:]
                        else:
                            table += '\n    // ---- %s extension commands\n' % cur_cmd.ext_name
                        cur_extension_name = cur_cmd.ext_name

                    # Remove 'vk' from proto name
                    base_name = cur_cmd.name[2:]

                    if cur_cmd.protect is not None:
                        table += '#ifdef %s\n' % cur_cmd.protect

                    table += '    PFN_%s %s;\n' % (cur_cmd.name, base_name)

                    if cur_cmd.protect is not None:
                        table += '#endif // %s\n' % cur_cmd.protect

        table += '} VkLayerInstanceDispatchTable;\n\n'
        return table

    #
    # Create a layer device dispatch table from the appropriate list and return it as a string
    def OutputLayerDeviceDispatchTable(self):
        commands = []
        table = ''
        cur_extension_name = ''

        table += '// Device function pointer dispatch table\n'
        table += 'typedef struct VkLayerDispatchTable_ {\n'

        for x in range(0, 2):
            if x == 0:
                commands = self.core_commands
            else:
                commands = self.ext_commands

            for cur_cmd in commands:
                is_inst_handle_type = cur_cmd.name in ADD_INST_CMDS or cur_cmd.handle_type == 'VkInstance' or cur_cmd.handle_type == 'VkPhysicalDevice'
                if not is_inst_handle_type:

                    if cur_cmd.ext_name != cur_extension_name:
                        if 'VK_VERSION_' in cur_cmd.ext_name:
                            table += '\n    // ---- Core %s commands\n' % cur_cmd.ext_name[11:]
                        else:
                            table += '\n    // ---- %s extension commands\n' % cur_cmd.ext_name
                        cur_extension_name = cur_cmd.ext_name

                    # Remove 'vk' from proto name
                    base_name = cur_cmd.name[2:]

                    if cur_cmd.protect is not None:
                        table += '#ifdef %s\n' % cur_cmd.protect

                    table += '    PFN_%s %s;\n' % (cur_cmd.name, base_name)

                    if cur_cmd.protect is not None:
                        table += '#endif // %s\n' % cur_cmd.protect

        table += '} VkLayerDispatchTable;\n\n'
        return table
