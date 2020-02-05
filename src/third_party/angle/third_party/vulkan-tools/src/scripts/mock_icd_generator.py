#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2017 The Khronos Group Inc.
# Copyright (c) 2015-2017 Valve Corporation
# Copyright (c) 2015-2017 LunarG, Inc.
# Copyright (c) 2015-2017 Google Inc.
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
#
# This script generates a Mock ICD that intercepts almost all Vulkan
#  functions. That layer is not intended to be useful or even compilable
#  in its initial state. Rather it's intended to be a starting point that
#  can be copied and customized to assist in creation of a new layer.

import os,re,sys
from generator import *
from common_codegen import *


# Mock header code
HEADER_C_CODE = '''
using mutex_t = std::mutex;
using lock_guard_t = std::lock_guard<mutex_t>;
using unique_lock_t = std::unique_lock<mutex_t>;

static mutex_t global_lock;
static uint64_t global_unique_handle = 1;
static const uint32_t SUPPORTED_LOADER_ICD_INTERFACE_VERSION = 5;
static uint32_t loader_interface_version = 0;
static bool negotiate_loader_icd_interface_called = false;
static void* CreateDispObjHandle() {
    auto handle = new VK_LOADER_DATA;
    set_loader_magic_value(handle);
    return handle;
}
static void DestroyDispObjHandle(void* handle) {
    delete reinterpret_cast<VK_LOADER_DATA*>(handle);
}
'''

# Manual code at the top of the cpp source file
SOURCE_CPP_PREFIX = '''
using std::unordered_map;

// Map device memory handle to any mapped allocations that we'll need to free on unmap
static unordered_map<VkDeviceMemory, std::vector<void*>> mapped_memory_map;

static VkPhysicalDevice physical_device = nullptr;
static unordered_map<VkDevice, unordered_map<uint32_t, unordered_map<uint32_t, VkQueue>>> queue_map;
static unordered_map<VkDevice, unordered_map<VkBuffer, VkBufferCreateInfo>> buffer_map;

// TODO: Would like to codegen this but limits aren't in XML
static VkPhysicalDeviceLimits SetLimits(VkPhysicalDeviceLimits *limits) {
    limits->maxImageDimension1D = 4096;
    limits->maxImageDimension2D = 4096;
    limits->maxImageDimension3D = 256;
    limits->maxImageDimensionCube = 4096;
    limits->maxImageArrayLayers = 256;
    limits->maxTexelBufferElements = 65536;
    limits->maxUniformBufferRange = 16384;
    limits->maxStorageBufferRange = 134217728;
    limits->maxPushConstantsSize = 128;
    limits->maxMemoryAllocationCount = 4096;
    limits->maxSamplerAllocationCount = 4000;
    limits->bufferImageGranularity = 1;
    limits->sparseAddressSpaceSize = 2147483648;
    limits->maxBoundDescriptorSets = 4;
    limits->maxPerStageDescriptorSamplers = 16;
    limits->maxPerStageDescriptorUniformBuffers = 12;
    limits->maxPerStageDescriptorStorageBuffers = 4;
    limits->maxPerStageDescriptorSampledImages = 16;
    limits->maxPerStageDescriptorStorageImages = 4;
    limits->maxPerStageDescriptorInputAttachments = 4;
    limits->maxPerStageResources = 128;
    limits->maxDescriptorSetSamplers = 96;
    limits->maxDescriptorSetUniformBuffers = 72;
    limits->maxDescriptorSetUniformBuffersDynamic = 8;
    limits->maxDescriptorSetStorageBuffers = 24;
    limits->maxDescriptorSetStorageBuffersDynamic = 4;
    limits->maxDescriptorSetSampledImages = 96;
    limits->maxDescriptorSetStorageImages = 24;
    limits->maxDescriptorSetInputAttachments = 4;
    limits->maxVertexInputAttributes = 16;
    limits->maxVertexInputBindings = 16;
    limits->maxVertexInputAttributeOffset = 2047;
    limits->maxVertexInputBindingStride = 2048;
    limits->maxVertexOutputComponents = 64;
    limits->maxTessellationGenerationLevel = 64;
    limits->maxTessellationPatchSize = 32;
    limits->maxTessellationControlPerVertexInputComponents = 64;
    limits->maxTessellationControlPerVertexOutputComponents = 64;
    limits->maxTessellationControlPerPatchOutputComponents = 120;
    limits->maxTessellationControlTotalOutputComponents = 2048;
    limits->maxTessellationEvaluationInputComponents = 64;
    limits->maxTessellationEvaluationOutputComponents = 64;
    limits->maxGeometryShaderInvocations = 32;
    limits->maxGeometryInputComponents = 64;
    limits->maxGeometryOutputComponents = 64;
    limits->maxGeometryOutputVertices = 256;
    limits->maxGeometryTotalOutputComponents = 1024;
    limits->maxFragmentInputComponents = 64;
    limits->maxFragmentOutputAttachments = 4;
    limits->maxFragmentDualSrcAttachments = 1;
    limits->maxFragmentCombinedOutputResources = 4;
    limits->maxComputeSharedMemorySize = 16384;
    limits->maxComputeWorkGroupCount[0] = 65535;
    limits->maxComputeWorkGroupCount[1] = 65535;
    limits->maxComputeWorkGroupCount[2] = 65535;
    limits->maxComputeWorkGroupInvocations = 128;
    limits->maxComputeWorkGroupSize[0] = 128;
    limits->maxComputeWorkGroupSize[1] = 128;
    limits->maxComputeWorkGroupSize[2] = 64;
    limits->subPixelPrecisionBits = 4;
    limits->subTexelPrecisionBits = 4;
    limits->mipmapPrecisionBits = 4;
    limits->maxDrawIndexedIndexValue = UINT32_MAX;
    limits->maxDrawIndirectCount = UINT16_MAX;
    limits->maxSamplerLodBias = 2.0f;
    limits->maxSamplerAnisotropy = 16;
    limits->maxViewports = 16;
    limits->maxViewportDimensions[0] = 4096;
    limits->maxViewportDimensions[1] = 4096;
    limits->viewportBoundsRange[0] = -8192;
    limits->viewportBoundsRange[1] = 8191;
    limits->viewportSubPixelBits = 0;
    limits->minMemoryMapAlignment = 64;
    limits->minTexelBufferOffsetAlignment = 16;
    limits->minUniformBufferOffsetAlignment = 16;
    limits->minStorageBufferOffsetAlignment = 16;
    limits->minTexelOffset = -8;
    limits->maxTexelOffset = 7;
    limits->minTexelGatherOffset = -8;
    limits->maxTexelGatherOffset = 7;
    limits->minInterpolationOffset = 0.0f;
    limits->maxInterpolationOffset = 0.5f;
    limits->subPixelInterpolationOffsetBits = 4;
    limits->maxFramebufferWidth = 4096;
    limits->maxFramebufferHeight = 4096;
    limits->maxFramebufferLayers = 256;
    limits->framebufferColorSampleCounts = 0x7F;
    limits->framebufferDepthSampleCounts = 0x7F;
    limits->framebufferStencilSampleCounts = 0x7F;
    limits->framebufferNoAttachmentsSampleCounts = 0x7F;
    limits->maxColorAttachments = 4;
    limits->sampledImageColorSampleCounts = 0x7F;
    limits->sampledImageIntegerSampleCounts = 0x7F;
    limits->sampledImageDepthSampleCounts = 0x7F;
    limits->sampledImageStencilSampleCounts = 0x7F;
    limits->storageImageSampleCounts = 0x7F;
    limits->maxSampleMaskWords = 1;
    limits->timestampComputeAndGraphics = VK_TRUE;
    limits->timestampPeriod = 1;
    limits->maxClipDistances = 8;
    limits->maxCullDistances = 8;
    limits->maxCombinedClipAndCullDistances = 8;
    limits->discreteQueuePriorities = 2;
    limits->pointSizeRange[0] = 1.0f;
    limits->pointSizeRange[1] = 64.0f;
    limits->lineWidthRange[0] = 1.0f;
    limits->lineWidthRange[1] = 8.0f;
    limits->pointSizeGranularity = 1.0f;
    limits->lineWidthGranularity = 1.0f;
    limits->strictLines = VK_TRUE;
    limits->standardSampleLocations = VK_TRUE;
    limits->optimalBufferCopyOffsetAlignment = 1;
    limits->optimalBufferCopyRowPitchAlignment = 1;
    limits->nonCoherentAtomSize = 256;

    return *limits;
}

void SetBoolArrayTrue(VkBool32* bool_array, uint32_t num_bools)
{
    for (uint32_t i = 0; i < num_bools; ++i) {
        bool_array[i] = VK_TRUE;
    }
}
'''

