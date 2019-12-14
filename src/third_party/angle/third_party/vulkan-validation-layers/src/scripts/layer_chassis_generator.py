#!/usr/bin/python3 -i
#
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
# Author: Tobin Ehlis <tobine@google.com>
# Author: Mark Lobodzinski <mark@lunarg.com>
#
# This script generates the dispatch portion of a factory layer which intercepts
# all Vulkan  functions. The resultant factory layer allows rapid development of
# layers and interceptors.

import os,re,sys
from generator import *
from common_codegen import *

# LayerFactoryGeneratorOptions - subclass of GeneratorOptions.
#
# Adds options used by LayerFactoryOutputGenerator objects during factory
# layer generation.
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
class LayerChassisGeneratorOptions(GeneratorOptions):
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
                 helper_file_type = '',
                 expandEnumerants = True):
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

# LayerChassisOutputGenerator - subclass of OutputGenerator.
# Generates a LayerFactory layer that intercepts all API entrypoints
#  This is intended to be used as a starting point for creating custom layers
#
# ---- methods ----
# LayerChassisOutputGenerator(errFile, warnFile, diagFile) - args as for
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
class LayerChassisOutputGenerator(OutputGenerator):
    """Generate specified API interfaces in a specific style, such as a C header"""
    # This is an ordered list of sections in the header file.
    TYPE_SECTIONS = ['include', 'define', 'basetype', 'handle', 'enum',
                     'group', 'bitmask', 'funcpointer', 'struct']
    ALL_SECTIONS = TYPE_SECTIONS + ['command']

    manual_functions = [
        # Include functions here to be interecpted w/ manually implemented function bodies
        'vkGetDeviceProcAddr',
        'vkGetInstanceProcAddr',
        'vkCreateDevice',
        'vkDestroyDevice',
        'vkCreateInstance',
        'vkDestroyInstance',
        'vkEnumerateInstanceLayerProperties',
        'vkEnumerateInstanceExtensionProperties',
        'vkEnumerateDeviceLayerProperties',
        'vkEnumerateDeviceExtensionProperties',
        # Functions that are handled explicitly due to chassis architecture violations
        'vkCreateGraphicsPipelines',
        'vkCreateComputePipelines',
        'vkCreateRayTracingPipelinesNV',
        'vkCreatePipelineLayout',
        'vkCreateShaderModule',
        'vkAllocateDescriptorSets',
        'vkCreateBuffer',
        # ValidationCache functions do not get dispatched
        'vkCreateValidationCacheEXT',
        'vkDestroyValidationCacheEXT',
        'vkMergeValidationCachesEXT',
        'vkGetValidationCacheDataEXT',
        # We don't wanna hook this function
        'vkGetPhysicalDeviceProcAddr',
        ]

    alt_ret_codes = [
        # Include functions here which must tolerate VK_INCOMPLETE as a return code
        'vkEnumeratePhysicalDevices',
        'vkEnumeratePhysicalDeviceGroupsKHR',
        'vkGetValidationCacheDataEXT',
        'vkGetPipelineCacheData',
        'vkGetShaderInfoAMD',
        'vkGetPhysicalDeviceDisplayPropertiesKHR',
        'vkGetPhysicalDeviceDisplayProperties2KHR',
        'vkGetPhysicalDeviceDisplayPlanePropertiesKHR',
        'vkGetDisplayPlaneSupportedDisplaysKHR',
        'vkGetDisplayModePropertiesKHR',
        'vkGetDisplayModeProperties2KHR',
        'vkGetPhysicalDeviceSurfaceFormatsKHR',
        'vkGetPhysicalDeviceSurfacePresentModesKHR',
        'vkGetPhysicalDevicePresentRectanglesKHR',
        'vkGetPastPresentationTimingGOOGLE',
        'vkGetSwapchainImagesKHR',
        'vkEnumerateInstanceLayerProperties',
        'vkEnumerateDeviceLayerProperties',
        'vkEnumerateInstanceExtensionProperties',
        'vkEnumerateDeviceExtensionProperties',
        'vkGetPhysicalDeviceCalibrateableTimeDomainsEXT',
    ]

    pre_dispatch_debug_utils_functions = {
        'vkDebugMarkerSetObjectNameEXT' : 'layer_data->report_data->DebugReportSetMarkerObjectName(pNameInfo);',
        'vkSetDebugUtilsObjectNameEXT' : 'layer_data->report_data->DebugReportSetUtilsObjectName(pNameInfo);',
        'vkQueueBeginDebugUtilsLabelEXT' : 'BeginQueueDebugUtilsLabel(layer_data->report_data, queue, pLabelInfo);',
        'vkQueueInsertDebugUtilsLabelEXT' : 'InsertQueueDebugUtilsLabel(layer_data->report_data, queue, pLabelInfo);',
        }

    post_dispatch_debug_utils_functions = {
        'vkQueueEndDebugUtilsLabelEXT' : 'EndQueueDebugUtilsLabel(layer_data->report_data, queue);',
        'vkCreateDebugReportCallbackEXT' : 'layer_create_report_callback(layer_data->report_data, false, pCreateInfo, pAllocator, pCallback);',
        'vkDestroyDebugReportCallbackEXT' : 'layer_destroy_callback(layer_data->report_data, callback, pAllocator);',
        'vkCreateDebugUtilsMessengerEXT' : 'layer_create_messenger_callback(layer_data->report_data, false, pCreateInfo, pAllocator, pMessenger);',
        'vkDestroyDebugUtilsMessengerEXT' : 'layer_destroy_callback(layer_data->report_data, messenger, pAllocator);',
        }

    precallvalidate_loop = "for (auto intercept : layer_data->object_dispatch) {"
    precallrecord_loop = precallvalidate_loop
    postcallrecord_loop = "for (auto intercept : layer_data->object_dispatch) {"

    inline_custom_header_preamble = """
#define NOMINMAX
#include <atomic>
#include <mutex>
#include <cinttypes>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>

#include "vk_loader_platform.h"
#include "vulkan/vulkan.h"
#include "vk_layer_config.h"
#include "vk_layer_data.h"
#include "vk_layer_logging.h"
#include "vk_object_types.h"
#include "vulkan/vk_layer.h"
#include "vk_enum_string_helper.h"
#include "vk_layer_extension_utils.h"
#include "vk_layer_utils.h"
#include "vulkan/vk_layer.h"
#include "vk_dispatch_table_helper.h"
#include "vk_extension_helper.h"
#include "vk_safe_struct.h"
#include "vk_typemap_helper.h"


extern std::atomic<uint64_t> global_unique_id;

// To avoid re-hashing unique ids on each use, we precompute the hash and store the
// hash's LSBs in the high 24 bits.
struct HashedUint64 {
    static const int HASHED_UINT64_SHIFT = 40;
    size_t operator()(const uint64_t &t) const { return t >> HASHED_UINT64_SHIFT; }

    static uint64_t hash(uint64_t id) {
        uint64_t h = (uint64_t)std::hash<uint64_t>()(id);
        id |= h << HASHED_UINT64_SHIFT;
        return id;
    }
};

extern vl_concurrent_unordered_map<uint64_t, uint64_t, 4, HashedUint64> unique_id_mapping;
"""

    inline_custom_header_class_definition = """

// Layer object type identifiers
enum LayerObjectTypeId {
    LayerObjectTypeInstance,                    // Container for an instance dispatch object
    LayerObjectTypeDevice,                      // Container for a device dispatch object
    LayerObjectTypeThreading,                   // Instance or device threading layer object
    LayerObjectTypeParameterValidation,         // Instance or device parameter validation layer object
    LayerObjectTypeObjectTracker,               // Instance or device object tracker layer object
    LayerObjectTypeCoreValidation,              // Instance or device core validation layer object
    LayerObjectTypeBestPractices,               // Instance or device best practices layer object
    LayerObjectTypeMaxEnum,                     // Max enum count
};

struct TEMPLATE_STATE {
    VkDescriptorUpdateTemplateKHR desc_update_template;
    safe_VkDescriptorUpdateTemplateCreateInfo create_info;

    TEMPLATE_STATE(VkDescriptorUpdateTemplateKHR update_template, safe_VkDescriptorUpdateTemplateCreateInfo *pCreateInfo)
        : desc_update_template(update_template), create_info(*pCreateInfo) {}
};

class LAYER_PHYS_DEV_PROPERTIES {
public:
    VkPhysicalDeviceProperties properties;
    std::vector<VkQueueFamilyProperties> queue_family_properties;
};

typedef enum ValidationCheckDisables {
    VALIDATION_CHECK_DISABLE_COMMAND_BUFFER_STATE,
    VALIDATION_CHECK_DISABLE_OBJECT_IN_USE,
    VALIDATION_CHECK_DISABLE_IDLE_DESCRIPTOR_SET,
    VALIDATION_CHECK_DISABLE_PUSH_CONSTANT_RANGE,
    VALIDATION_CHECK_DISABLE_QUERY_VALIDATION,
    VALIDATION_CHECK_DISABLE_IMAGE_LAYOUT_VALIDATION,
} ValidationCheckDisables;

// CHECK_DISABLED struct is a container for bools that can block validation checks from being performed.
// These bools are all "false" by default meaning that all checks are enabled. Enum values can be specified
// via the vk_layer_setting.txt config file or at CreateInstance time via the VK_EXT_validation_features extension
// that can selectively disable checks.
struct CHECK_DISABLED {
    bool command_buffer_state;                      // Skip command buffer state validation
    bool object_in_use;                             // Skip all object in_use checking
    bool idle_descriptor_set;                       // Skip check to verify that descriptor set is not in-use
    bool push_constant_range;                       // Skip push constant range checks
    bool query_validation;                          // Disable all core validation query-related checks
    bool image_layout_validation;                   // Disable image layout validation
    bool object_tracking;                           // Disable object lifetime validation
    bool core_checks;                               // Disable core validation checks
    bool thread_safety;                             // Disable thread safety validation
    bool stateless_checks;                          // Disable stateless validation checks
    bool handle_wrapping;                           // Disable unique handles/handle wrapping
    bool shader_validation;                         // Skip validation for shaders

    void SetAll(bool value) { std::fill(&command_buffer_state, &shader_validation + 1, value); }
};

struct CHECK_ENABLED {
    bool gpu_validation;
    bool gpu_validation_reserve_binding_slot;
    bool best_practices;

    void SetAll(bool value) { std::fill(&gpu_validation, &gpu_validation_reserve_binding_slot + 1, value); }
};

// Layer chassis validation object base class definition
class ValidationObject {
    public:
        uint32_t api_version;
        debug_report_data* report_data = nullptr;

        VkLayerInstanceDispatchTable instance_dispatch_table;
        VkLayerDispatchTable device_dispatch_table;

        InstanceExtensions instance_extensions;
        DeviceExtensions device_extensions = {};
        CHECK_DISABLED disabled = {};
        CHECK_ENABLED enabled = {};

        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        LAYER_PHYS_DEV_PROPERTIES phys_dev_properties = {};

        std::vector<ValidationObject*> object_dispatch;
        LayerObjectTypeId container_type;

        std::string layer_name = "CHASSIS";

        // Constructor
        ValidationObject(){};
        // Destructor
        virtual ~ValidationObject() {};

        std::mutex validation_object_mutex;
        virtual std::unique_lock<std::mutex> write_lock() {
            return std::unique_lock<std::mutex>(validation_object_mutex);
        }

        ValidationObject* GetValidationObject(std::vector<ValidationObject*>& object_dispatch, LayerObjectTypeId object_type) {
            for (auto validation_object : object_dispatch) {
                if (validation_object->container_type == object_type) {
                    return validation_object;
                }
            }
            return nullptr;
        };

        // Handle Wrapping Data
        // Reverse map display handles
        vl_concurrent_unordered_map<VkDisplayKHR, uint64_t, 0> display_id_reverse_mapping;
        // Wrapping Descriptor Template Update structures requires access to the template createinfo structs
        std::unordered_map<uint64_t, std::unique_ptr<TEMPLATE_STATE>> desc_template_createinfo_map;
        struct SubpassesUsageStates {
            std::unordered_set<uint32_t> subpasses_using_color_attachment;
            std::unordered_set<uint32_t> subpasses_using_depthstencil_attachment;
        };
        // Uses unwrapped handles
        std::unordered_map<VkRenderPass, SubpassesUsageStates> renderpasses_states;
        // Map of wrapped swapchain handles to arrays of wrapped swapchain image IDs
        // Each swapchain has an immutable list of wrapped swapchain image IDs -- always return these IDs if they exist
        std::unordered_map<VkSwapchainKHR, std::vector<VkImage>> swapchain_wrapped_image_handle_map;
        // Map of wrapped descriptor pools to set of wrapped descriptor sets allocated from each pool
        std::unordered_map<VkDescriptorPool, std::unordered_set<VkDescriptorSet>> pool_descriptor_sets_map;


        // Unwrap a handle.
        template <typename HandleType>
        HandleType Unwrap(HandleType wrappedHandle) {
            auto iter = unique_id_mapping.find(reinterpret_cast<uint64_t const &>(wrappedHandle));
            if (iter == unique_id_mapping.end())
                return (HandleType)0;
            return (HandleType)iter->second;
        }

        // Wrap a newly created handle with a new unique ID, and return the new ID.
        template <typename HandleType>
        HandleType WrapNew(HandleType newlyCreatedHandle) {
            auto unique_id = global_unique_id++;
            unique_id = HashedUint64::hash(unique_id);
            unique_id_mapping.insert_or_assign(unique_id, reinterpret_cast<uint64_t const &>(newlyCreatedHandle));
            return (HandleType)unique_id;
        }

        // Specialized handling for VkDisplayKHR. Adds an entry to enable reverse-lookup.
        VkDisplayKHR WrapDisplay(VkDisplayKHR newlyCreatedHandle, ValidationObject *map_data) {
            auto unique_id = global_unique_id++;
            unique_id = HashedUint64::hash(unique_id);
            unique_id_mapping.insert_or_assign(unique_id, reinterpret_cast<uint64_t const &>(newlyCreatedHandle));
            map_data->display_id_reverse_mapping.insert_or_assign(newlyCreatedHandle, unique_id);
            return (VkDisplayKHR)unique_id;
        }

        // VkDisplayKHR objects don't have a single point of creation, so we need to see if one already exists in the map before
        // creating another.
        VkDisplayKHR MaybeWrapDisplay(VkDisplayKHR handle, ValidationObject *map_data) {
            // See if this display is already known
            auto it = map_data->display_id_reverse_mapping.find(handle);
            if (it != map_data->display_id_reverse_mapping.end()) return (VkDisplayKHR)it->second;
            // Unknown, so wrap
            return WrapDisplay(handle, map_data);
        }

        // Pre/post hook point declarations
"""

    inline_copyright_message = """
// This file is ***GENERATED***.  Do Not Edit.
// See layer_chassis_generator.py for modifications.

/* Copyright (c) 2015-2019 The Khronos Group Inc.
 * Copyright (c) 2015-2019 Valve Corporation
 * Copyright (c) 2015-2019 LunarG, Inc.
 * Copyright (c) 2015-2019 Google Inc.
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
 * Author: Mark Lobodzinski <mark@lunarg.com>
 */"""

    inline_custom_source_preamble = """

#include <string.h>
#include <mutex>

#define VALIDATION_ERROR_MAP_IMPL

#include "chassis.h"
#include "layer_chassis_dispatch.h"

small_unordered_map<void*, ValidationObject*, 2> layer_data_map;

// Global unique object identifier.
std::atomic<uint64_t> global_unique_id(1ULL);
// Map uniqueID to actual object handle. Accesses to the map itself are
// internally synchronized.
vl_concurrent_unordered_map<uint64_t, uint64_t, 4, HashedUint64> unique_id_mapping;

bool wrap_handles = true;

#define OBJECT_LAYER_NAME "VK_LAYER_KHRONOS_validation"
#define OBJECT_LAYER_DESCRIPTION "khronos_validation"

// Include layer validation object definitions
#include "object_lifetime_validation.h"
#include "thread_safety.h"
#include "stateless_validation.h"
#include "core_validation.h"
#include "best_practices.h"

namespace vulkan_layer_chassis {

using std::unordered_map;

static const VkLayerProperties global_layer = {
    OBJECT_LAYER_NAME, VK_LAYER_API_VERSION, 1, "LunarG validation Layer",
};

static const VkExtensionProperties instance_extensions[] = {{VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_EXT_DEBUG_REPORT_SPEC_VERSION},
                                                            {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_SPEC_VERSION}};
static const VkExtensionProperties device_extensions[] = {
    {VK_EXT_VALIDATION_CACHE_EXTENSION_NAME, VK_EXT_VALIDATION_CACHE_SPEC_VERSION},
    {VK_EXT_DEBUG_MARKER_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_SPEC_VERSION},
};

typedef struct {
    bool is_instance_api;
    void* funcptr;
} function_data;

extern const std::unordered_map<std::string, function_data> name_to_funcptr_map;

// Manually written functions

// Check enabled instance extensions against supported instance extension whitelist
static void InstanceExtensionWhitelist(ValidationObject *layer_data, const VkInstanceCreateInfo *pCreateInfo, VkInstance instance) {
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        // Check for recognized instance extensions
        if (!white_list(pCreateInfo->ppEnabledExtensionNames[i], kInstanceExtensionNames)) {
            log_msg(layer_data->report_data, VK_DEBUG_REPORT_WARNING_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT, 0,
                    kVUIDUndefined,
                    "Instance Extension %s is not supported by this layer.  Using this extension may adversely affect validation "
                    "results and/or produce undefined behavior.",
                    pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }
}

// Check enabled device extensions against supported device extension whitelist
static void DeviceExtensionWhitelist(ValidationObject *layer_data, const VkDeviceCreateInfo *pCreateInfo, VkDevice device) {
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        // Check for recognized device extensions
        if (!white_list(pCreateInfo->ppEnabledExtensionNames[i], kDeviceExtensionNames)) {
            log_msg(layer_data->report_data, VK_DEBUG_REPORT_WARNING_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT, 0,
                    kVUIDUndefined,
                    "Device Extension %s is not supported by this layer.  Using this extension may adversely affect validation "
                    "results and/or produce undefined behavior.",
                    pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }
}


// Process validation features, flags and settings specified through extensions, a layer settings file, or environment variables

static const std::unordered_map<std::string, VkValidationFeatureDisableEXT> VkValFeatureDisableLookup = {
    {"VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT", VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT},
    {"VK_VALIDATION_FEATURE_DISABLE_THREAD_SAFETY_EXT", VK_VALIDATION_FEATURE_DISABLE_THREAD_SAFETY_EXT},
    {"VK_VALIDATION_FEATURE_DISABLE_API_PARAMETERS_EXT", VK_VALIDATION_FEATURE_DISABLE_API_PARAMETERS_EXT},
    {"VK_VALIDATION_FEATURE_DISABLE_OBJECT_LIFETIMES_EXT", VK_VALIDATION_FEATURE_DISABLE_OBJECT_LIFETIMES_EXT},
    {"VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT", VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT},
    {"VK_VALIDATION_FEATURE_DISABLE_UNIQUE_HANDLES_EXT", VK_VALIDATION_FEATURE_DISABLE_UNIQUE_HANDLES_EXT},
    {"VK_VALIDATION_FEATURE_DISABLE_ALL_EXT", VK_VALIDATION_FEATURE_DISABLE_ALL_EXT},
};

static const std::unordered_map<std::string, VkValidationFeatureEnableEXT> VkValFeatureEnableLookup = {
    {"VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT", VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT},
    {"VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT", VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT},
    {"VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT", VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT},
};

static const std::unordered_map<std::string, ValidationCheckDisables> ValidationDisableLookup = {
    {"VALIDATION_CHECK_DISABLE_COMMAND_BUFFER_STATE", VALIDATION_CHECK_DISABLE_COMMAND_BUFFER_STATE},
    {"VALIDATION_CHECK_DISABLE_OBJECT_IN_USE", VALIDATION_CHECK_DISABLE_OBJECT_IN_USE},
    {"VALIDATION_CHECK_DISABLE_IDLE_DESCRIPTOR_SET", VALIDATION_CHECK_DISABLE_IDLE_DESCRIPTOR_SET},
    {"VALIDATION_CHECK_DISABLE_PUSH_CONSTANT_RANGE", VALIDATION_CHECK_DISABLE_PUSH_CONSTANT_RANGE},
    {"VALIDATION_CHECK_DISABLE_QUERY_VALIDATION", VALIDATION_CHECK_DISABLE_QUERY_VALIDATION},
    {"VALIDATION_CHECK_DISABLE_IMAGE_LAYOUT_VALIDATION", VALIDATION_CHECK_DISABLE_IMAGE_LAYOUT_VALIDATION},
};

// Set the local disable flag for the appropriate VALIDATION_CHECK_DISABLE enum
void SetValidationDisable(CHECK_DISABLED* disable_data, const ValidationCheckDisables disable_id) {
    switch (disable_id) {
        case VALIDATION_CHECK_DISABLE_COMMAND_BUFFER_STATE:
            disable_data->command_buffer_state = true;
            break;
        case VALIDATION_CHECK_DISABLE_OBJECT_IN_USE:
            disable_data->object_in_use = true;
            break;
        case VALIDATION_CHECK_DISABLE_IDLE_DESCRIPTOR_SET:
            disable_data->idle_descriptor_set = true;
            break;
        case VALIDATION_CHECK_DISABLE_PUSH_CONSTANT_RANGE:
            disable_data->push_constant_range = true;
            break;
        case VALIDATION_CHECK_DISABLE_QUERY_VALIDATION:
            disable_data->query_validation = true;
            break;
        case VALIDATION_CHECK_DISABLE_IMAGE_LAYOUT_VALIDATION:
            disable_data->image_layout_validation = true;
            break;
        default:
            assert(true);
    }
}

// Set the local disable flag for a single VK_VALIDATION_FEATURE_DISABLE_* flag
void SetValidationFeatureDisable(CHECK_DISABLED* disable_data, const VkValidationFeatureDisableEXT feature_disable) {
    switch (feature_disable) {
        case VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT:
            disable_data->shader_validation = true;
            break;
        case VK_VALIDATION_FEATURE_DISABLE_THREAD_SAFETY_EXT:
            disable_data->thread_safety = true;
            break;
        case VK_VALIDATION_FEATURE_DISABLE_API_PARAMETERS_EXT:
            disable_data->stateless_checks = true;
            break;
        case VK_VALIDATION_FEATURE_DISABLE_OBJECT_LIFETIMES_EXT:
            disable_data->object_tracking = true;
            break;
        case VK_VALIDATION_FEATURE_DISABLE_CORE_CHECKS_EXT:
            disable_data->core_checks = true;
            break;
        case VK_VALIDATION_FEATURE_DISABLE_UNIQUE_HANDLES_EXT:
            disable_data->handle_wrapping = true;
            break;
        case VK_VALIDATION_FEATURE_DISABLE_ALL_EXT:
            // Set all disabled flags to true
            disable_data->SetAll(true);
            break;
        default:
            break;
    }
}

// Set the local enable flag for a single VK_VALIDATION_FEATURE_ENABLE_* flag
void SetValidationFeatureEnable(CHECK_ENABLED *enable_data, const VkValidationFeatureEnableEXT feature_enable) {
    switch (feature_enable) {
        case VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT:
            enable_data->gpu_validation = true;
            break;
        case VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT:
            enable_data->gpu_validation_reserve_binding_slot = true;
            break;
        case VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT:
            enable_data->best_practices = true;
            break;
        default:
            break;
    }
}

// Set the local disable flag for settings specified through the VK_EXT_validation_flags extension
void SetValidationFlags(CHECK_DISABLED* disables, const VkValidationFlagsEXT* val_flags_struct) {
    for (uint32_t i = 0; i < val_flags_struct->disabledValidationCheckCount; ++i) {
        switch (val_flags_struct->pDisabledValidationChecks[i]) {
            case VK_VALIDATION_CHECK_SHADERS_EXT:
                disables->shader_validation = true;
                break;
            case VK_VALIDATION_CHECK_ALL_EXT:
                // Set all disabled flags to true
                disables->SetAll(true);
                break;
            default:
                break;
        }
    }
}

// Process Validation Features flags specified through the ValidationFeature extension
void SetValidationFeatures(CHECK_DISABLED *disable_data, CHECK_ENABLED *enable_data,
                           const VkValidationFeaturesEXT *val_features_struct) {
    for (uint32_t i = 0; i < val_features_struct->disabledValidationFeatureCount; ++i) {
        SetValidationFeatureDisable(disable_data, val_features_struct->pDisabledValidationFeatures[i]);
    }
    for (uint32_t i = 0; i < val_features_struct->enabledValidationFeatureCount; ++i) {
        SetValidationFeatureEnable(enable_data, val_features_struct->pEnabledValidationFeatures[i]);
    }
}

// Given a string representation of a list of enable enum values, call the appropriate setter function
void SetLocalEnableSetting(std::string list_of_enables, std::string delimiter, CHECK_ENABLED* enables) {
    size_t pos = 0;
    std::string token;
    while (list_of_enables.length() != 0) {
        pos = list_of_enables.find(delimiter);
        if (pos != std::string::npos) {
            token = list_of_enables.substr(0, pos);
        } else {
            pos = list_of_enables.length() - delimiter.length();
            token = list_of_enables;
        }
        if (token.find("VK_VALIDATION_FEATURE_ENABLE_") != std::string::npos) {
            auto result = VkValFeatureEnableLookup.find(token);
            if (result != VkValFeatureEnableLookup.end()) {
                SetValidationFeatureEnable(enables, result->second);
            }
        }
        list_of_enables.erase(0, pos + delimiter.length());
    }
}

// Given a string representation of a list of disable enum values, call the appropriate setter function
void SetLocalDisableSetting(std::string list_of_disables, std::string delimiter, CHECK_DISABLED* disables) {
    size_t pos = 0;
    std::string token;
    while (list_of_disables.length() != 0) {
        pos = list_of_disables.find(delimiter);
        if (pos != std::string::npos) {
            token = list_of_disables.substr(0, pos);
        } else {
            pos = list_of_disables.length() - delimiter.length();
            token = list_of_disables;
        }
        if (token.find("VK_VALIDATION_FEATURE_DISABLE_") != std::string::npos) {
            auto result = VkValFeatureDisableLookup.find(token);
            if (result != VkValFeatureDisableLookup.end()) {
                SetValidationFeatureDisable(disables, result->second);
            }
        }
        if (token.find("VALIDATION_CHECK_DISABLE_") != std::string::npos) {
            auto result = ValidationDisableLookup.find(token);
            if (result != ValidationDisableLookup.end()) {
                SetValidationDisable(disables, result->second);
            }
        }
        list_of_disables.erase(0, pos + delimiter.length());
    }
}

// Process enables and disables set though the vk_layer_settings.txt config file or through an environment variable
void ProcessConfigAndEnvSettings(const char* layer_description, CHECK_ENABLED* enables, CHECK_DISABLED* disables) {
    std::string enable_key = layer_description;
    std::string disable_key = layer_description;
    enable_key.append(".enables");
    disable_key.append(".disables");
    std::string list_of_config_enables = getLayerOption(enable_key.c_str());
    std::string list_of_env_enables = GetLayerEnvVar("VK_LAYER_ENABLES");
    std::string list_of_config_disables = getLayerOption(disable_key.c_str());
    std::string list_of_env_disables = GetLayerEnvVar("VK_LAYER_DISABLES");
#if defined(_WIN32)
    std::string env_delimiter = ";";
#else
    std::string env_delimiter = ":";
#endif
    SetLocalEnableSetting(list_of_config_enables, ",", enables);
    SetLocalEnableSetting(list_of_env_enables, env_delimiter, enables);
    SetLocalDisableSetting(list_of_config_disables, ",", disables);
    SetLocalDisableSetting(list_of_env_disables, env_delimiter, disables);
}


// Non-code-generated chassis API functions

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char *funcName) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!ApiParentExtensionEnabled(funcName, &layer_data->device_extensions)) {
        return nullptr;
    }
    const auto &item = name_to_funcptr_map.find(funcName);
    if (item != name_to_funcptr_map.end()) {
        if (item->second.is_instance_api) {
            return nullptr;
        } else {
            return reinterpret_cast<PFN_vkVoidFunction>(item->second.funcptr);
        }
    }
    auto &table = layer_data->device_dispatch_table;
    if (!table.GetDeviceProcAddr) return nullptr;
    return table.GetDeviceProcAddr(device, funcName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char *funcName) {
    const auto &item = name_to_funcptr_map.find(funcName);
    if (item != name_to_funcptr_map.end()) {
        return reinterpret_cast<PFN_vkVoidFunction>(item->second.funcptr);
    }
    auto layer_data = GetLayerDataPtr(get_dispatch_key(instance), layer_data_map);
    auto &table = layer_data->instance_dispatch_table;
    if (!table.GetInstanceProcAddr) return nullptr;
    return table.GetInstanceProcAddr(instance, funcName);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceLayerProperties(uint32_t *pCount, VkLayerProperties *pProperties) {
    return util_GetLayerProperties(1, &global_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                                              VkLayerProperties *pProperties) {
    return util_GetLayerProperties(1, &global_layer, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount,
                                                                    VkExtensionProperties *pProperties) {
    if (pLayerName && !strcmp(pLayerName, global_layer.layerName))
        return util_GetExtensionProperties(ARRAY_SIZE(instance_extensions), instance_extensions, pCount, pProperties);

    return VK_ERROR_LAYER_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                                                  uint32_t *pCount, VkExtensionProperties *pProperties) {
    if (pLayerName && !strcmp(pLayerName, global_layer.layerName)) return util_GetExtensionProperties(ARRAY_SIZE(device_extensions), device_extensions, pCount, pProperties);
    assert(physicalDevice);
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    return layer_data->instance_dispatch_table.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                              VkInstance *pInstance) {
    VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    assert(chain_info->u.pLayerInfo);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (fpCreateInstance == NULL) return VK_ERROR_INITIALIZATION_FAILED;
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    uint32_t specified_version = (pCreateInfo->pApplicationInfo ? pCreateInfo->pApplicationInfo->apiVersion : VK_API_VERSION_1_0);
    uint32_t api_version = (specified_version < VK_API_VERSION_1_1) ? VK_API_VERSION_1_0 : VK_API_VERSION_1_1;
    auto report_data = new debug_report_data{};
    report_data->instance_pnext_chain = SafePnextCopy(pCreateInfo->pNext);
    ActivateInstanceDebugCallbacks(report_data);

    CHECK_ENABLED local_enables {};
    CHECK_DISABLED local_disables {};
    const auto *validation_features_ext = lvl_find_in_chain<VkValidationFeaturesEXT>(pCreateInfo->pNext);
    if (validation_features_ext) {
        SetValidationFeatures(&local_disables, &local_enables, validation_features_ext);
    }
    const auto *validation_flags_ext = lvl_find_in_chain<VkValidationFlagsEXT>(pCreateInfo->pNext);
    if (validation_flags_ext) {
        SetValidationFlags(&local_disables, validation_flags_ext);
    }
    ProcessConfigAndEnvSettings(OBJECT_LAYER_DESCRIPTION, &local_enables, &local_disables);

    // Create temporary dispatch vector for pre-calls until instance is created
    std::vector<ValidationObject*> local_object_dispatch;
    // Add VOs to dispatch vector. Order here will be the validation dispatch order!
    auto thread_checker = new ThreadSafety(nullptr);
    if (!local_disables.thread_safety) {
        local_object_dispatch.emplace_back(thread_checker);
    }
    thread_checker->container_type = LayerObjectTypeThreading;
    thread_checker->api_version = api_version;
    thread_checker->report_data = report_data;
    auto parameter_validation = new StatelessValidation;
    if (!local_disables.stateless_checks) {
        local_object_dispatch.emplace_back(parameter_validation);
    }
    parameter_validation->container_type = LayerObjectTypeParameterValidation;
    parameter_validation->api_version = api_version;
    parameter_validation->report_data = report_data;
    auto object_tracker = new ObjectLifetimes;
    if (!local_disables.object_tracking) {
        local_object_dispatch.emplace_back(object_tracker);
    }
    object_tracker->container_type = LayerObjectTypeObjectTracker;
    object_tracker->api_version = api_version;
    object_tracker->report_data = report_data;
    auto core_checks = new CoreChecks;
    if (!local_disables.core_checks) {
        local_object_dispatch.emplace_back(core_checks);
    }
    core_checks->container_type = LayerObjectTypeCoreValidation;
    core_checks->api_version = api_version;
    core_checks->report_data = report_data;
    auto best_practices = new BestPractices;
    if (local_enables.best_practices) {
        local_object_dispatch.emplace_back(best_practices);
    }
    best_practices->container_type = LayerObjectTypeBestPractices;
    best_practices->api_version = api_version;
    best_practices->report_data = report_data;

    // If handle wrapping is disabled via the ValidationFeatures extension, override build flag
    if (local_disables.handle_wrapping) {
        wrap_handles = false;
    }

    // Init dispatch array and call registration functions
    for (auto intercept : local_object_dispatch) {
        intercept->PreCallValidateCreateInstance(pCreateInfo, pAllocator, pInstance);
    }
    for (auto intercept : local_object_dispatch) {
        intercept->PreCallRecordCreateInstance(pCreateInfo, pAllocator, pInstance);
    }

    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) return result;

    auto framework = GetLayerDataPtr(get_dispatch_key(*pInstance), layer_data_map);

    framework->object_dispatch = local_object_dispatch;
    framework->container_type = LayerObjectTypeInstance;
    framework->disabled = local_disables;
    framework->enabled = local_enables;

    framework->instance = *pInstance;
    layer_init_instance_dispatch_table(*pInstance, &framework->instance_dispatch_table, fpGetInstanceProcAddr);
    framework->report_data = report_data;
    framework->api_version = api_version;
    framework->instance_extensions.InitFromInstanceCreateInfo(specified_version, pCreateInfo);

    layer_debug_messenger_actions(framework->report_data, pAllocator, OBJECT_LAYER_DESCRIPTION);

    object_tracker->instance_dispatch_table = framework->instance_dispatch_table;
    object_tracker->enabled = framework->enabled;
    object_tracker->disabled = framework->disabled;
    thread_checker->instance_dispatch_table = framework->instance_dispatch_table;
    thread_checker->enabled = framework->enabled;
    thread_checker->disabled = framework->disabled;
    parameter_validation->instance_dispatch_table = framework->instance_dispatch_table;
    parameter_validation->enabled = framework->enabled;
    parameter_validation->disabled = framework->disabled;
    core_checks->instance_dispatch_table = framework->instance_dispatch_table;
    core_checks->instance = *pInstance;
    core_checks->enabled = framework->enabled;
    core_checks->disabled = framework->disabled;
    core_checks->instance_state = core_checks;
    best_practices->instance_dispatch_table = framework->instance_dispatch_table;
    best_practices->enabled = framework->enabled;
    best_practices->disabled = framework->disabled;

    for (auto intercept : framework->object_dispatch) {
        intercept->PostCallRecordCreateInstance(pCreateInfo, pAllocator, pInstance, result);
    }

    InstanceExtensionWhitelist(framework, pCreateInfo, *pInstance);
    DeactivateInstanceDebugCallbacks(report_data);
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    dispatch_key key = get_dispatch_key(instance);
    auto layer_data = GetLayerDataPtr(key, layer_data_map);
    ActivateInstanceDebugCallbacks(layer_data->report_data);

    """ + precallvalidate_loop + """
        auto lock = intercept->write_lock();
        intercept->PreCallValidateDestroyInstance(instance, pAllocator);
    }
    """ + precallrecord_loop + """
        auto lock = intercept->write_lock();
        intercept->PreCallRecordDestroyInstance(instance, pAllocator);
    }

    layer_data->instance_dispatch_table.DestroyInstance(instance, pAllocator);

    """ + postcallrecord_loop + """
        auto lock = intercept->write_lock();
        intercept->PostCallRecordDestroyInstance(instance, pAllocator);
    }

    DeactivateInstanceDebugCallbacks(layer_data->report_data);
    FreePnextChain(layer_data->report_data->instance_pnext_chain);

    layer_debug_utils_destroy_instance(layer_data->report_data);

    for (auto item = layer_data->object_dispatch.begin(); item != layer_data->object_dispatch.end(); item++) {
        delete *item;
    }
    FreeLayerDataPtr(key, layer_data_map);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo *pCreateInfo,
                                            const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    VkLayerDeviceCreateInfo *chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);

    auto instance_interceptor = GetLayerDataPtr(get_dispatch_key(gpu), layer_data_map);

    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(instance_interceptor->instance, "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    // Get physical device limits for device
    VkPhysicalDeviceProperties device_properties = {};
    instance_interceptor->instance_dispatch_table.GetPhysicalDeviceProperties(gpu, &device_properties);

    // Setup the validation tables based on the application API version from the instance and the capabilities of the device driver
    uint32_t effective_api_version = std::min(device_properties.apiVersion, instance_interceptor->api_version);

    DeviceExtensions device_extensions = {};
    device_extensions.InitFromDeviceCreateInfo(&instance_interceptor->instance_extensions, effective_api_version, pCreateInfo);
    for (auto item : instance_interceptor->object_dispatch) {
        item->device_extensions = device_extensions;
    }

    safe_VkDeviceCreateInfo modified_create_info(pCreateInfo);

    bool skip = false;
    for (auto intercept : instance_interceptor->object_dispatch) {
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateCreateDevice(gpu, pCreateInfo, pAllocator, pDevice);
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : instance_interceptor->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordCreateDevice(gpu, pCreateInfo, pAllocator, pDevice, &modified_create_info);
    }

    VkResult result = fpCreateDevice(gpu, reinterpret_cast<VkDeviceCreateInfo *>(&modified_create_info), pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    auto device_interceptor = GetLayerDataPtr(get_dispatch_key(*pDevice), layer_data_map);
    device_interceptor->container_type = LayerObjectTypeDevice;

    // Save local info in device object
    device_interceptor->phys_dev_properties.properties = device_properties;
    device_interceptor->api_version = device_interceptor->device_extensions.InitFromDeviceCreateInfo(
        &instance_interceptor->instance_extensions, effective_api_version, pCreateInfo);
    device_interceptor->device_extensions = device_extensions;

    layer_init_device_dispatch_table(*pDevice, &device_interceptor->device_dispatch_table, fpGetDeviceProcAddr);

    device_interceptor->device = *pDevice;
    device_interceptor->physical_device = gpu;
    device_interceptor->instance = instance_interceptor->instance;
    device_interceptor->report_data = instance_interceptor->report_data;

    // Note that this defines the order in which the layer validation objects are called
    auto thread_safety = new ThreadSafety(reinterpret_cast<ThreadSafety *>(instance_interceptor->GetValidationObject(instance_interceptor->object_dispatch, LayerObjectTypeThreading)));
    thread_safety->container_type = LayerObjectTypeThreading;
    if (!instance_interceptor->disabled.thread_safety) {
        device_interceptor->object_dispatch.emplace_back(thread_safety);
    }
    auto stateless_validation = new StatelessValidation;
    stateless_validation->container_type = LayerObjectTypeParameterValidation;
    if (!instance_interceptor->disabled.stateless_checks) {
        device_interceptor->object_dispatch.emplace_back(stateless_validation);
    }
    auto object_tracker = new ObjectLifetimes;
    object_tracker->container_type = LayerObjectTypeObjectTracker;
    if (!instance_interceptor->disabled.object_tracking) {
        device_interceptor->object_dispatch.emplace_back(object_tracker);
    }
    auto core_checks = new CoreChecks;
    core_checks->container_type = LayerObjectTypeCoreValidation;
    core_checks->instance_state = reinterpret_cast<CoreChecks *>(
        core_checks->GetValidationObject(instance_interceptor->object_dispatch, LayerObjectTypeCoreValidation));
    if (!instance_interceptor->disabled.core_checks) {
        device_interceptor->object_dispatch.emplace_back(core_checks);
    }
    auto best_practices = new BestPractices;
    best_practices->container_type = LayerObjectTypeBestPractices;
    best_practices->instance_state = reinterpret_cast<BestPractices *>(
        best_practices->GetValidationObject(instance_interceptor->object_dispatch, LayerObjectTypeBestPractices));
    if (instance_interceptor->enabled.best_practices) {
        device_interceptor->object_dispatch.emplace_back(best_practices);
    }

    // Set per-intercept common data items
    for (auto dev_intercept : device_interceptor->object_dispatch) {
        dev_intercept->device = *pDevice;
        dev_intercept->physical_device = gpu;
        dev_intercept->instance = instance_interceptor->instance;
        dev_intercept->report_data = device_interceptor->report_data;
        dev_intercept->device_dispatch_table = device_interceptor->device_dispatch_table;
        dev_intercept->api_version = device_interceptor->api_version;
        dev_intercept->disabled = instance_interceptor->disabled;
        dev_intercept->enabled = instance_interceptor->enabled;
        dev_intercept->instance_dispatch_table = instance_interceptor->instance_dispatch_table;
        dev_intercept->instance_extensions = instance_interceptor->instance_extensions;
        dev_intercept->device_extensions = device_interceptor->device_extensions;
    }

    for (auto intercept : instance_interceptor->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordCreateDevice(gpu, pCreateInfo, pAllocator, pDevice, result);
    }

    DeviceExtensionWhitelist(device_interceptor, pCreateInfo, *pDevice);

    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    dispatch_key key = get_dispatch_key(device);
    auto layer_data = GetLayerDataPtr(key, layer_data_map);
    """ + precallvalidate_loop + """
        auto lock = intercept->write_lock();
        intercept->PreCallValidateDestroyDevice(device, pAllocator);
    }
    """ + precallrecord_loop + """
        auto lock = intercept->write_lock();
        intercept->PreCallRecordDestroyDevice(device, pAllocator);
    }

    layer_data->device_dispatch_table.DestroyDevice(device, pAllocator);

    """ + postcallrecord_loop + """
        auto lock = intercept->write_lock();
        intercept->PostCallRecordDestroyDevice(device, pAllocator);
    }

    for (auto item = layer_data->object_dispatch.begin(); item != layer_data->object_dispatch.end(); item++) {
        delete *item;
    }
    FreeLayerDataPtr(key, layer_data_map);
}


// Special-case APIs for which core_validation needs custom parameter lists and/or modifies parameters

VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    bool skip = false;

    create_graphics_pipeline_api_state cgpl_state[LayerObjectTypeMaxEnum]{};

    for (auto intercept : layer_data->object_dispatch) {
        cgpl_state[intercept->container_type].pCreateInfos = pCreateInfos;
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, &(cgpl_state[intercept->container_type]));
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, &(cgpl_state[intercept->container_type]));
    }

    auto usepCreateInfos = (!cgpl_state[LayerObjectTypeCoreValidation].pCreateInfos) ? pCreateInfos : cgpl_state[LayerObjectTypeCoreValidation].pCreateInfos; 

    VkResult result = DispatchCreateGraphicsPipelines(device, pipelineCache, createInfoCount, usepCreateInfos, pAllocator, pPipelines);

    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, result, &(cgpl_state[intercept->container_type]));
    }
    return result;
}

// This API saves some core_validation pipeline state state on the stack for performance purposes
VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    bool skip = false;

    create_compute_pipeline_api_state ccpl_state[LayerObjectTypeMaxEnum]{};

    for (auto intercept : layer_data->object_dispatch) {
        ccpl_state[intercept->container_type].pCreateInfos = pCreateInfos;
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, &(ccpl_state[intercept->container_type]));
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, &(ccpl_state[intercept->container_type]));
    }

    auto usepCreateInfos = (!ccpl_state[LayerObjectTypeCoreValidation].pCreateInfos) ? pCreateInfos : ccpl_state[LayerObjectTypeCoreValidation].pCreateInfos; 

    VkResult result = DispatchCreateComputePipelines(device, pipelineCache, createInfoCount, usepCreateInfos, pAllocator, pPipelines);

    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, result, &(ccpl_state[intercept->container_type]));
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateRayTracingPipelinesNV(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkRayTracingPipelineCreateInfoNV*     pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    bool skip = false;

    create_ray_tracing_pipeline_api_state crtpl_state[LayerObjectTypeMaxEnum]{};

    for (auto intercept : layer_data->object_dispatch) {
        crtpl_state[intercept->container_type].pCreateInfos = pCreateInfos;
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos,
                                                                      pAllocator, pPipelines, &(crtpl_state[intercept->container_type]));
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator,
                                                            pPipelines, &(crtpl_state[intercept->container_type]));
    }

    VkResult result = DispatchCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);

    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator,
                                                             pPipelines, result, &(crtpl_state[intercept->container_type]));
    }
    return result;
}

// This API needs the ability to modify a down-chain parameter
VKAPI_ATTR VkResult VKAPI_CALL CreatePipelineLayout(
    VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    bool skip = false;

    create_pipeline_layout_api_state cpl_state{};
    cpl_state.modified_create_info = *pCreateInfo;

    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout, &cpl_state);
    }
    VkResult result = DispatchCreatePipelineLayout(device, &cpl_state.modified_create_info, pAllocator, pPipelineLayout);
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout, result);
    }
    return result;
}

// This API needs some local stack data for performance reasons and also may modify a parameter
VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(
    VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    bool skip = false;

    create_shader_module_api_state csm_state{};
    csm_state.instrumented_create_info = *pCreateInfo;

    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule, &csm_state);
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule, &csm_state);
    }
    VkResult result = DispatchCreateShaderModule(device, &csm_state.instrumented_create_info, pAllocator, pShaderModule);
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule, result, &csm_state);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(
    VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    bool skip = false;

    cvdescriptorset::AllocateDescriptorSetsData ads_state(pAllocateInfo->descriptorSetCount);

    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets, &ads_state);
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
    }
    VkResult result = DispatchAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets, result, &ads_state);
    }
    return result;
}

// This API needs the ability to modify a down-chain parameter
VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(
    VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    bool skip = false;

    create_buffer_api_state cb_state{};
    cb_state.modified_create_info = *pCreateInfo;

    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        skip |= intercept->PreCallValidateCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
        if (skip) return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PreCallRecordCreateBuffer(device, pCreateInfo, pAllocator, pBuffer, &cb_state);
    }
    VkResult result = DispatchCreateBuffer(device, &cb_state.modified_create_info, pAllocator, pBuffer);
    for (auto intercept : layer_data->object_dispatch) {
        auto lock = intercept->write_lock();
        intercept->PostCallRecordCreateBuffer(device, pCreateInfo, pAllocator, pBuffer, result);
    }
    return result;
}



// ValidationCache APIs do not dispatch

VKAPI_ATTR VkResult VKAPI_CALL CreateValidationCacheEXT(
    VkDevice                                    device,
    const VkValidationCacheCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkValidationCacheEXT*                       pValidationCache) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    VkResult result = VK_SUCCESS;

    ValidationObject *validation_data = layer_data->GetValidationObject(layer_data->object_dispatch, LayerObjectTypeCoreValidation);
    if (validation_data) {
        auto lock = validation_data->write_lock();
        result = validation_data->CoreLayerCreateValidationCacheEXT(device, pCreateInfo, pAllocator, pValidationCache);
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroyValidationCacheEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    const VkAllocationCallbacks*                pAllocator) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);

    ValidationObject *validation_data = layer_data->GetValidationObject(layer_data->object_dispatch, LayerObjectTypeCoreValidation);
    if (validation_data) {
        auto lock = validation_data->write_lock();
        validation_data->CoreLayerDestroyValidationCacheEXT(device, validationCache, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL MergeValidationCachesEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        dstCache,
    uint32_t                                    srcCacheCount,
    const VkValidationCacheEXT*                 pSrcCaches) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    VkResult result = VK_SUCCESS;

    ValidationObject *validation_data = layer_data->GetValidationObject(layer_data->object_dispatch, LayerObjectTypeCoreValidation);
    if (validation_data) {
        auto lock = validation_data->write_lock();
        result = validation_data->CoreLayerMergeValidationCachesEXT(device, dstCache, srcCacheCount, pSrcCaches);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL GetValidationCacheDataEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    size_t*                                     pDataSize,
    void*                                       pData) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    VkResult result = VK_SUCCESS;

    ValidationObject *validation_data = layer_data->GetValidationObject(layer_data->object_dispatch, LayerObjectTypeCoreValidation);
    if (validation_data) {
        auto lock = validation_data->write_lock();
        result = validation_data->CoreLayerGetValidationCacheDataEXT(device, validationCache, pDataSize, pData);
    }
    return result;

}"""

    inline_custom_validation_class_definitions = """
        virtual VkResult CoreLayerCreateValidationCacheEXT(VkDevice device, const VkValidationCacheCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkValidationCacheEXT* pValidationCache) { return VK_SUCCESS; };
        virtual void CoreLayerDestroyValidationCacheEXT(VkDevice device, VkValidationCacheEXT validationCache, const VkAllocationCallbacks* pAllocator) {};
        virtual VkResult CoreLayerMergeValidationCachesEXT(VkDevice device, VkValidationCacheEXT dstCache, uint32_t srcCacheCount, const VkValidationCacheEXT* pSrcCaches)  { return VK_SUCCESS; };
        virtual VkResult CoreLayerGetValidationCacheDataEXT(VkDevice device, VkValidationCacheEXT validationCache, size_t* pDataSize, void* pData)  { return VK_SUCCESS; };

        // Allow additional state parameter for CreateGraphicsPipelines
        virtual bool PreCallValidateCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, void* cgpl_state) {
            return PreCallValidateCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        };
        virtual void PreCallRecordCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, void* cgpl_state) {
            PreCallRecordCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        };
        virtual void PostCallRecordCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, VkResult result, void* cgpl_state) {
            PostCallRecordCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, result);
        };

        // Allow additional state parameter for CreateComputePipelines
        virtual bool PreCallValidateCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, void* pipe_state)  {
            return PreCallValidateCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        };
        virtual void PreCallRecordCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, void* ccpl_state) {
            PreCallRecordCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        };
        virtual void PostCallRecordCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, VkResult result, void* pipe_state) {
            PostCallRecordCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, result);
        };

        // Allow additional state parameter for CreateRayTracingPipelinesNV
        virtual bool PreCallValidateCreateRayTracingPipelinesNV(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoNV* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, void* pipe_state)  {
            return PreCallValidateCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        };
        virtual void PreCallRecordCreateRayTracingPipelinesNV(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoNV* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, void* ccpl_state) {
            PreCallRecordCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
        };
        virtual void PostCallRecordCreateRayTracingPipelinesNV(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoNV* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines, VkResult result, void* pipe_state) {
            PostCallRecordCreateRayTracingPipelinesNV(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, result);
        };

        // Allow modification of a down-chain parameter for CreatePipelineLayout
        virtual void PreCallRecordCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout, void *cpl_state) {
            PreCallRecordCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
        };

        // Enable the CreateShaderModule API to take an extra argument for state preservation and paramter modification
        virtual bool PreCallValidateCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule, void* csm_state)  {
            return PreCallValidateCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
        };
        virtual void PreCallRecordCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule, void* csm_state) {
            PreCallRecordCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
        };
        virtual void PostCallRecordCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule, VkResult result, void* csm_state) {
            PostCallRecordCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule, result);
        };

        // Allow AllocateDescriptorSets to use some local stack storage for performance purposes
        virtual bool PreCallValidateAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets, void* ads_state)  {
            return PreCallValidateAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
        };
        virtual void PostCallRecordAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets, VkResult result, void* ads_state)  {
            PostCallRecordAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets, result);
        };

        // Allow modification of a down-chain parameter for CreateBuffer
        virtual void PreCallRecordCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer, void *cb_state) {
            PreCallRecordCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
        };

        // Modify a parameter to CreateDevice
        virtual void PreCallRecordCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice, safe_VkDeviceCreateInfo *modified_create_info) {
            PreCallRecordCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
        };
"""

    inline_custom_source_postamble = """
// loader-layer interface v0, just wrappers since there is only a layer

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount,
                                                                                      VkExtensionProperties *pProperties) {
    return vulkan_layer_chassis::EnumerateInstanceExtensionProperties(pLayerName, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t *pCount,
                                                                                  VkLayerProperties *pProperties) {
    return vulkan_layer_chassis::EnumerateInstanceLayerProperties(pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                                                                VkLayerProperties *pProperties) {
    // the layer command handles VK_NULL_HANDLE just fine internally
    assert(physicalDevice == VK_NULL_HANDLE);
    return vulkan_layer_chassis::EnumerateDeviceLayerProperties(VK_NULL_HANDLE, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                                    const char *pLayerName, uint32_t *pCount,
                                                                                    VkExtensionProperties *pProperties) {
    // the layer command handles VK_NULL_HANDLE just fine internally
    assert(physicalDevice == VK_NULL_HANDLE);
    return vulkan_layer_chassis::EnumerateDeviceExtensionProperties(VK_NULL_HANDLE, pLayerName, pCount, pProperties);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *funcName) {
    return vulkan_layer_chassis::GetDeviceProcAddr(dev, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *funcName) {
    return vulkan_layer_chassis::GetInstanceProcAddr(instance, funcName);
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct) {
    assert(pVersionStruct != NULL);
    assert(pVersionStruct->sType == LAYER_NEGOTIATE_INTERFACE_STRUCT);

    // Fill in the function pointers if our version is at least capable of having the structure contain them.
    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    }

    return VK_SUCCESS;
}"""


    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        # Internal state - accumulators for different inner block text
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])
        self.intercepts = []
        self.layer_factory = ''                     # String containing base layer factory class definition

    # Check if the parameter passed in is a pointer to an array
    def paramIsArray(self, param):
        return param.attrib.get('len') is not None

    # Check if the parameter passed in is a pointer
    def paramIsPointer(self, param):
        ispointer = False
        for elem in param:
            if elem.tag == 'type' and elem.tail is not None and '*' in elem.tail:
                ispointer = True
        return ispointer

    #
    #
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        # Output Copyright
        write(self.inline_copyright_message, file=self.outFile)
        # Multiple inclusion protection
        self.header = False
        if (self.genOpts.filename and 'h' == self.genOpts.filename[-1]):
            self.header = True
            write('#pragma once', file=self.outFile)
            self.newline()
        if self.header:
            write(self.inline_custom_header_preamble, file=self.outFile)
        else:
            write(self.inline_custom_source_preamble, file=self.outFile)
        self.layer_factory += self.inline_custom_header_class_definition
    #
    #
    def endFile(self):
        # Finish C++ namespace and multiple inclusion protection
        self.newline()
        if not self.header:
            # Record intercepted procedures
            write('// Map of intercepted ApiName to its associated function data', file=self.outFile)
            write('const std::unordered_map<std::string, function_data> name_to_funcptr_map = {', file=self.outFile)
            write('\n'.join(self.intercepts), file=self.outFile)
            write('};\n', file=self.outFile)
            self.newline()
            write('} // namespace vulkan_layer_chassis', file=self.outFile)
        if self.header:
            self.newline()
            # Output Layer Factory Class Definitions
            self.layer_factory += self.inline_custom_validation_class_definitions
            self.layer_factory += '};\n\n'
            self.layer_factory += 'extern small_unordered_map<void*, ValidationObject*, 2> layer_data_map;'
            write(self.layer_factory, file=self.outFile)
        else:
            write(self.inline_custom_source_postamble, file=self.outFile)
        # Finish processing in superclass
        OutputGenerator.endFile(self)

    def beginFeature(self, interface, emit):
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        # Get feature extra protect
        self.featureExtraProtect = GetFeatureProtect(interface)
        # Accumulate includes, defines, types, enums, function pointer typedefs, end function prototypes separately for this
        # feature. They're only printed in endFeature().
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])

    def endFeature(self):
        # Actually write the interface to the output file.
        if (self.emit):
            self.newline()
            # If type declarations are needed by other features based on this one, it may be necessary to suppress the ExtraProtect,
            # or move it below the 'for section...' loop.
            if (self.featureExtraProtect != None):
                write('#ifdef', self.featureExtraProtect, file=self.outFile)
            for section in self.TYPE_SECTIONS:
                contents = self.sections[section]
                if contents:
                    write('\n'.join(contents), file=self.outFile)
                    self.newline()
            if (self.sections['command']):
                write('\n'.join(self.sections['command']), end=u'', file=self.outFile)
                self.newline()
            if (self.featureExtraProtect != None):
                write('#endif //', self.featureExtraProtect, file=self.outFile)
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    # Append a definition to the specified section
    def appendSection(self, section, text):
        self.sections[section].append(text)
    #
    # Type generation
    def genType(self, typeinfo, name, alias):
        pass
    #
    # Struct (e.g. C "struct" type) generation. This is a special case of the <type> tag where the contents are
    # interpreted as a set of <member> tags instead of freeform C type declarations. The <member> tags are just like <param>
    # tags - they are a declaration of a struct or union member. Only simple member declarations are supported (no nested
    # structs etc.)
    def genStruct(self, typeinfo, typeName):
        OutputGenerator.genStruct(self, typeinfo, typeName)
        body = 'typedef ' + typeinfo.elem.get('category') + ' ' + typeName + ' {\n'
        # paramdecl = self.makeCParamDecl(typeinfo.elem, self.genOpts.alignFuncParam)
        for member in typeinfo.elem.findall('.//member'):
            body += self.makeCParamDecl(member, self.genOpts.alignFuncParam)
            body += ';\n'
        body += '} ' + typeName + ';\n'
        self.appendSection('struct', body)
    #
    # Group (e.g. C "enum" type) generation. These are concatenated together with other types.
    def genGroup(self, groupinfo, groupName, alias):
        pass
    # Enumerant generation
    # <enum> tags may specify their values in several ways, but are usually just integers.
    def genEnum(self, enuminfo, name, alias):
        pass
    #
    # Customize Cdecl for layer factory base class
    def BaseClassCdecl(self, elem, name):
        raw = self.makeCDecls(elem)[1]

        # Toss everything before the undecorated name
        prototype = raw.split("VKAPI_PTR *PFN_vk")[1]
        prototype = prototype.replace(")", "", 1)
        prototype = prototype.replace(";", " {};")

        # Build up pre/post call virtual function declarations
        pre_call_validate = 'virtual bool PreCallValidate' + prototype
        pre_call_validate = pre_call_validate.replace("{}", " { return false; }")
        pre_call_record = 'virtual void PreCallRecord' + prototype
        post_call_record = 'virtual void PostCallRecord' + prototype
        resulttype = elem.find('proto/type')
        if resulttype.text == 'VkResult':
            post_call_record = post_call_record.replace(')', ', VkResult result)')
        elif resulttype.text == 'VkDeviceAddress':
            post_call_record = post_call_record.replace(')', ', VkDeviceAddress result)')
        return '        %s\n        %s\n        %s\n' % (pre_call_validate, pre_call_record, post_call_record)
    #
    # Command generation
    def genCmd(self, cmdinfo, name, alias):
        ignore_functions = [
        'vkEnumerateInstanceVersion',
        ]

        if name in ignore_functions:
            return

        if self.header: # In the header declare all intercepts
            self.appendSection('command', '')
            self.appendSection('command', self.makeCDecls(cmdinfo.elem)[0])
            if (self.featureExtraProtect != None):
                self.layer_factory += '#ifdef %s\n' % self.featureExtraProtect
            # Update base class with virtual function declarations
            if 'ValidationCache' not in name:
                self.layer_factory += self.BaseClassCdecl(cmdinfo.elem, name)
            if (self.featureExtraProtect != None):
                self.layer_factory += '#endif\n'
            return

        is_instance = 'false'
        dispatchable_type = cmdinfo.elem.find('param/type').text
        if dispatchable_type in ["VkPhysicalDevice", "VkInstance"] or name == 'vkCreateInstance':
            is_instance = 'true'

        if name in self.manual_functions:
            self.intercepts += [ '    {"%s", {%s, (void*)%s}},' % (name, is_instance, name[2:]) ]
            return
        # Record that the function will be intercepted
        if (self.featureExtraProtect != None):
            self.intercepts += [ '#ifdef %s' % self.featureExtraProtect ]
        self.intercepts += [ '    {"%s", {%s, (void*)%s}},' % (name, is_instance, name[2:]) ]
        if (self.featureExtraProtect != None):
            self.intercepts += [ '#endif' ]
        OutputGenerator.genCmd(self, cmdinfo, name, alias)
        #
        decls = self.makeCDecls(cmdinfo.elem)
        self.appendSection('command', '')
        self.appendSection('command', '%s {' % decls[0][:-1])
        # Setup common to call wrappers. First parameter is always dispatchable
        dispatchable_name = cmdinfo.elem.find('param/name').text
        self.appendSection('command', '    auto layer_data = GetLayerDataPtr(get_dispatch_key(%s), layer_data_map);' % (dispatchable_name))
        api_function_name = cmdinfo.elem.attrib.get('name')
        params = cmdinfo.elem.findall('param/name')
        paramstext = ', '.join([str(param.text) for param in params])
        API = api_function_name.replace('vk','Dispatch') + '('

        # Declare result variable, if any.
        return_map = {
            'PFN_vkVoidFunction': 'return nullptr;',
            'VkBool32': 'return VK_FALSE;',
            'VkDeviceAddress': 'return 0;',
            'VkResult': 'return VK_ERROR_VALIDATION_FAILED_EXT;',
            'void': 'return;',
            'uint32_t': 'return 0;'
            }
        resulttype = cmdinfo.elem.find('proto/type')
        assignresult = ''
        if (resulttype.text != 'void'):
            assignresult = resulttype.text + ' result = '

        # Set up skip and locking
        self.appendSection('command', '    bool skip = false;')

        # Generate pre-call validation source code
        self.appendSection('command', '    %s' % self.precallvalidate_loop)
        self.appendSection('command', '        auto lock = intercept->write_lock();')
        self.appendSection('command', '        skip |= intercept->PreCallValidate%s(%s);' % (api_function_name[2:], paramstext))
        self.appendSection('command', '        if (skip) %s' % return_map[resulttype.text])
        self.appendSection('command', '    }')

        # Generate pre-call state recording source code
        self.appendSection('command', '    %s' % self.precallrecord_loop)
        self.appendSection('command', '        auto lock = intercept->write_lock();')
        self.appendSection('command', '        intercept->PreCallRecord%s(%s);' % (api_function_name[2:], paramstext))
        self.appendSection('command', '    }')

        # Insert pre-dispatch debug utils function call
        if name in self.pre_dispatch_debug_utils_functions:
            self.appendSection('command', '    %s' % self.pre_dispatch_debug_utils_functions[name])

        # Output dispatch (down-chain) function call
        self.appendSection('command', '    ' + assignresult + API + paramstext + ');')

        # Insert post-dispatch debug utils function call
        if name in self.post_dispatch_debug_utils_functions:
            self.appendSection('command', '    %s' % self.post_dispatch_debug_utils_functions[name])

        # Generate post-call object processing source code
        self.appendSection('command', '    %s' % self.postcallrecord_loop)
        returnparam = ''
        if (resulttype.text == 'VkResult' or resulttype.text == 'VkDeviceAddress'):
            returnparam = ', result'
        self.appendSection('command', '        auto lock = intercept->write_lock();')
        self.appendSection('command', '        intercept->PostCallRecord%s(%s%s);' % (api_function_name[2:], paramstext, returnparam))
        self.appendSection('command', '    }')
        # Return result variable, if any.
        if (resulttype.text != 'void'):
            self.appendSection('command', '    return result;')
        self.appendSection('command', '}')
    #
    # Override makeProtoName to drop the "vk" prefix
    def makeProtoName(self, name, tail):
        return self.genOpts.apientry + name[2:] + tail
