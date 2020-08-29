#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2020 The Khronos Group Inc.
# Copyright (c) 2015-2020 Valve Corporation
# Copyright (c) 2015-2020 LunarG, Inc.
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

import os,re,sys
import xml.etree.ElementTree as etree
from generator import *
from collections import namedtuple
from common_codegen import *

funcptr_source_preamble = '''
#include "lvt_function_pointers.h"
#include <stdio.h>

namespace vk {

'''

funcptr_header_preamble = '''
#include <vulkan/vulkan.h>
#include "vk_loader_platform.h"

namespace vk {

'''

#
# LvtFileOutputGeneratorOptions - subclass of GeneratorOptions.
class LvtFileOutputGeneratorOptions(GeneratorOptions):
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
# LvtFileOutputGenerator - subclass of OutputGenerator.
# Generates files needed by the layer validation tests
class LvtFileOutputGenerator(OutputGenerator):
    """Generate LVT support files based on XML element attributes"""
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
        file_comment += '// See lvt_file_generator.py for modifications\n'
        write(file_comment, file=self.outFile)
        # Copyright Notice
        copyright =  '/*\n'
        copyright += ' * Copyright (c) 2015-2020 The Khronos Group Inc.\n'
        copyright += ' * Copyright (c) 2015-2020 Valve Corporation\n'
        copyright += ' * Copyright (c) 2015-2020 LunarG, Inc.\n'
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
        self.AddCommandToDispatchList(name, info[0], self.featureExtraProtect, cmdinfo)

    #
    # Determine if this API should be ignored or added to the funcptr list
    def AddCommandToDispatchList(self, name, handle_type, protect, cmdinfo):
        WSI_mandatory_extensions = [
            'VK_KHR_win32_surface',
            'VK_KHR_xcb_surface',
            'VK_KHR_xlib_surface',
            'VK_KHR_wayland_surface',
            'VK_MVK_macos_surface',
            'VK_KHR_surface',
            'VK_KHR_swapchain',
            'VK_KHR_display',
            'VK_KHR_android_surface',
            ]
        if 'VK_VERSION' in self.featureName or self.featureName in WSI_mandatory_extensions:
            self.dispatch_list.append((name, self.featureExtraProtect))
        return
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
        table = funcptr_source_preamble
        entries = self.dispatch_list

        for item in entries:
            # Remove 'vk' from proto name
            base_name = item[0][2:]
            if item[1] is not None:
                table += '#ifdef %s\n' % item[1]
            table += 'PFN_%s %s;\n' % (item[0], base_name)
            if item[1] is not None:
                table += '#endif // %s\n' % item[1]

        table += '\n\n'
        table += 'void InitDispatchTable() {\n'
        table += '\n'
        table += '#if(WIN32)\n'
        table += '    const char filename[] = "vulkan-1.dll";\n'
        table += '#elif(__APPLE__)\n'
        table += '    const char filename[] = "libvulkan.dylib";\n'
        table += '#else\n'
        table += '    const char filename[] = "libvulkan.so";\n'
        table += '#endif\n'
        table += '\n'
        table += '    auto loader_handle = loader_platform_open_library(filename);\n'
        table += '\n'
        table += '    if (loader_handle == nullptr) {\n'
        table += '        printf("%s\\n", loader_platform_open_library_error(filename));\n'
        table += '        exit(1);\n'
        table += '    }\n\n'

        for item in entries:
            # Remove 'vk' from proto name
            base_name = item[0][2:]

            if item[1] is not None:
                table += '#ifdef %s\n' % item[1]
            table += '    %s = reinterpret_cast<PFN_%s>(loader_platform_get_proc_address(loader_handle, "%s"));\n' % (base_name, item[0], item[0])
            if item[1] is not None:
                table += '#endif // %s\n' % item[1]
        table += '}\n\n'
        table += '} // namespace vk'
        return table
    #
    # Create the test function pointer source and return it as a string
    def GenerateFunctionPointerHeader(self):
        entries = []
        table = funcptr_header_preamble
        entries = self.dispatch_list

        for item in entries:
            # Remove 'vk' from proto name
            base_name = item[0][2:]
            if item[1] is not None:
                table += '#ifdef %s\n' % item[1]
            table += 'extern PFN_%s %s;\n' % (item[0], base_name)
            if item[1] is not None:
                table += '#endif // %s\n' % item[1]
        table += '\n'
        table += 'void InitDispatchTable();\n\n'
        table += '} // namespace vk'
        return table

    # Create a helper file and return it as a string
    def OutputDestFile(self):
        if self.lvt_file_type == 'function_pointer_header':
            return self.GenerateFunctionPointerHeader()
        elif self.lvt_file_type == 'function_pointer_source':
            return self.GenerateFunctionPointerSource()
        else:
            return 'Bad LVT File Generator Option %s' % self.lvt_file_type