# Manual code at the end of the cpp source file
SOURCE_CPP_POSTFIX = '''

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetPhysicalDeviceProcAddr(VkInstance instance, const char *funcName) {
    // TODO: This function should only care about physical device functions and return nullptr for other functions
    const auto &item = name_to_funcptr_map.find(funcName);
    if (item != name_to_funcptr_map.end()) {
        return reinterpret_cast<PFN_vkVoidFunction>(item->second);
    }
    // Mock should intercept all functions so if we get here just return null
    return nullptr;
}

} // namespace vkmock

#if defined(__GNUC__) && __GNUC__ >= 4
#define EXPORT __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

extern "C" {

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!vkmock::negotiate_loader_icd_interface_called) {
        vkmock::loader_interface_version = 1;
    }
    return vkmock::GetInstanceProcAddr(instance, pName);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    return vkmock::GetPhysicalDeviceProcAddr(instance, pName);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    vkmock::negotiate_loader_icd_interface_called = true;
    vkmock::loader_interface_version = *pSupportedVersion;
    if (*pSupportedVersion > vkmock::SUPPORTED_LOADER_ICD_INTERFACE_VERSION) {
        *pSupportedVersion = vkmock::SUPPORTED_LOADER_ICD_INTERFACE_VERSION;
    }
    return VK_SUCCESS;
}


EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance                                  instance,
    VkSurfaceKHR                                surface,
    const VkAllocationCallbacks*                pAllocator)
{
    vkmock::DestroySurfaceKHR(instance, surface, pAllocator);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    VkSurfaceKHR                                surface,
    VkBool32*                                   pSupported)
{
    return vkmock::GetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
    return vkmock::GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pSurfaceCapabilities);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats)
{
    return vkmock::GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes)
{
    return vkmock::GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, pPresentModes);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(
    VkInstance                                  instance,
    const VkDisplaySurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateDisplayPlaneSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}

#ifdef VK_USE_PLATFORM_XLIB_KHR

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(
    VkInstance                                  instance,
    const VkXlibSurfaceCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateXlibSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_XLIB_KHR */

#ifdef VK_USE_PLATFORM_XCB_KHR

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(
    VkInstance                                  instance,
    const VkXcbSurfaceCreateInfoKHR*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateXcbSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_XCB_KHR */

#ifdef VK_USE_PLATFORM_WAYLAND_KHR

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateWaylandSurfaceKHR(
    VkInstance                                  instance,
    const VkWaylandSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_WAYLAND_KHR */

#ifdef VK_USE_PLATFORM_ANDROID_KHR

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateAndroidSurfaceKHR(
    VkInstance                                  instance,
    const VkAndroidSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateAndroidSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_ANDROID_KHR */

#ifdef VK_USE_PLATFORM_WIN32_KHR

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(
    VkInstance                                  instance,
    const VkWin32SurfaceCreateInfoKHR*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_WIN32_KHR */

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(
    VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHR*           pModes)
{
    return vkmock::GetDeviceGroupSurfacePresentModesKHR(device, surface, pModes);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pRectCount,
    VkRect2D*                                   pRects)
{
    return vkmock::GetPhysicalDevicePresentRectanglesKHR(physicalDevice, surface, pRectCount, pRects);
}

#ifdef VK_USE_PLATFORM_VI_NN

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateViSurfaceNN(
    VkInstance                                  instance,
    const VkViSurfaceCreateInfoNN*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateViSurfaceNN(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_VI_NN */

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilities2EXT*                  pSurfaceCapabilities)
{
    return vkmock::GetPhysicalDeviceSurfaceCapabilities2EXT(physicalDevice, surface, pSurfaceCapabilities);
}

#ifdef VK_USE_PLATFORM_IOS_MVK

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateIOSSurfaceMVK(
    VkInstance                                  instance,
    const VkIOSSurfaceCreateInfoMVK*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateIOSSurfaceMVK(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_IOS_MVK */

#ifdef VK_USE_PLATFORM_MACOS_MVK

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateMacOSSurfaceMVK(
    VkInstance                                  instance,
    const VkMacOSSurfaceCreateInfoMVK*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    return vkmock::CreateMacOSSurfaceMVK(instance, pCreateInfo, pAllocator, pSurface);
}
#endif /* VK_USE_PLATFORM_MACOS_MVK */

} // end extern "C"

'''

