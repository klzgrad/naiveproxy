#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2020 The Khronos Group Inc.
# Copyright (c) 2015-2020 Valve Corporation
# Copyright (c) 2015-2020 LunarG, Inc.
# Copyright (c) 2019-2020 Intel Corporation
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
# Author: Lionel Landwerlin <lionel.g.landwerlin@intel.com>

import os,re,sys
import xml.etree.ElementTree as etree
from generator import *
from collections import namedtuple
from common_codegen import *

#
# CommandCounterOutputGeneratorOptions - subclass of GeneratorOptions.
class CommandCounterOutputGeneratorOptions(GeneratorOptions):
    def __init__(self,
                 conventions = None,
                 filename = None,
                 directory = '.',
                 genpath = None,
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
                 apicall = '',
                 apientry = '',
                 apientryp = '',
                 alignFuncParam = 0,
                 expandEnumerants = True,
                 lvt_file_type = ''):
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
        self.prefixText      = prefixText
        self.genFuncPointers = genFuncPointers
        self.prefixText      = None
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.alignFuncParam  = alignFuncParam
        self.lvt_file_type   = lvt_file_type
#
# CommandCounterOutputGenerator - subclass of OutputGenerator.
# Generates files needed by the layer validation state tracker
class CommandCounterOutputGenerator(OutputGenerator):
    """Generate command counter in VkCommandBuffer based on XML element attributes"""
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        # Internal state - accumulators for different inner block text
        self.dispatch_list = []               # List of entries for dispatch list
    #
    # Called once at the beginning of each run
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)

        # Initialize members that require the tree
        self.handle_types = GetHandleTypes(self.registry.tree)
        self.lvt_file_type = genOpts.lvt_file_type

        if genOpts.lvt_file_type == 'function_pointer_header':
            write("#pragma once", file=self.outFile)

        # User-supplied prefix text, if any (list of strings)
        if (genOpts.prefixText):
            for s in genOpts.prefixText:
                write(s, file=self.outFile)
        # File Comment
        file_comment = '// *** THIS FILE IS GENERATED - DO NOT EDIT ***\n'
        file_comment += '// See command_counter_generator.py for modifications\n'
        write(file_comment, file=self.outFile)
        # Copyright Notice
        copyright =  '/*\n'
        copyright += ' * Copyright (c) 2015-2020 The Khronos Group Inc.\n'
        copyright += ' * Copyright (c) 2015-2020 Valve Corporation\n'
        copyright += ' * Copyright (c) 2015-2020 LunarG, Inc.\n'
        copyright += ' * Copyright (c) 2019-2020 Intel Corporation\n'
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
        copyright += ' * Author: Lionel Landwerlin <lionel.g.landwerlin@intel.com>\n'
        copyright += ' */\n'
        write(copyright, file=self.outFile)
    #
    # Write completed source code to output file
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
    # Processing at beginning of each feature or extension
    def beginFeature(self, interface, emit):
        OutputGenerator.beginFeature(self, interface, emit)
        self.featureExtraProtect = GetFeatureProtect(interface)

    #
    # Process commands, adding to dispatch list
    def genCmd(self, cmdinfo, name, alias):
        OutputGenerator.genCmd(self, cmdinfo, name, alias)
        # Get first param type
        params = cmdinfo.elem.findall('param')
        info = self.getTypeNameTuple(params[0])
        if name.startswith('vkCmd') and info[0] == 'VkCommandBuffer':
            self.dispatch_list.append((self.featureExtraProtect, name, cmdinfo))

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
    # Create the test function pointer source and return it as a string
    def GenerateFunctionPointerSource(self):
        entries = []
        entries = self.dispatch_list

        table = '#include "chassis.h"\n'
        table += '#include "state_tracker.h"\n'
        table += '#include "command_counter.h"\n'
        table += '\n'

        for item in entries:
            # Remove 'vk' from proto name
            base_name = item[1][2:]

            if item[0] is not None:
                table += '#ifdef %s\n' % item[0]
            params = item[2].elem.findall('param')
            paramstext = ', '.join([''.join(param.itertext()) for param in params])
            table += 'void CommandCounter::PreCallRecord%s(%s) {\n' % (base_name, paramstext)
            table += '    coreChecks->IncrementCommandCount(%s);\n' % params[0].findall('name')[0].text
            table += '}\n'
            if item[0] is not None:
                table += '#endif // %s\n' % item[0]
        return table
    #
    # Create the test function pointer source and return it as a string
    def GenerateFunctionPointerHeader(self):
        entries = []
        table = ''
        entries = self.dispatch_list

        for item in entries:
            # Remove 'vk' from proto name
            base_name = item[1][2:]

            if item[0] is not None:
                table += '#ifdef %s\n' % item[0]
            params = item[2].elem.findall('param')
            paramstext = ', '.join([''.join(param.itertext()) for param in params])
            table += 'void PreCallRecord%s(%s);\n' % (base_name, paramstext)
            if item[0] is not None:
                table += '#endif // %s\n' % item[0]
        return table

    # Create a helper file and return it as a string
    def OutputDestFile(self):
        if self.lvt_file_type == 'function_pointer_header':
            return self.GenerateFunctionPointerHeader()
        elif self.lvt_file_type == 'function_pointer_source':
            return self.GenerateFunctionPointerSource()
        else:
            return 'Bad LVT File Generator Option %s' % self.lvt_file_type
