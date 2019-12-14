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
# Author: Dave Houlton <daveh@lunarg.com>

import os,re,sys,string,json
import xml.etree.ElementTree as etree
from generator import *
from collections import namedtuple
from common_codegen import *

# This is a workaround to use a Python 2.7 and 3.x compatible syntax.
from io import open

# ObjectTrackerGeneratorOptions - subclass of GeneratorOptions.
#
# Adds options used by ObjectTrackerOutputGenerator objects during
# object_tracker layer generation.
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
class ObjectTrackerGeneratorOptions(GeneratorOptions):
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
                 expandEnumerants = True,
                 valid_usage_path = ''):
        GeneratorOptions.__init__(self, conventions, filename, directory, apiname, profile,
                                  versions, emitversions, defaultExtensions,
                                  addExtensions, removeExtensions, emitExtensions, sortProcedure)
        self.prefixText      = prefixText
        self.genFuncPointers = genFuncPointers
        self.protectFile     = protectFile
        self.protectFeature  = protectFeature
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam  = alignFuncParam
        self.expandEnumerants = expandEnumerants
        self.valid_usage_path = valid_usage_path


# ObjectTrackerOutputGenerator - subclass of OutputGenerator.
# Generates object_tracker layer object validation code
#
# ---- methods ----
# ObjectTrackerOutputGenerator(errFile, warnFile, diagFile) - args as for OutputGenerator. Defines additional internal state.
# ---- methods overriding base class ----
# beginFile(genOpts)
# endFile()
# beginFeature(interface, emit)
# endFeature()
# genCmd(cmdinfo)
# genStruct()
# genType()
class ObjectTrackerOutputGenerator(OutputGenerator):
    """Generate ObjectTracker code based on XML element attributes"""
    # This is an ordered list of sections in the header file.
    ALL_SECTIONS = ['command']
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        self.INDENT_SPACES = 4
        self.prototypes = []
        self.instance_extensions = []
        self.device_extensions = []
        # Commands which are not autogenerated but still intercepted
        self.no_autogen_list = [
            'vkDestroyInstance',
            'vkCreateInstance',
            'vkEnumeratePhysicalDevices',
            'vkGetPhysicalDeviceQueueFamilyProperties',
            'vkGetPhysicalDeviceQueueFamilyProperties2',
            'vkGetPhysicalDeviceQueueFamilyProperties2KHR',
            'vkGetDeviceQueue',
            'vkGetDeviceQueue2',
            'vkCreateDescriptorSetLayout',
            'vkDestroyDescriptorPool',
            'vkDestroyCommandPool',
            'vkAllocateCommandBuffers',
            'vkAllocateDescriptorSets',
            'vkFreeDescriptorSets',
            'vkFreeCommandBuffers',
            'vkUpdateDescriptorSets',
            'vkBeginCommandBuffer',
            'vkGetDescriptorSetLayoutSupport',
            'vkGetDescriptorSetLayoutSupportKHR',
            'vkDestroySwapchainKHR',
            'vkGetSwapchainImagesKHR',
            'vkCmdPushDescriptorSetKHR',
            'vkDestroyDevice',
            'vkResetDescriptorPool',
            'vkGetPhysicalDeviceDisplayPropertiesKHR',
            'vkGetPhysicalDeviceDisplayProperties2KHR',
            'vkGetDisplayModePropertiesKHR',
            'vkGetDisplayModeProperties2KHR',
            'vkAcquirePerformanceConfigurationINTEL',
            'vkReleasePerformanceConfigurationINTEL',
            'vkQueueSetPerformanceConfigurationINTEL',
            'vkCreateFramebuffer',
            'vkSetDebugUtilsObjectNameEXT',
            'vkSetDebugUtilsObjectTagEXT',
            ]
        # These VUIDS are not implicit, but are best handled in this layer. Codegen for vkDestroy calls will generate a key
        # which is translated here into a good VU.  Saves ~40 checks.
        self.manual_vuids = dict()
        self.manual_vuids = {
            "fence-compatalloc": "\"VUID-vkDestroyFence-fence-01121\"",
            "fence-nullalloc": "\"VUID-vkDestroyFence-fence-01122\"",
            "event-compatalloc": "\"VUID-vkDestroyEvent-event-01146\"",
            "event-nullalloc": "\"VUID-vkDestroyEvent-event-01147\"",
            "buffer-compatalloc": "\"VUID-vkDestroyBuffer-buffer-00923\"",
            "buffer-nullalloc": "\"VUID-vkDestroyBuffer-buffer-00924\"",
            "image-compatalloc": "\"VUID-vkDestroyImage-image-01001\"",
            "image-nullalloc": "\"VUID-vkDestroyImage-image-01002\"",
            "shaderModule-compatalloc": "\"VUID-vkDestroyShaderModule-shaderModule-01092\"",
            "shaderModule-nullalloc": "\"VUID-vkDestroyShaderModule-shaderModule-01093\"",
            "pipeline-compatalloc": "\"VUID-vkDestroyPipeline-pipeline-00766\"",
            "pipeline-nullalloc": "\"VUID-vkDestroyPipeline-pipeline-00767\"",
            "sampler-compatalloc": "\"VUID-vkDestroySampler-sampler-01083\"",
            "sampler-nullalloc": "\"VUID-vkDestroySampler-sampler-01084\"",
            "renderPass-compatalloc": "\"VUID-vkDestroyRenderPass-renderPass-00874\"",
            "renderPass-nullalloc": "\"VUID-vkDestroyRenderPass-renderPass-00875\"",
            "descriptorUpdateTemplate-compatalloc": "\"VUID-vkDestroyDescriptorUpdateTemplate-descriptorSetLayout-00356\"",
            "descriptorUpdateTemplate-nullalloc": "\"VUID-vkDestroyDescriptorUpdateTemplate-descriptorSetLayout-00357\"",
            "imageView-compatalloc": "\"VUID-vkDestroyImageView-imageView-01027\"",
            "imageView-nullalloc": "\"VUID-vkDestroyImageView-imageView-01028\"",
            "pipelineCache-compatalloc": "\"VUID-vkDestroyPipelineCache-pipelineCache-00771\"",
            "pipelineCache-nullalloc": "\"VUID-vkDestroyPipelineCache-pipelineCache-00772\"",
            "pipelineLayout-compatalloc": "\"VUID-vkDestroyPipelineLayout-pipelineLayout-00299\"",
            "pipelineLayout-nullalloc": "\"VUID-vkDestroyPipelineLayout-pipelineLayout-00300\"",
            "descriptorSetLayout-compatalloc": "\"VUID-vkDestroyDescriptorSetLayout-descriptorSetLayout-00284\"",
            "descriptorSetLayout-nullalloc": "\"VUID-vkDestroyDescriptorSetLayout-descriptorSetLayout-00285\"",
            "semaphore-compatalloc": "\"VUID-vkDestroySemaphore-semaphore-01138\"",
            "semaphore-nullalloc": "\"VUID-vkDestroySemaphore-semaphore-01139\"",
            "queryPool-compatalloc": "\"VUID-vkDestroyQueryPool-queryPool-00794\"",
            "queryPool-nullalloc": "\"VUID-vkDestroyQueryPool-queryPool-00795\"",
            "bufferView-compatalloc": "\"VUID-vkDestroyBufferView-bufferView-00937\"",
            "bufferView-nullalloc": "\"VUID-vkDestroyBufferView-bufferView-00938\"",
            "surface-compatalloc": "\"VUID-vkDestroySurfaceKHR-surface-01267\"",
            "surface-nullalloc": "\"VUID-vkDestroySurfaceKHR-surface-01268\"",
            "framebuffer-compatalloc": "\"VUID-vkDestroyFramebuffer-framebuffer-00893\"",
            "framebuffer-nullalloc": "\"VUID-vkDestroyFramebuffer-framebuffer-00894\"",
            "VkGraphicsPipelineCreateInfo-basePipelineHandle": "\"VUID-VkGraphicsPipelineCreateInfo-flags-00722\"",
            "VkComputePipelineCreateInfo-basePipelineHandle": "\"VUID-VkComputePipelineCreateInfo-flags-00697\"",
            "VkRayTracingPipelineCreateInfoNV-basePipelineHandle": "\"VUID-VkRayTracingPipelineCreateInfoNV-flags-02404\"",
           }

        # Commands shadowed by interface functions and are not implemented
        self.interface_functions = [
            ]
        self.headerVersion = None
        # Internal state - accumulators for different inner block text
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])
        self.cmd_list = []             # list of commands processed to maintain ordering
        self.cmd_info_dict = {}        # Per entry-point data for code generation and validation
        self.structMembers = []        # List of StructMemberData records for all Vulkan structs
        self.extension_structs = []    # List of all structs or sister-structs containing handles
                                       # A sister-struct may contain no handles but shares <validextensionstructs> with one that does
        self.structTypes = dict()      # Map of Vulkan struct typename to required VkStructureType
        self.struct_member_dict = dict()
        # Named tuples to store struct and command data
        self.StructType = namedtuple('StructType', ['name', 'value'])
        self.CmdInfoData = namedtuple('CmdInfoData', ['name', 'cmdinfo', 'members', 'extra_protect', 'alias', 'iscreate', 'isdestroy', 'allocator'])
        self.CommandParam = namedtuple('CommandParam', ['type', 'name', 'isconst', 'isoptional', 'iscount', 'iscreate', 'len', 'extstructs', 'cdecl', 'islocal'])
        self.StructMemberData = namedtuple('StructMemberData', ['name', 'members'])
        self.object_types = []         # List of all handle types
        self.valid_vuids = set()       # Set of all valid VUIDs
        self.vuid_dict = dict()        # VUID dictionary (from JSON)
    #
    # Check if the parameter passed in is optional
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
        if not isoptional:
            # Matching logic in parameter validation and ValidityOutputGenerator.isHandleOptional
            optString = param.attrib.get('noautovalidity')
            if optString and optString == 'true':
                isoptional = True;
        return isoptional
    #
    # Get VUID identifier from implicit VUID tag
    def GetVuid(self, parent, suffix):
        vuid_string = 'VUID-%s-%s' % (parent, suffix)
        vuid = "kVUIDUndefined"
        if '->' in vuid_string:
           return vuid
        if vuid_string in self.valid_vuids:
            vuid = "\"%s\"" % vuid_string
        else:
            alias =  self.cmd_info_dict[parent].alias if parent in self.cmd_info_dict else None
            if alias:
                alias_string = 'VUID-%s-%s' % (alias, suffix)
                if alias_string in self.valid_vuids:
                    vuid = "\"%s\"" % alias_string
        return vuid
    #
    # Increases indent by 4 spaces and tracks it globally
    def incIndent(self, indent):
        inc = ' ' * self.INDENT_SPACES
        if indent:
            return indent + inc
        return inc
    #
    # Decreases indent by 4 spaces and tracks it globally
    def decIndent(self, indent):
        if indent and (len(indent) > self.INDENT_SPACES):
            return indent[:-self.INDENT_SPACES]
        return ''
    #
    # Override makeProtoName to drop the "vk" prefix
    def makeProtoName(self, name, tail):
        return self.genOpts.apientry + name[2:] + tail
    #
    # Check if the parameter passed in is a pointer to an array
    def paramIsArray(self, param):
        return param.attrib.get('len') is not None

    #
    # Generate the object tracker undestroyed object validation function
    def GenReportFunc(self):
        output_func = ''
        for objtype in ['instance', 'device']:
            upper_objtype = objtype.capitalize();
            output_func += 'bool ObjectLifetimes::ReportUndestroyed%sObjects(Vk%s %s, const std::string& error_code) {\n' % (upper_objtype, upper_objtype, objtype)
            output_func += '    bool skip = false;\n'
            if objtype == 'device':
                output_func += '    skip |= ReportLeaked%sObjects(%s, kVulkanObjectTypeCommandBuffer, error_code);\n' % (upper_objtype, objtype)
            for handle in self.object_types:
                if self.handle_types.IsNonDispatchable(handle):
                    if (objtype == 'device' and self.handle_parents.IsParentDevice(handle)) or (objtype == 'instance' and not self.handle_parents.IsParentDevice(handle)):
                        output_func += '    skip |= ReportLeaked%sObjects(%s, %s, error_code);\n' % (upper_objtype, objtype, self.GetVulkanObjType(handle))
            output_func += '    return skip;\n'
            output_func += '}\n'
        return output_func

    #
    # Generate the object tracker undestroyed object destruction function
    def GenDestroyFunc(self):
        output_func = ''
        for objtype in ['instance', 'device']:
            upper_objtype = objtype.capitalize();
            output_func += 'void ObjectLifetimes::DestroyLeaked%sObjects() {\n' % upper_objtype
            if objtype == 'device':
                output_func += '    DestroyUndestroyedObjects(kVulkanObjectTypeCommandBuffer);\n'
            for handle in self.object_types:
                if self.handle_types.IsNonDispatchable(handle):
                    if (objtype == 'device' and self.handle_parents.IsParentDevice(handle)) or (objtype == 'instance' and not self.handle_parents.IsParentDevice(handle)):
                        output_func += '    DestroyUndestroyedObjects(%s);\n' % self.GetVulkanObjType(handle)
            output_func += '}\n'

        return output_func

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
    # Separate content for validation source and header files
    def otwrite(self, dest, formatstring):
        if 'object_tracker.h' in self.genOpts.filename and (dest == 'hdr' or dest == 'both'):
            write(formatstring, file=self.outFile)
        elif 'object_tracker.cpp' in self.genOpts.filename and (dest == 'cpp' or dest == 'both'):
            write(formatstring, file=self.outFile)

    #
    # Called at beginning of processing as file is opened
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)

        # Initialize members that require the tree
        self.handle_types = GetHandleTypes(self.registry.tree)
        self.handle_parents = GetHandleParents(self.registry.tree)
        self.type_categories = GetTypeCategories(self.registry.tree)

        header_file = (genOpts.filename == 'object_tracker.h')
        source_file = (genOpts.filename == 'object_tracker.cpp')

        if not header_file and not source_file:
            print("Error: Output Filenames have changed, update generator source.\n")
            sys.exit(1)

        self.valid_usage_path = genOpts.valid_usage_path
        vu_json_filename = os.path.join(self.valid_usage_path + os.sep, 'validusage.json')
        if os.path.isfile(vu_json_filename):
            json_file = open(vu_json_filename, 'r')
            self.vuid_dict = json.load(json_file)
            json_file.close()
        if len(self.vuid_dict) == 0:
            print("Error: Could not find, or error loading %s/validusage.json\n", vu_json_filename)
            sys.exit(1)

        # Build a set of all vuid text strings found in validusage.json
        for json_vuid_string in self.ExtractVUIDs(self.vuid_dict):
            self.valid_vuids.add(json_vuid_string)

        # File Comment
        file_comment = '// *** THIS FILE IS GENERATED - DO NOT EDIT ***\n'
        file_comment += '// See object_tracker_generator.py for modifications\n'
        self.otwrite('both', file_comment)
        # Copyright Statement
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
        copyright += ' * Author: Dave Houlton <daveh@lunarg.com>\n'
        copyright += ' *\n'
        copyright += ' ****************************************************************************/\n'
        self.otwrite('both', copyright)
        self.newline()
        self.otwrite('cpp', '#include "chassis.h"')
        self.otwrite('cpp', '#include "object_lifetime_validation.h"')

    #
    # Now that the data is all collected and complete, generate and output the object validation routines
    def endFile(self):
        self.struct_member_dict = dict(self.structMembers)
        # Generate the list of APIs that might need to handle wrapped extension structs
        # self.GenerateCommandWrapExtensionList()
        self.WrapCommands()
        # Build undestroyed objects reporting function
        report_func = self.GenReportFunc()
        self.newline()
        # Build undestroyed objects destruction function
        destroy_func = self.GenDestroyFunc()
        self.otwrite('cpp', '\n')
        self.otwrite('cpp', '// ObjectTracker undestroyed objects validation function')
        self.otwrite('cpp', '%s' % report_func)
        self.otwrite('cpp', '%s' % destroy_func)
        # Actually write the interface to the output file.
        if (self.emit):
            self.newline()
            if self.featureExtraProtect is not None:
                prot = '#ifdef %s' % self.featureExtraProtect
                self.otwrite('both', '%s' % prot)
            # Write the object_tracker code to the  file
            if self.sections['command']:
                source = ('\n'.join(self.sections['command']))
                self.otwrite('both', '%s' % source)
            if (self.featureExtraProtect is not None):
                prot = '\n#endif // %s', self.featureExtraProtect
                self.otwrite('both', prot)
            else:
                self.otwrite('both', '\n')


        self.otwrite('hdr', 'void PostCallRecordDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator);')
        self.otwrite('hdr', 'void PreCallRecordResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags);')
        self.otwrite('hdr', 'void PostCallRecordGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);')
        self.otwrite('hdr', 'void PreCallRecordFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);')
        self.otwrite('hdr', 'void PreCallRecordFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets);')
        self.otwrite('hdr', 'void PostCallRecordGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR *pQueueFamilyProperties);')
        self.otwrite('hdr', 'void PostCallRecordGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR *pQueueFamilyProperties);')
        self.otwrite('hdr', 'void PostCallRecordGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount, VkDisplayPropertiesKHR *pProperties, VkResult result);')
        self.otwrite('hdr', 'void PostCallRecordGetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t *pPropertyCount, VkDisplayModePropertiesKHR *pProperties, VkResult result);')
        self.otwrite('hdr', 'void PostCallRecordGetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount, VkDisplayProperties2KHR *pProperties, VkResult result);')
        self.otwrite('hdr', 'void PostCallRecordGetDisplayModeProperties2KHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t *pPropertyCount, VkDisplayModeProperties2KHR *pProperties, VkResult result);')
        OutputGenerator.endFile(self)
    #
    # Processing point at beginning of each extension definition
    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        self.headerVersion = None
        self.featureExtraProtect = GetFeatureProtect(interface)

        if self.featureName != 'VK_VERSION_1_0' and self.featureName != 'VK_VERSION_1_1':
            white_list_entry = []
            if (self.featureExtraProtect is not None):
                white_list_entry += [ '#ifdef %s' % self.featureExtraProtect ]
            white_list_entry += [ '"%s"' % self.featureName ]
            if (self.featureExtraProtect is not None):
                white_list_entry += [ '#endif' ]
            featureType = interface.get('type')
            if featureType == 'instance':
                self.instance_extensions += white_list_entry
            elif featureType == 'device':
                self.device_extensions += white_list_entry
    #
    # Processing point at end of each extension definition
    def endFeature(self):
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    # Process enums, structs, etc.
    def genType(self, typeinfo, name, alias):
        OutputGenerator.genType(self, typeinfo, name, alias)
        typeElem = typeinfo.elem
        # If the type is a struct type, traverse the imbedded <member> tags generating a structure.
        # Otherwise, emit the tag text.
        category = typeElem.get('category')
        if (category == 'struct' or category == 'union'):
            self.genStruct(typeinfo, name, alias)
        if category == 'handle':
            self.object_types.append(name)
    #
    # Append a definition to the specified section
    def appendSection(self, section, text):
        # self.sections[section].append('SECTION: ' + section + '\n')
        self.sections[section].append(text)
    #
    # Check if the parameter passed in is a pointer
    def paramIsPointer(self, param):
        ispointer = False
        for elem in param:
            if elem.tag == 'type' and elem.tail is not None and '*' in elem.tail:
                ispointer = True
        return ispointer
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
            # Spec has now notation for len attributes, using :: instead of platform specific pointer symbol
            result = str(result).replace('::', '->')
        return result
    #
    # Generate a VkStructureType based on a structure typename
    def genVkStructureType(self, typename):
        # Add underscore between lowercase then uppercase
        value = re.sub('([a-z0-9])([A-Z])', r'\1_\2', typename)
        # Change to uppercase
        value = value.upper()
        # Add STRUCTURE_TYPE_
        return re.sub('VK_', 'VK_STRUCTURE_TYPE_', value)
    #
    # Struct parameter check generation.
    # This is a special case of the <type> tag where the contents are interpreted as a set of
    # <member> tags instead of freeform C type declarations. The <member> tags are just like
    # <param> tags - they are a declaration of a struct or union member. Only simple member
    # declarations are supported (no nested structs etc.)
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
            cdecl = self.makeCParamDecl(member, 0)
            # Process VkStructureType
            if type == 'VkStructureType':
                # Extract the required struct type value from the comments
                # embedded in the original text defining the 'typeinfo' element
                rawXml = etree.tostring(typeinfo.elem).decode('ascii')
                result = re.search(r'VK_STRUCTURE_TYPE_\w+', rawXml)
                if result:
                    value = result.group(0)
                else:
                    value = self.genVkStructureType(typeName)
                # Store the required type value
                self.structTypes[typeName] = self.StructType(name=name, value=value)
            # Store pointer/array/string info
            extstructs = member.attrib.get('validextensionstructs') if name == 'pNext' else None
            membersInfo.append(self.CommandParam(type=type,
                                                 name=name,
                                                 isconst=True if 'const' in cdecl else False,
                                                 isoptional=self.paramIsOptional(member),
                                                 iscount=True if name in lens else False,
                                                 len=self.getLen(member),
                                                 extstructs=extstructs,
                                                 cdecl=cdecl,
                                                 islocal=False,
                                                 iscreate=False))
        self.structMembers.append(self.StructMemberData(name=typeName, members=membersInfo))
    #
    # Insert a lock_guard line
    def lock_guard(self, indent):
        return '%sstd::lock_guard<std::mutex> lock(global_lock);\n' % indent
    #
    # Determine if a struct has an object as a member or an embedded member
    def struct_contains_object(self, struct_item):
        struct_member_dict = dict(self.structMembers)
        struct_members = struct_member_dict[struct_item]

        for member in struct_members:
            if member.type in self.handle_types:
                return True
            # recurse for member structs, guard against infinite recursion
            elif member.type in struct_member_dict and member.type != struct_item:
                if self.struct_contains_object(member.type):
                    return True
        return False
    #
    # Return list of struct members which contain, or whose sub-structures contain an obj in a given list of parameters or members
    def getParmeterStructsWithObjects(self, item_list):
        struct_list = set()
        for item in item_list:
            paramtype = item.find('type')
            typecategory = self.type_categories[paramtype.text]
            if typecategory == 'struct':
                if self.struct_contains_object(paramtype.text) == True:
                    struct_list.add(item)
        return struct_list
    #
    # Return list of objects from a given list of parameters or members
    def getObjectsInParameterList(self, item_list, create_func):
        object_list = set()
        if create_func == True:
            member_list = item_list[0:-1]
        else:
            member_list = item_list
        for item in member_list:
            if paramtype.text in self.handle_types:
                object_list.add(item)
        return object_list
    #
    # Construct list of extension structs containing handles, or extension structs that share a <validextensionstructs>
    # tag WITH an extension struct containing handles.
    def GenerateCommandWrapExtensionList(self):
        for struct in self.structMembers:
            if (len(struct.members) > 1) and struct.members[1].extstructs is not None:
                found = False;
                for item in struct.members[1].extstructs.split(','):
                    if item != '' and self.struct_contains_object(item) == True:
                        found = True
                if found == True:
                    for item in struct.members[1].extstructs.split(','):
                        if item != '' and item not in self.extension_structs:
                            self.extension_structs.append(item)
    #
    # Returns True if a struct may have a pNext chain containing an object
    def StructWithExtensions(self, struct_type):
        if struct_type in self.struct_member_dict:
            param_info = self.struct_member_dict[struct_type]
            if (len(param_info) > 1) and param_info[1].extstructs is not None:
                for item in param_info[1].extstructs.split(','):
                    if item in self.extension_structs:
                        return True
        return False
    #
    # Generate VulkanObjectType from object type
    def GetVulkanObjType(self, type):
        return 'kVulkanObjectType%s' % type[2:]
    #
    # Return correct dispatch table type -- instance or device
    def GetDispType(self, type):
        return 'instance' if type in ['VkInstance', 'VkPhysicalDevice'] else 'device'
    #
    # Generate source for creating a Vulkan object
    def generate_create_object_code(self, indent, proto, params, cmd_info, allocator):
        create_obj_code = ''
        handle_type = params[-1].find('type')
        is_create_pipelines = False

        if handle_type.text in self.handle_types:
            # Check for special case where multiple handles are returned
            object_array = False
            if cmd_info[-1].len is not None:
                object_array = True;
            handle_name = params[-1].find('name')
            object_dest = '*%s' % handle_name.text
            if object_array == True:
                if 'CreateGraphicsPipelines' in proto.text or 'CreateComputePipelines' in proto.text or 'CreateRayTracingPipelines' in proto.text:
                    is_create_pipelines = True
                    create_obj_code += '%sif (VK_ERROR_VALIDATION_FAILED_EXT == result) return;\n' % indent
                create_obj_code += '%sif (%s) {\n' % (indent, handle_name.text)
                indent = self.incIndent(indent)
                countispointer = ''
                if 'uint32_t*' in cmd_info[-2].cdecl:
                    countispointer = '*'
                create_obj_code += '%sfor (uint32_t index = 0; index < %s%s; index++) {\n' % (indent, countispointer, cmd_info[-1].len)
                indent = self.incIndent(indent)
                object_dest = '%s[index]' % cmd_info[-1].name

            dispobj = params[0].find('type').text
            if is_create_pipelines:
                create_obj_code += '%sif (!pPipelines[index]) continue;\n' % indent
            create_obj_code += '%sCreateObject(%s, %s, %s);\n' % (indent, object_dest, self.GetVulkanObjType(cmd_info[-1].type), allocator)
            if object_array == True:
                indent = self.decIndent(indent)
                create_obj_code += '%s}\n' % indent
                indent = self.decIndent(indent)
                create_obj_code += '%s}\n' % indent
            indent = self.decIndent(indent)

        return create_obj_code
    #
    # Generate source for destroying a non-dispatchable object
    def generate_destroy_object_code(self, indent, proto, cmd_info):
        validate_code = ''
        record_code = ''
        object_array = False
        if True in [destroy_txt in proto.text for destroy_txt in ['Destroy', 'Free']]:
            # Check for special case where multiple handles are returned
            if cmd_info[-1].len is not None:
                object_array = True;
                param = -1
            else:
                param = -2
            compatalloc_vuid_string = '%s-compatalloc' % cmd_info[param].name
            nullalloc_vuid_string = '%s-nullalloc' % cmd_info[param].name
            compatalloc_vuid = self.manual_vuids.get(compatalloc_vuid_string, "kVUIDUndefined")
            nullalloc_vuid = self.manual_vuids.get(nullalloc_vuid_string, "kVUIDUndefined")
            if cmd_info[param].type in self.handle_types:
                if object_array == True:
                    # This API is freeing an array of handles -- add loop control
                    validate_code += 'HEY, NEED TO DESTROY AN ARRAY\n'
                else:
                    dispobj = cmd_info[0].type
                    # Call Destroy a single time
                    validate_code += '%sskip |= ValidateDestroyObject(%s, %s, pAllocator, %s, %s);\n' % (indent, cmd_info[param].name, self.GetVulkanObjType(cmd_info[param].type), compatalloc_vuid, nullalloc_vuid)
                    record_code += '%sRecordDestroyObject(%s, %s);\n' % (indent, cmd_info[param].name, self.GetVulkanObjType(cmd_info[param].type))
        return object_array, validate_code, record_code
    #
    # Output validation for a single object (obj_count is NULL) or a counted list of objects
    def outputObjects(self, obj_type, obj_name, obj_count, prefix, index, indent, disp_name, parent_name, null_allowed, top_level):
        pre_call_code = ''
        param_suffix = '%s-parameter' % (obj_name)
        parent_suffix = '%s-parent' % (obj_name)
        param_vuid = self.GetVuid(parent_name, param_suffix)
        parent_vuid = self.GetVuid(parent_name, parent_suffix)

        # If no parent VUID for this member, look for a commonparent VUID
        if parent_vuid == 'kVUIDUndefined':
            parent_vuid = self.GetVuid(parent_name, 'commonparent')
        if obj_count is not None:

            pre_call_code += '%sif (%s%s) {\n' % (indent, prefix, obj_name)
            indent = self.incIndent(indent)
            pre_call_code += '%sfor (uint32_t %s = 0; %s < %s; ++%s) {\n' % (indent, index, index, obj_count, index)
            indent = self.incIndent(indent)
            pre_call_code += '%sskip |= ValidateObject(%s%s[%s], %s, %s, %s, %s);\n' % (indent, prefix, obj_name, index, self.GetVulkanObjType(obj_type), null_allowed, param_vuid, parent_vuid)
            indent = self.decIndent(indent)
            pre_call_code += '%s}\n' % indent
            indent = self.decIndent(indent)
            pre_call_code += '%s}\n' % indent
        else:
            bonus_indent = ''
            if 'basePipelineHandle' in obj_name:
                pre_call_code += '%sif ((%sflags & VK_PIPELINE_CREATE_DERIVATIVE_BIT) && (%sbasePipelineIndex == -1))\n' % (indent, prefix, prefix)
                bonus_indent = '    '
                null_allowed = 'false'
                manual_vuid_index = parent_name + '-' + obj_name
                param_vuid = self.manual_vuids.get(manual_vuid_index, "kVUIDUndefined")
            pre_call_code += '%s%sskip |= ValidateObject(%s%s, %s, %s, %s, %s);\n' % (bonus_indent, indent, prefix, obj_name, self.GetVulkanObjType(obj_type), null_allowed, param_vuid, parent_vuid)
        return pre_call_code
    #
    # first_level_param indicates if elements are passed directly into the function else they're below a ptr/struct
    def validate_objects(self, members, indent, prefix, array_index, disp_name, parent_name, first_level_param):
        pre_code = ''
        index = 'index%s' % str(array_index)
        array_index += 1
        # Process any objects in this structure and recurse for any sub-structs in this struct
        for member in members:
            # Handle objects
            if member.iscreate and first_level_param and member == members[-1]:
                continue
            if member.type in self.handle_types:
                count_name = member.len
                if (count_name is not None):
                    count_name = '%s%s' % (prefix, member.len)
                null_allowed = member.isoptional
                tmp_pre = self.outputObjects(member.type, member.name, count_name, prefix, index, indent, disp_name, parent_name, str(null_allowed).lower(), first_level_param)
                pre_code += tmp_pre
            # Handle Structs that contain objects at some level
            elif member.type in self.struct_member_dict:
                # Structs at first level will have an object
                if self.struct_contains_object(member.type) == True:
                    struct_info = self.struct_member_dict[member.type]
                    # TODO (jbolz): Can this use paramIsPointer?
                    ispointer = '*' in member.cdecl;
                    # Struct Array
                    if member.len is not None:
                        # Update struct prefix
                        new_prefix = '%s%s' % (prefix, member.name)
                        pre_code += '%sif (%s%s) {\n' % (indent, prefix, member.name)
                        indent = self.incIndent(indent)
                        pre_code += '%sfor (uint32_t %s = 0; %s < %s%s; ++%s) {\n' % (indent, index, index, prefix, member.len, index)
                        indent = self.incIndent(indent)
                        local_prefix = '%s[%s].' % (new_prefix, index)
                        # Process sub-structs in this struct
                        tmp_pre = self.validate_objects(struct_info, indent, local_prefix, array_index, disp_name, member.type, False)
                        pre_code += tmp_pre
                        indent = self.decIndent(indent)
                        pre_code += '%s}\n' % indent
                        indent = self.decIndent(indent)
                        pre_code += '%s}\n' % indent
                    # Single Struct Pointer
                    elif ispointer:
                        # Update struct prefix
                        new_prefix = '%s%s->' % (prefix, member.name)
                        # Declare safe_VarType for struct
                        pre_code += '%sif (%s%s) {\n' % (indent, prefix, member.name)
                        indent = self.incIndent(indent)
                        # Process sub-structs in this struct
                        tmp_pre = self.validate_objects(struct_info, indent, new_prefix, array_index, disp_name, member.type, False)
                        pre_code += tmp_pre
                        indent = self.decIndent(indent)
                        pre_code += '%s}\n' % indent
                    # Single Nested Struct
                    else:
                        # Update struct prefix
                        new_prefix = '%s%s.' % (prefix, member.name)
                        # Process sub-structs
                        tmp_pre = self.validate_objects(struct_info, indent, new_prefix, array_index, disp_name, member.type, False)
                        pre_code += tmp_pre
        return pre_code
    #
    # For a particular API, generate the object handling code
    def generate_wrapping_code(self, cmd):
        indent = '    '
        pre_call_validate = ''
        pre_call_record = ''
        post_call_record = ''

        destroy_array = False
        validate_destroy_code = ''
        record_destroy_code = ''

        proto = cmd.find('proto/name')
        params = cmd.findall('param')
        if proto.text is not None:
            cmddata = self.cmd_info_dict[proto.text]
            cmd_info = cmddata.members
            disp_name = cmd_info[0].name
            # Handle object create operations if last parameter is created by this call
            if cmddata.iscreate:
                post_call_record += self.generate_create_object_code(indent, proto, params, cmd_info, cmddata.allocator)
            # Handle object destroy operations
            if cmddata.isdestroy:
                (destroy_array, validate_destroy_code, record_destroy_code) = self.generate_destroy_object_code(indent, proto, cmd_info)

            pre_call_record += record_destroy_code
            pre_call_validate += self.validate_objects(cmd_info, indent, '', 0, disp_name, proto.text, True)
            pre_call_validate += validate_destroy_code

        return pre_call_validate, pre_call_record, post_call_record
    #
    # Capture command parameter info needed to create, destroy, and validate objects
    def genCmd(self, cmdinfo, cmdname, alias):
        # Add struct-member type information to command parameter information
        OutputGenerator.genCmd(self, cmdinfo, cmdname, alias)
        members = cmdinfo.elem.findall('.//param')
        # Iterate over members once to get length parameters for arrays
        lens = set()
        for member in members:
            length = self.getLen(member)
            if length:
                lens.add(length)
        struct_member_dict = dict(self.structMembers)

        # Set command invariant information needed at a per member level in validate...
        is_create_command = any(filter(lambda pat: pat in cmdname, ('Create', 'Allocate', 'Enumerate', 'RegisterDeviceEvent', 'RegisterDisplayEvent')))
        last_member_is_pointer = len(members) and self.paramIsPointer(members[-1])
        iscreate = is_create_command or ('vkGet' in cmdname and last_member_is_pointer)
        isdestroy = any([destroy_txt in cmdname for destroy_txt in ['Destroy', 'Free']])

        # Generate member info
        membersInfo = []
        allocator = 'nullptr'
        for member in members:
            # Get type and name of member
            info = self.getTypeNameTuple(member)
            type = info[0]
            name = info[1]
            cdecl = self.makeCParamDecl(member, 0)
            # Check for parameter name in lens set
            iscount = True if name in lens else False
            length = self.getLen(member)
            isconst = True if 'const' in cdecl else False
            # Mark param as local if it is an array of objects
            islocal = False;
            if type in self.handle_types:
                if (length is not None) and (isconst == True):
                    islocal = True
            # Or if it's a struct that contains an object
            elif type in struct_member_dict:
                if self.struct_contains_object(type) == True:
                    islocal = True
            if type == 'VkAllocationCallbacks':
                allocator = name
            extstructs = member.attrib.get('validextensionstructs') if name == 'pNext' else None
            membersInfo.append(self.CommandParam(type=type,
                                                 name=name,
                                                 isconst=isconst,
                                                 isoptional=self.paramIsOptional(member),
                                                 iscount=iscount,
                                                 len=length,
                                                 extstructs=extstructs,
                                                 cdecl=cdecl,
                                                 islocal=islocal,
                                                 iscreate=iscreate))

        self.cmd_list.append(cmdname)
        self.cmd_info_dict[cmdname] =self.CmdInfoData(name=cmdname, cmdinfo=cmdinfo, members=membersInfo, iscreate=iscreate, isdestroy=isdestroy, allocator=allocator, extra_protect=self.featureExtraProtect, alias=alias)
    #
    # Create code Create, Destroy, and validate Vulkan objects
    def WrapCommands(self):
        for cmdname in self.cmd_list:
            cmddata = self.cmd_info_dict[cmdname]
            cmdinfo = cmddata.cmdinfo
            if cmdname in self.interface_functions:
                continue
            manual = False
            if cmdname in self.no_autogen_list:
                manual = True

            # Generate object handling code
            (pre_call_validate, pre_call_record, post_call_record) = self.generate_wrapping_code(cmdinfo.elem)

            feature_extra_protect = cmddata.extra_protect
            if (feature_extra_protect is not None):
                self.appendSection('command', '')
                self.appendSection('command', '#ifdef '+ feature_extra_protect)
                self.prototypes += [ '#ifdef %s' % feature_extra_protect ]

            # Add intercept to procmap
            self.prototypes += [ '    {"%s", (void*)%s},' % (cmdname,cmdname[2:]) ]

            decls = self.makeCDecls(cmdinfo.elem)

            # Gather the parameter items
            params = cmdinfo.elem.findall('param/name')
            # Pull out the text for each of the parameters, separate them by commas in a list
            paramstext = ', '.join([str(param.text) for param in params])
            # Generate the API call template
            fcn_call = cmdinfo.elem.attrib.get('name').replace('vk', 'TOKEN', 1) + '(' + paramstext + ');'

            func_decl_template = decls[0][:-1].split('VKAPI_CALL ')
            func_decl_template = func_decl_template[1]

            result_type = cmdinfo.elem.find('proto/type')

            if 'object_tracker.h' in self.genOpts.filename:
                # Output PreCallValidateAPI prototype if necessary
                if pre_call_validate:
                    pre_cv_func_decl = 'bool PreCallValidate' + func_decl_template + ';'
                    self.appendSection('command', pre_cv_func_decl)

                # Output PreCallRecordAPI prototype if necessary
                if pre_call_record:
                    pre_cr_func_decl = 'void PreCallRecord' + func_decl_template + ';'
                    self.appendSection('command', pre_cr_func_decl)

                # Output PosCallRecordAPI prototype if necessary
                if post_call_record:
                    post_cr_func_decl = 'void PostCallRecord' + func_decl_template + ';'
                    if result_type.text == 'VkResult':
                        post_cr_func_decl = post_cr_func_decl.replace(')', ',\n    VkResult                                    result)')
                    elif result_type.text == 'VkDeviceAddress':
                        post_cr_func_decl = post_cr_func_decl.replace(')', ',\n    VkDeviceAddress                             result)')
                    self.appendSection('command', post_cr_func_decl)

            if 'object_tracker.cpp' in self.genOpts.filename:
                # Output PreCallValidateAPI function if necessary
                if pre_call_validate and not manual:
                    pre_cv_func_decl = 'bool ObjectLifetimes::PreCallValidate' + func_decl_template + ' {'
                    self.appendSection('command', '')
                    self.appendSection('command', pre_cv_func_decl)
                    self.appendSection('command', '    bool skip = false;')
                    self.appendSection('command', pre_call_validate)
                    self.appendSection('command', '    return skip;')
                    self.appendSection('command', '}')

                # Output PreCallRecordAPI function if necessary
                if pre_call_record and not manual:
                    pre_cr_func_decl = 'void ObjectLifetimes::PreCallRecord' + func_decl_template + ' {'
                    self.appendSection('command', '')
                    self.appendSection('command', pre_cr_func_decl)
                    self.appendSection('command', pre_call_record)
                    self.appendSection('command', '}')

                # Output PosCallRecordAPI function if necessary
                if post_call_record and not manual:
                    post_cr_func_decl = 'void ObjectLifetimes::PostCallRecord' + func_decl_template + ' {'
                    self.appendSection('command', '')

                    if result_type.text == 'VkResult':
                        post_cr_func_decl = post_cr_func_decl.replace(')', ',\n    VkResult                                    result)')
                        # The two createpipelines APIs may create on failure -- skip the success result check
                        if 'CreateGraphicsPipelines' not in cmdname and 'CreateComputePipelines' not in cmdname and 'CreateRayTracingPipelines' not in cmdname:
                            post_cr_func_decl = post_cr_func_decl.replace('{', '{\n    if (result != VK_SUCCESS) return;')
                    elif result_type.text == 'VkDeviceAddress':
                        post_cr_func_decl = post_cr_func_decl.replace(')', ',\n    VkDeviceAddress                             result)')
                    self.appendSection('command', post_cr_func_decl)

                    self.appendSection('command', post_call_record)
                    self.appendSection('command', '}')

            if (feature_extra_protect is not None):
                self.appendSection('command', '#endif // '+ feature_extra_protect)
                self.prototypes += [ '#endif' ]