CUSTOM_C_INTERCEPTS = {
'vkCreateInstance': '''
    // TODO: If loader ver <=4 ICD must fail with VK_ERROR_INCOMPATIBLE_DRIVER for all vkCreateInstance calls with
    //  apiVersion set to > Vulkan 1.0 because the loader is still at interface version <= 4. Otherwise, the
    //  ICD should behave as normal.
    if (loader_interface_version <= 4) {
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    *pInstance = (VkInstance)CreateDispObjHandle();
    // TODO: If emulating specific device caps, will need to add intelligence here
    return VK_SUCCESS;
''',
'vkDestroyInstance': '''
    // Destroy physical device
    DestroyDispObjHandle((void*)physical_device);

    DestroyDispObjHandle((void*)instance);
''',
'vkEnumeratePhysicalDevices': '''
    if (pPhysicalDevices) {
        if (!physical_device) {
            physical_device = (VkPhysicalDevice)CreateDispObjHandle();
        }
        *pPhysicalDevices = physical_device;
    } else {
        *pPhysicalDeviceCount = 1;
    }
    return VK_SUCCESS;
''',
'vkCreateDevice': '''
    *pDevice = (VkDevice)CreateDispObjHandle();
    // TODO: If emulating specific device caps, will need to add intelligence here
    return VK_SUCCESS;
''',
'vkDestroyDevice': '''
    unique_lock_t lock(global_lock);
    // First destroy sub-device objects
    // Destroy Queues
    for (auto dev_queue_map_pair : queue_map) {
        for (auto queue_family_map_pair : queue_map[dev_queue_map_pair.first]) {
            for (auto index_queue_pair : queue_map[dev_queue_map_pair.first][queue_family_map_pair.first]) {
                DestroyDispObjHandle((void*)index_queue_pair.second);
            }
        }
    }
    queue_map.clear();
    // Now destroy device
    DestroyDispObjHandle((void*)device);
    // TODO: If emulating specific device caps, will need to add intelligence here
''',
'vkGetDeviceQueue': '''
    unique_lock_t lock(global_lock);
    auto queue = queue_map[device][queueFamilyIndex][queueIndex];
    if (queue) {
        *pQueue = queue;
    } else {
        *pQueue = queue_map[device][queueFamilyIndex][queueIndex] = (VkQueue)CreateDispObjHandle();
    }
    // TODO: If emulating specific device caps, will need to add intelligence here
    return;
''',
'vkGetDeviceQueue2': '''
    GetDeviceQueue(device, pQueueInfo->queueFamilyIndex, pQueueInfo->queueIndex, pQueue);
    // TODO: Add further support for GetDeviceQueue2 features
''',
'vkEnumerateInstanceLayerProperties': '''
    return VK_SUCCESS;
''',
'vkEnumerateDeviceLayerProperties': '''
    return VK_SUCCESS;
''',
'vkEnumerateInstanceExtensionProperties': '''
    // If requesting number of extensions, return that
    if (!pLayerName) {
        if (!pProperties) {
            *pPropertyCount = (uint32_t)instance_extension_map.size();
        } else {
            uint32_t i = 0;
            for (const auto &name_ver_pair : instance_extension_map) {
                if (i == *pPropertyCount) {
                    break;
                }
                std::strncpy(pProperties[i].extensionName, name_ver_pair.first.c_str(), sizeof(pProperties[i].extensionName));
                pProperties[i].extensionName[sizeof(pProperties[i].extensionName) - 1] = 0;
                pProperties[i].specVersion = name_ver_pair.second;
                ++i;
            }
            if (i != instance_extension_map.size()) {
                return VK_INCOMPLETE;
            }
        }
    }
    // If requesting extension properties, fill in data struct for number of extensions
    return VK_SUCCESS;
''',
'vkEnumerateDeviceExtensionProperties': '''
    // If requesting number of extensions, return that
    if (!pLayerName) {
        if (!pProperties) {
            *pPropertyCount = (uint32_t)device_extension_map.size();
        } else {
            uint32_t i = 0;
            for (const auto &name_ver_pair : device_extension_map) {
                if (i == *pPropertyCount) {
                    break;
                }
                std::strncpy(pProperties[i].extensionName, name_ver_pair.first.c_str(), sizeof(pProperties[i].extensionName));
                pProperties[i].extensionName[sizeof(pProperties[i].extensionName) - 1] = 0;
                pProperties[i].specVersion = name_ver_pair.second;
                ++i;
            }
            if (i != device_extension_map.size()) {
                return VK_INCOMPLETE;
            }
        }
    }
    // If requesting extension properties, fill in data struct for number of extensions
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceSurfacePresentModesKHR': '''
    // Currently always say that all present modes are supported
    if (!pPresentModes) {
        *pPresentModeCount = 6;
    } else {
        // Intentionally falling through and just filling however many modes are requested
        switch(*pPresentModeCount) {
        case 6:
            pPresentModes[5] = VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
            // fall through
        case 5:
            pPresentModes[4] = VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
            // fall through
        case 4:
            pPresentModes[3] = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            // fall through
        case 3:
            pPresentModes[2] = VK_PRESENT_MODE_FIFO_KHR;
            // fall through
        case 2:
            pPresentModes[1] = VK_PRESENT_MODE_MAILBOX_KHR;
            // fall through
        default:
            pPresentModes[0] = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        }
    }
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceSurfaceFormatsKHR': '''
    // Currently always say that RGBA8 & BGRA8 are supported
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = 2;
    } else {
        // Intentionally falling through and just filling however many types are requested
        switch(*pSurfaceFormatCount) {
        case 2:
            pSurfaceFormats[1].format = VK_FORMAT_R8G8B8A8_UNORM;
            pSurfaceFormats[1].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            // fall through
        default:
            pSurfaceFormats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
            pSurfaceFormats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            break;
        }
    }
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceSurfaceFormats2KHR': '''
    // Currently always say that RGBA8 & BGRA8 are supported
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = 2;
    } else {
        // Intentionally falling through and just filling however many types are requested
        switch(*pSurfaceFormatCount) {
        case 2:
            pSurfaceFormats[1].pNext = nullptr;
            pSurfaceFormats[1].surfaceFormat.format = VK_FORMAT_R8G8B8A8_UNORM;
            pSurfaceFormats[1].surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            // fall through
        default:
            pSurfaceFormats[1].pNext = nullptr;
            pSurfaceFormats[0].surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
            pSurfaceFormats[0].surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            break;
        }
    }
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceSurfaceSupportKHR': '''
    // Currently say that all surface/queue combos are supported
    *pSupported = VK_TRUE;
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceSurfaceCapabilitiesKHR': '''
    // In general just say max supported is available for requested surface
    pSurfaceCapabilities->minImageCount = 1;
    pSurfaceCapabilities->maxImageCount = 0;
    pSurfaceCapabilities->currentExtent.width = 0xFFFFFFFF;
    pSurfaceCapabilities->currentExtent.height = 0xFFFFFFFF;
    pSurfaceCapabilities->minImageExtent.width = 1;
    pSurfaceCapabilities->minImageExtent.height = 1;
    pSurfaceCapabilities->maxImageExtent.width = 3840;
    pSurfaceCapabilities->maxImageExtent.height = 2160;
    pSurfaceCapabilities->maxImageArrayLayers = 128;
    pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR |
                                                VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
                                                    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR |
                                                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR |
                                                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                VK_IMAGE_USAGE_SAMPLED_BIT |
                                                VK_IMAGE_USAGE_STORAGE_BIT |
                                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                                VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceSurfaceCapabilities2KHR': '''
    GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, pSurfaceInfo->surface, &pSurfaceCapabilities->surfaceCapabilities);
    return VK_SUCCESS;
''',
'vkGetInstanceProcAddr': '''
    if (!negotiate_loader_icd_interface_called) {
        loader_interface_version = 0;
    }
    const auto &item = name_to_funcptr_map.find(pName);
    if (item != name_to_funcptr_map.end()) {
        return reinterpret_cast<PFN_vkVoidFunction>(item->second);
    }
    // Mock should intercept all functions so if we get here just return null
    return nullptr;
''',
'vkGetDeviceProcAddr': '''
    return GetInstanceProcAddr(nullptr, pName);
''',
'vkGetPhysicalDeviceMemoryProperties': '''
    pMemoryProperties->memoryTypeCount = 2;
    pMemoryProperties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pMemoryProperties->memoryTypes[0].heapIndex = 0;
    pMemoryProperties->memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    pMemoryProperties->memoryTypes[1].heapIndex = 1;
    pMemoryProperties->memoryHeapCount = 2;
    pMemoryProperties->memoryHeaps[0].flags = 0;
    pMemoryProperties->memoryHeaps[0].size = 8000000000;
    pMemoryProperties->memoryHeaps[1].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    pMemoryProperties->memoryHeaps[1].size = 8000000000;
''',
'vkGetPhysicalDeviceMemoryProperties2KHR': '''
    GetPhysicalDeviceMemoryProperties(physicalDevice, &pMemoryProperties->memoryProperties);
''',
'vkGetPhysicalDeviceQueueFamilyProperties': '''
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = 1;
    } else {
        if (*pQueueFamilyPropertyCount) {
            pQueueFamilyProperties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT;
            pQueueFamilyProperties[0].queueCount = 1;
            pQueueFamilyProperties[0].timestampValidBits = 0;
            pQueueFamilyProperties[0].minImageTransferGranularity = {1,1,1};
        }
    }
''',
'vkGetPhysicalDeviceQueueFamilyProperties2KHR': '''
    if (pQueueFamilyPropertyCount && pQueueFamilyProperties) {
        GetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, &pQueueFamilyProperties->queueFamilyProperties);
    } else {
        GetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, nullptr);
    }
''',
'vkGetPhysicalDeviceFeatures': '''
    uint32_t num_bools = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    VkBool32 *bool_array = &pFeatures->robustBufferAccess;
    SetBoolArrayTrue(bool_array, num_bools);
''',
'vkGetPhysicalDeviceFeatures2KHR': '''
    GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);
    uint32_t num_bools = 0; // Count number of VkBool32s in extension structs
    VkBool32* feat_bools = nullptr;
    const auto *desc_idx_features = lvl_find_in_chain<VkPhysicalDeviceDescriptorIndexingFeaturesEXT>(pFeatures->pNext);
    if (desc_idx_features) {
        const auto bool_size = sizeof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT) - offsetof(VkPhysicalDeviceDescriptorIndexingFeaturesEXT, shaderInputAttachmentArrayDynamicIndexing);
        num_bools = bool_size/sizeof(VkBool32);
        feat_bools = (VkBool32*)&desc_idx_features->shaderInputAttachmentArrayDynamicIndexing;
        SetBoolArrayTrue(feat_bools, num_bools);
    }
    const auto *blendop_features = lvl_find_in_chain<VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT>(pFeatures->pNext);
    if (blendop_features) {
        const auto bool_size = sizeof(VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT) - offsetof(VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT, advancedBlendCoherentOperations);
        num_bools = bool_size/sizeof(VkBool32);
        feat_bools = (VkBool32*)&blendop_features->advancedBlendCoherentOperations;
        SetBoolArrayTrue(feat_bools, num_bools);
    }
''',
'vkGetPhysicalDeviceFormatProperties': '''
    if (VK_FORMAT_UNDEFINED == format) {
        *pFormatProperties = { 0x0, 0x0, 0x0 };
    } else {
        // TODO: Just returning full support for everything initially
        *pFormatProperties = { 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF };
    }
''',
'vkGetPhysicalDeviceFormatProperties2KHR': '''
    GetPhysicalDeviceFormatProperties(physicalDevice, format, &pFormatProperties->formatProperties);
''',
'vkGetPhysicalDeviceImageFormatProperties': '''
    // A hardcoded unsupported format
    if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    // TODO: Just hard-coding some values for now
    // TODO: If tiling is linear, limit the mips, levels, & sample count
    if (VK_IMAGE_TILING_LINEAR == tiling) {
        *pImageFormatProperties = { { 4096, 4096, 256 }, 1, 1, VK_SAMPLE_COUNT_1_BIT, 4294967296 };
    } else {
        // We hard-code support for all sample counts except 64 bits.
        *pImageFormatProperties = { { 4096, 4096, 256 }, 12, 256, 0x7F & ~VK_SAMPLE_COUNT_64_BIT, 4294967296 };
    }
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceImageFormatProperties2KHR': '''
    GetPhysicalDeviceImageFormatProperties(physicalDevice, pImageFormatInfo->format, pImageFormatInfo->type, pImageFormatInfo->tiling, pImageFormatInfo->usage, pImageFormatInfo->flags, &pImageFormatProperties->imageFormatProperties);
    return VK_SUCCESS;
''',
'vkGetPhysicalDeviceProperties': '''
    // TODO: Just hard-coding some values for now
    pProperties->apiVersion = VK_API_VERSION_1_0;
    pProperties->driverVersion = 1;
    pProperties->vendorID = 0xba5eba11;
    pProperties->deviceID = 0xf005ba11;
    pProperties->deviceType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    //std::string devName = "Vulkan Mock Device";
    strcpy(pProperties->deviceName, "Vulkan Mock Device");
    pProperties->pipelineCacheUUID[0] = 18;
    pProperties->limits = SetLimits(&pProperties->limits);
    pProperties->sparseProperties = { VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE, VK_TRUE };
''',
'vkGetPhysicalDeviceProperties2KHR': '''
    GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);
    const auto *desc_idx_props = lvl_find_in_chain<VkPhysicalDeviceDescriptorIndexingPropertiesEXT>(pProperties->pNext);
    if (desc_idx_props) {
        VkPhysicalDeviceDescriptorIndexingPropertiesEXT* write_props = (VkPhysicalDeviceDescriptorIndexingPropertiesEXT*)desc_idx_props;
        write_props->maxUpdateAfterBindDescriptorsInAllPools = 500000;
        write_props->shaderUniformBufferArrayNonUniformIndexingNative = false;
        write_props->shaderSampledImageArrayNonUniformIndexingNative = false;
        write_props->shaderStorageBufferArrayNonUniformIndexingNative = false;
        write_props->shaderStorageImageArrayNonUniformIndexingNative = false;
        write_props->shaderInputAttachmentArrayNonUniformIndexingNative = false;
        write_props->robustBufferAccessUpdateAfterBind = true;
        write_props->quadDivergentImplicitLod = true;
        write_props->maxPerStageDescriptorUpdateAfterBindSamplers = 500000;
        write_props->maxPerStageDescriptorUpdateAfterBindUniformBuffers = 500000;
        write_props->maxPerStageDescriptorUpdateAfterBindStorageBuffers = 500000;
        write_props->maxPerStageDescriptorUpdateAfterBindSampledImages = 500000;
        write_props->maxPerStageDescriptorUpdateAfterBindStorageImages = 500000;
        write_props->maxPerStageDescriptorUpdateAfterBindInputAttachments = 500000;
        write_props->maxPerStageUpdateAfterBindResources = 500000;
        write_props->maxDescriptorSetUpdateAfterBindSamplers = 500000;
        write_props->maxDescriptorSetUpdateAfterBindUniformBuffers = 96;
        write_props->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 8;
        write_props->maxDescriptorSetUpdateAfterBindStorageBuffers = 500000;
        write_props->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 4;
        write_props->maxDescriptorSetUpdateAfterBindSampledImages = 500000;
        write_props->maxDescriptorSetUpdateAfterBindStorageImages = 500000;
        write_props->maxDescriptorSetUpdateAfterBindInputAttachments = 500000;
    }

    const auto *push_descriptor_props = lvl_find_in_chain<VkPhysicalDevicePushDescriptorPropertiesKHR>(pProperties->pNext);
    if (push_descriptor_props) {
        VkPhysicalDevicePushDescriptorPropertiesKHR* write_props = (VkPhysicalDevicePushDescriptorPropertiesKHR*)push_descriptor_props;
        write_props->maxPushDescriptors = 256;
    }

    const auto *depth_stencil_resolve_props = lvl_find_in_chain<VkPhysicalDeviceDepthStencilResolvePropertiesKHR>(pProperties->pNext);
    if (depth_stencil_resolve_props) {
        VkPhysicalDeviceDepthStencilResolvePropertiesKHR* write_props = (VkPhysicalDeviceDepthStencilResolvePropertiesKHR*)depth_stencil_resolve_props;
        write_props->supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR;
        write_props->supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR;
    }
''',
'vkGetPhysicalDeviceExternalSemaphoreProperties':'''
    // Hard code support for all handle types and features
    pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0x1F;
    pExternalSemaphoreProperties->compatibleHandleTypes = 0x1F;
    pExternalSemaphoreProperties->externalSemaphoreFeatures = 0x3;
''',
'vkGetPhysicalDeviceExternalSemaphorePropertiesKHR':'''
    GetPhysicalDeviceExternalSemaphoreProperties(physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
''',
'vkGetPhysicalDeviceExternalFenceProperties':'''
    // Hard-code support for all handle types and features
    pExternalFenceProperties->exportFromImportedHandleTypes = 0xF;
    pExternalFenceProperties->compatibleHandleTypes = 0xF;
    pExternalFenceProperties->externalFenceFeatures = 0x3;
''',
'vkGetPhysicalDeviceExternalFencePropertiesKHR':'''
    GetPhysicalDeviceExternalFenceProperties(physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
''',
'vkGetPhysicalDeviceExternalBufferProperties':'''
    // Hard-code support for all handle types and features
    pExternalBufferProperties->externalMemoryProperties.externalMemoryFeatures = 0x7;
    pExternalBufferProperties->externalMemoryProperties.exportFromImportedHandleTypes = 0x1FF;
    pExternalBufferProperties->externalMemoryProperties.compatibleHandleTypes = 0x1FF;
''',
'vkGetPhysicalDeviceExternalBufferPropertiesKHR':'''
    GetPhysicalDeviceExternalBufferProperties(physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
''',
'vkGetBufferMemoryRequirements': '''
    // TODO: Just hard-coding reqs for now
    pMemoryRequirements->size = 4096;
    pMemoryRequirements->alignment = 1;
    pMemoryRequirements->memoryTypeBits = 0xFFFF;
    // Return a better size based on the buffer size from the create info.
    auto d_iter = buffer_map.find(device);
    if (d_iter != buffer_map.end()) {
        auto iter = d_iter->second.find(buffer);
        if (iter != d_iter->second.end()) {
            pMemoryRequirements->size = ((iter->second.size + 4095) / 4096) * 4096;
        }
    }
''',
'vkGetBufferMemoryRequirements2KHR': '''
    GetBufferMemoryRequirements(device, pInfo->buffer, &pMemoryRequirements->memoryRequirements);
''',
'vkGetImageMemoryRequirements': '''
    // TODO: Just hard-coding reqs for now
    pMemoryRequirements->size = 4096;
    pMemoryRequirements->alignment = 1;

    // Here we hard-code that the memory type at index 3 doesn't support this image.
    pMemoryRequirements->memoryTypeBits = 0xFFFF & ~(0x1 << 3);
''',
'vkGetImageMemoryRequirements2KHR': '''
    GetImageMemoryRequirements(device, pInfo->image, &pMemoryRequirements->memoryRequirements);
''',
'vkMapMemory': '''
    unique_lock_t lock(global_lock);
    // TODO: Just hard-coding 64k whole size for now
    if (VK_WHOLE_SIZE == size)
        size = 0x10000;
    void* map_addr = malloc((size_t)size);
    mapped_memory_map[memory].push_back(map_addr);
    *ppData = map_addr;
    return VK_SUCCESS;
''',
'vkUnmapMemory': '''
    unique_lock_t lock(global_lock);
    for (auto map_addr : mapped_memory_map[memory]) {
        free(map_addr);
    }
    mapped_memory_map.erase(memory);
''',
'vkGetImageSubresourceLayout': '''
    // Need safe values. Callers are computing memory offsets from pLayout, with no return code to flag failure. 
    *pLayout = VkSubresourceLayout(); // Default constructor zero values.
''',
'vkGetSwapchainImagesKHR': '''
    if (!pSwapchainImages) {
        *pSwapchainImageCount = 1;
    } else if (*pSwapchainImageCount > 0) {
        pSwapchainImages[0] = (VkImage)global_unique_handle++;
        if (*pSwapchainImageCount != 1) {
            return VK_INCOMPLETE;
        }
    }
    return VK_SUCCESS;
''',
'vkAcquireNextImagesKHR': '''
    *pImageIndex = 0;
    return VK_SUCCESS;
''',
'vkCreateBuffer': '''
    unique_lock_t lock(global_lock);
    *pBuffer = (VkBuffer)global_unique_handle++;
    buffer_map[device][*pBuffer] = *pCreateInfo;
    return VK_SUCCESS;
''',
'vkDestroyBuffer': '''
    unique_lock_t lock(global_lock);
    buffer_map[device].erase(buffer);
''',
}

# MockICDGeneratorOptions - subclass of GeneratorOptions.
#
# Adds options used by MockICDOutputGenerator objects during Mock
# ICD generation.
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
class MockICDGeneratorOptions(GeneratorOptions):
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
                 protectProto = None,
                 protectProtoStr = None,
                 apicall = '',
                 apientry = '',
                 apientryp = '',
                 indentFuncProto = True,
                 indentFuncPointer = False,
                 alignFuncParam = 0,
                 expandEnumerants = True,
                 helper_file_type = ''):
        GeneratorOptions.__init__(self, conventions, filename, directory, apiname, profile,
                                  versions, emitversions, defaultExtensions,
                                  addExtensions, removeExtensions, emitExtensions, sortProcedure)
        self.prefixText      = prefixText
        self.genFuncPointers = genFuncPointers
        self.protectFile     = protectFile
        self.protectFeature  = protectFeature
        self.protectProto    = protectProto
        self.protectProtoStr = protectProtoStr
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam  = alignFuncParam

# MockICDOutputGenerator - subclass of OutputGenerator.
# Generates a mock vulkan ICD.
#  This is intended to be a minimal replacement for a vulkan device in order
#  to enable Vulkan Validation testing.
#
# ---- methods ----
# MockOutputGenerator(errFile, warnFile, diagFile) - args as for
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
class MockICDOutputGenerator(OutputGenerator):
    """Generate specified API interfaces in a specific style, such as a C header"""
    # This is an ordered list of sections in the header file.
    TYPE_SECTIONS = ['include', 'define', 'basetype', 'handle', 'enum',
                     'group', 'bitmask', 'funcpointer', 'struct']
    ALL_SECTIONS = TYPE_SECTIONS + ['command']
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        # Internal state - accumulators for different inner block text
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])
        self.intercepts = []

    # Check if the parameter passed in is a pointer to an array
    def paramIsArray(self, param):
        return param.attrib.get('len') is not None

    # Check if the parameter passed in is a pointer
    def paramIsPointer(self, param):
        ispointer = False
        for elem in param:
            if ((elem.tag != 'type') and (elem.tail is not None)) and '*' in elem.tail:
                ispointer = True
        return ispointer

    # Check if an object is a non-dispatchable handle
    def isHandleTypeNonDispatchable(self, handletype):
        handle = self.registry.tree.find("types/type/[name='" + handletype + "'][@category='handle']")
        if handle is not None and handle.find('type').text == 'VK_DEFINE_NON_DISPATCHABLE_HANDLE':
            return True
        else:
            return False

    # Check if an object is a dispatchable handle
    def isHandleTypeDispatchable(self, handletype):
        handle = self.registry.tree.find("types/type/[name='" + handletype + "'][@category='handle']")
        if handle is not None and handle.find('type').text == 'VK_DEFINE_HANDLE':
            return True
        else:
            return False

    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        # C-specific
        #
        # Multiple inclusion protection & C++ namespace.
        self.header = False
        if (genOpts.protectFile and self.genOpts.filename and 'h' == self.genOpts.filename[-1]):
            self.header = True
            headerSym = '__' + re.sub(r'\.h', '_h_', os.path.basename(self.genOpts.filename))
            write('#ifndef', headerSym, file=self.outFile)
            write('#define', headerSym, '1', file=self.outFile)
            self.newline()
        #
        # User-supplied prefix text, if any (list of strings)
        if (genOpts.prefixText):
            for s in genOpts.prefixText:
                write(s, file=self.outFile)
        if self.header:
            write('#include <unordered_map>', file=self.outFile)
            write('#include <mutex>', file=self.outFile)
            write('#include <string>', file=self.outFile)
            write('#include <cstring>', file=self.outFile)
            write('#include "vulkan/vk_icd.h"', file=self.outFile)
        else:
            write('#include "mock_icd.h"', file=self.outFile)
            write('#include <stdlib.h>', file=self.outFile)
            write('#include <vector>', file=self.outFile)
            write('#include "vk_typemap_helper.h"', file=self.outFile)

        write('namespace vkmock {', file=self.outFile)
        if self.header:
            self.newline()
            write(HEADER_C_CODE, file=self.outFile)
            # Include all of the extensions in ICD except specific ignored ones
            device_exts = []
            instance_exts = []
            # Ignore extensions that ICDs should not implement or are not safe to report
            ignore_exts = ['VK_EXT_validation_cache']
            for ext in self.registry.tree.findall("extensions/extension"):
                if ext.attrib['supported'] != 'disabled': # Only include enabled extensions
                    if (ext.attrib['name'] in ignore_exts):
                        pass
                    elif (ext.attrib.get('type') and 'instance' == ext.attrib['type']):
                        instance_exts.append('    {"%s", %s},' % (ext.attrib['name'], ext[0][0].attrib['value']))
                    else:
                        device_exts.append('    {"%s", %s},' % (ext.attrib['name'], ext[0][0].attrib['value']))
            write('// Map of instance extension name to version', file=self.outFile)
            write('static const std::unordered_map<std::string, uint32_t> instance_extension_map = {', file=self.outFile)
            write('\n'.join(instance_exts), file=self.outFile)
            write('};', file=self.outFile)
            write('// Map of device extension name to version', file=self.outFile)
            write('static const std::unordered_map<std::string, uint32_t> device_extension_map = {', file=self.outFile)
            write('\n'.join(device_exts), file=self.outFile)
            write('};', file=self.outFile)

        else:
            self.newline()
            write(SOURCE_CPP_PREFIX, file=self.outFile)

    def endFile(self):
        # C-specific
        # Finish C++ namespace and multiple inclusion protection
        self.newline()
        if self.header:
            # record intercepted procedures
            write('// Map of all APIs to be intercepted by this layer', file=self.outFile)
            write('static const std::unordered_map<std::string, void*> name_to_funcptr_map = {', file=self.outFile)
            write('\n'.join(self.intercepts), file=self.outFile)
            write('};\n', file=self.outFile)
            self.newline()
            write('} // namespace vkmock', file=self.outFile)
            self.newline()
            write('#endif', file=self.outFile)
        else: # Loader-layer-interface, need to implement global interface functions
            write(SOURCE_CPP_POSTFIX, file=self.outFile)
        # Finish processing in superclass
        OutputGenerator.endFile(self)
    def beginFeature(self, interface, emit):
        #write('// starting beginFeature', file=self.outFile)
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        self.featureExtraProtect = GetFeatureProtect(interface)
        # C-specific
        # Accumulate includes, defines, types, enums, function pointer typedefs,
        # end function prototypes separately for this feature. They're only
        # printed in endFeature().
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])
        #write('// ending beginFeature', file=self.outFile)
    def endFeature(self):
        # C-specific
        # Actually write the interface to the output file.
        #write('// starting endFeature', file=self.outFile)
        if (self.emit):
            self.newline()
            if (self.genOpts.protectFeature):
                write('#ifndef', self.featureName, file=self.outFile)
            # If type declarations are needed by other features based on
            # this one, it may be necessary to suppress the ExtraProtect,
            # or move it below the 'for section...' loop.
            #write('// endFeature looking at self.featureExtraProtect', file=self.outFile)
            if (self.featureExtraProtect != None):
                write('#ifdef', self.featureExtraProtect, file=self.outFile)
            #write('#define', self.featureName, '1', file=self.outFile)
            for section in self.TYPE_SECTIONS:
                #write('// endFeature writing section'+section, file=self.outFile)
                contents = self.sections[section]
                if contents:
                    write('\n'.join(contents), file=self.outFile)
                    self.newline()
            #write('// endFeature looking at self.sections[command]', file=self.outFile)
            if (self.sections['command']):
                write('\n'.join(self.sections['command']), end=u'', file=self.outFile)
                self.newline()
            if (self.featureExtraProtect != None):
                write('#endif /*', self.featureExtraProtect, '*/', file=self.outFile)
            if (self.genOpts.protectFeature):
                write('#endif /*', self.featureName, '*/', file=self.outFile)
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
        #write('// ending endFeature', file=self.outFile)
    #
    # Append a definition to the specified section
    def appendSection(self, section, text):
        # self.sections[section].append('SECTION: ' + section + '\n')
        self.sections[section].append(text)
    #
    # Type generation
    def genType(self, typeinfo, name, alias):
        pass
    #
    # Struct (e.g. C "struct" type) generation.
    # This is a special case of the <type> tag where the contents are
    # interpreted as a set of <member> tags instead of freeform C
    # C type declarations. The <member> tags are just like <param>
    # tags - they are a declaration of a struct or union member.
    # Only simple member declarations are supported (no nested
    # structs etc.)
    def genStruct(self, typeinfo, typeName, alias):
        OutputGenerator.genStruct(self, typeinfo, typeName, alias)
        body = 'typedef ' + typeinfo.elem.get('category') + ' ' + typeName + ' {\n'
        # paramdecl = self.makeCParamDecl(typeinfo.elem, self.genOpts.alignFuncParam)
        for member in typeinfo.elem.findall('.//member'):
            body += self.makeCParamDecl(member, self.genOpts.alignFuncParam)
            body += ';\n'
        body += '} ' + typeName + ';\n'
        self.appendSection('struct', body)
    #
    # Group (e.g. C "enum" type) generation.
    # These are concatenated together with other types.
    def genGroup(self, groupinfo, groupName, alias):
        pass
    # Enumerant generation
    # <enum> tags may specify their values in several ways, but are usually
    # just integers.
    def genEnum(self, enuminfo, name, alias):
        pass
    #
    # Command generation
    def genCmd(self, cmdinfo, name, alias):
        decls = self.makeCDecls(cmdinfo.elem)
        if self.header: # In the header declare all intercepts
            self.appendSection('command', '')
            self.appendSection('command', 'static %s' % (decls[0]))
            if (self.featureExtraProtect != None):
                self.intercepts += [ '#ifdef %s' % self.featureExtraProtect ]
            self.intercepts += [ '    {"%s", (void*)%s},' % (name,name[2:]) ]
            if (self.featureExtraProtect != None):
                self.intercepts += [ '#endif' ]
            return

        manual_functions = [
            # Include functions here to be interecpted w/ manually implemented function bodies
            'vkGetDeviceProcAddr',
            'vkGetInstanceProcAddr',
            'vkCreateDevice',
            'vkDestroyDevice',
            'vkCreateInstance',
            'vkDestroyInstance',
            #'vkCreateDebugReportCallbackEXT',
            #'vkDestroyDebugReportCallbackEXT',
            'vkEnumerateInstanceLayerProperties',
            'vkEnumerateInstanceExtensionProperties',
            'vkEnumerateDeviceLayerProperties',
            'vkEnumerateDeviceExtensionProperties',
        ]
        if name in manual_functions:
            self.appendSection('command', '')
            if name not in CUSTOM_C_INTERCEPTS:
                self.appendSection('command', '// declare only')
                self.appendSection('command', 'static %s' % (decls[0]))
                self.appendSection('command', '// TODO: Implement custom intercept body')
            else:
                self.appendSection('command', 'static %s' % (decls[0][:-1]))
                self.appendSection('command', '{\n%s}' % (CUSTOM_C_INTERCEPTS[name]))
            self.intercepts += [ '    {"%s", (void*)%s},' % (name,name[2:]) ]
            return
        # record that the function will be intercepted
        if (self.featureExtraProtect != None):
            self.intercepts += [ '#ifdef %s' % self.featureExtraProtect ]
        self.intercepts += [ '    {"%s", (void*)%s},' % (name,name[2:]) ]
        if (self.featureExtraProtect != None):
            self.intercepts += [ '#endif' ]

        OutputGenerator.genCmd(self, cmdinfo, name, alias)
        #
        self.appendSection('command', '')
        self.appendSection('command', 'static %s' % (decls[0][:-1]))
        if name in CUSTOM_C_INTERCEPTS:
            self.appendSection('command', '{%s}' % (CUSTOM_C_INTERCEPTS[name]))
            return

        # Declare result variable, if any.
        resulttype = cmdinfo.elem.find('proto/type')
        if (resulttype != None and resulttype.text == 'void'):
            resulttype = None
        # if the name w/ KHR postfix is in the CUSTOM_C_INTERCEPTS
        # Call the KHR custom version instead of generating separate code
        khr_name = name + "KHR"
        if khr_name in CUSTOM_C_INTERCEPTS:
            return_string = ''
            if resulttype != None:
                return_string = 'return '
            params = cmdinfo.elem.findall('param/name')
            param_names = []
            for param in params:
                param_names.append(param.text)
            self.appendSection('command', '{\n    %s%s(%s);\n}' % (return_string, khr_name[2:], ", ".join(param_names)))
            return
        self.appendSection('command', '{')

        api_function_name = cmdinfo.elem.attrib.get('name')
        # GET THE TYPE OF FUNCTION
        if True in [ftxt in api_function_name for ftxt in ['Create', 'Allocate']]:
            # Get last param
            last_param = cmdinfo.elem.findall('param')[-1]
            lp_txt = last_param.find('name').text
            lp_len = None
            if ('len' in last_param.attrib):
                lp_len = last_param.attrib['len']
                lp_len = lp_len.replace('::', '->')
            lp_type = last_param.find('type').text
            handle_type = 'dispatchable'
            allocator_txt = 'CreateDispObjHandle()';
            if (self.isHandleTypeNonDispatchable(lp_type)):
                handle_type = 'non-' + handle_type
                allocator_txt = 'global_unique_handle++';
            # Need to lock in both cases
            self.appendSection('command', '    unique_lock_t lock(global_lock);')
            if (lp_len != None):
                #print("%s last params (%s) has len %s" % (handle_type, lp_txt, lp_len))
                self.appendSection('command', '    for (uint32_t i = 0; i < %s; ++i) {' % (lp_len))
                self.appendSection('command', '        %s[i] = (%s)%s;' % (lp_txt, lp_type, allocator_txt))
                self.appendSection('command', '    }')
            else:
                #print("Single %s last param is '%s' w/ type '%s'" % (handle_type, lp_txt, lp_type))
                self.appendSection('command', '    *%s = (%s)%s;' % (lp_txt, lp_type, allocator_txt))
        elif True in [ftxt in api_function_name for ftxt in ['Destroy', 'Free']]:
            self.appendSection('command', '//Destroy object')
        else:
            self.appendSection('command', '//Not a CREATE or DESTROY function')

        # Return result variable, if any.
        if (resulttype != None):
            if api_function_name == 'vkGetEventStatus':
                self.appendSection('command', '    return VK_EVENT_SET;')
            else:
                self.appendSection('command', '    return VK_SUCCESS;')
        self.appendSection('command', '}')
    #
    # override makeProtoName to drop the "vk" prefix
    def makeProtoName(self, name, tail):
        return self.genOpts.apientry + name[2:] + tail
