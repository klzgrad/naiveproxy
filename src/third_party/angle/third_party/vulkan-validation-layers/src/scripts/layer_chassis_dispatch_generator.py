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
# Author: Tobin Ehlis <tobine@google.com>
# Author: Mark Lobodzinski <mark@lunarg.com>

import os,re,sys
import xml.etree.ElementTree as etree
from generator import *
from collections import namedtuple
from common_codegen import *

# LayerChassisDispatchGeneratorOptions - subclass of GeneratorOptions.
#
# Adds options used by LayerChassisDispatchOutputGenerator objects during
# layer chassis dispatch file generation.
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
class LayerChassisDispatchGeneratorOptions(GeneratorOptions):
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
        self.genFuncPointers = genFuncPointers
        self.protectFile     = protectFile
        self.protectFeature  = protectFeature
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam   = alignFuncParam
        self.expandEnumerants = expandEnumerants


# LayerChassisDispatchOutputGenerator - subclass of OutputGenerator.
# Generates layer chassis non-dispatchable handle-wrapping code.
#
# ---- methods ----
# LayerChassisDispatchOutputGenerator(errFile, warnFile, diagFile) - args as for OutputGenerator. Defines additional internal state.
# ---- methods overriding base class ----
# beginFile(genOpts)
# endFile()
# beginFeature(interface, emit)
# endFeature()
# genCmd(cmdinfo)
# genStruct()
# genType()
class LayerChassisDispatchOutputGenerator(OutputGenerator):
    """Generate layer chassis handle wrapping code based on XML element attributes"""
    inline_copyright_message = """
// This file is ***GENERATED***.  Do Not Edit.
// See layer_chassis_dispatch_generator.py for modifications.

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

#define DISPATCH_MAX_STACK_ALLOCATIONS 32

VkResult DispatchCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
                                        const VkComputePipelineCreateInfo *pCreateInfos,
                                        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.CreateComputePipelines(device, pipelineCache, createInfoCount,
                                                                                          pCreateInfos, pAllocator, pPipelines);
    safe_VkComputePipelineCreateInfo *local_pCreateInfos = NULL;
    if (pCreateInfos) {
        local_pCreateInfos = new safe_VkComputePipelineCreateInfo[createInfoCount];
        for (uint32_t idx0 = 0; idx0 < createInfoCount; ++idx0) {
            local_pCreateInfos[idx0].initialize(&pCreateInfos[idx0]);
            if (pCreateInfos[idx0].basePipelineHandle) {
                local_pCreateInfos[idx0].basePipelineHandle = layer_data->Unwrap(pCreateInfos[idx0].basePipelineHandle);
            }
            if (pCreateInfos[idx0].layout) {
                local_pCreateInfos[idx0].layout = layer_data->Unwrap(pCreateInfos[idx0].layout);
            }
            if (pCreateInfos[idx0].stage.module) {
                local_pCreateInfos[idx0].stage.module = layer_data->Unwrap(pCreateInfos[idx0].stage.module);
            }
        }
    }
    if (pipelineCache) {
        pipelineCache = layer_data->Unwrap(pipelineCache);
    }

    VkResult result = layer_data->device_dispatch_table.CreateComputePipelines(device, pipelineCache, createInfoCount,
                                                                               local_pCreateInfos->ptr(), pAllocator, pPipelines);
    delete[] local_pCreateInfos;
    {
        for (uint32_t i = 0; i < createInfoCount; ++i) {
            if (pPipelines[i] != VK_NULL_HANDLE) {
                pPipelines[i] = layer_data->WrapNew(pPipelines[i]);
            }
        }
    }
    return result;
}

VkResult DispatchCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount,
                                         const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                         const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.CreateGraphicsPipelines(device, pipelineCache, createInfoCount,
                                                                                           pCreateInfos, pAllocator, pPipelines);
    safe_VkGraphicsPipelineCreateInfo *local_pCreateInfos = nullptr;
    if (pCreateInfos) {
        local_pCreateInfos = new safe_VkGraphicsPipelineCreateInfo[createInfoCount];
        read_dispatch_lock_guard_t lock(dispatch_lock);
        for (uint32_t idx0 = 0; idx0 < createInfoCount; ++idx0) {
            bool uses_color_attachment = false;
            bool uses_depthstencil_attachment = false;
            {
                const auto subpasses_uses_it = layer_data->renderpasses_states.find(layer_data->Unwrap(pCreateInfos[idx0].renderPass));
                if (subpasses_uses_it != layer_data->renderpasses_states.end()) {
                    const auto &subpasses_uses = subpasses_uses_it->second;
                    if (subpasses_uses.subpasses_using_color_attachment.count(pCreateInfos[idx0].subpass))
                        uses_color_attachment = true;
                    if (subpasses_uses.subpasses_using_depthstencil_attachment.count(pCreateInfos[idx0].subpass))
                        uses_depthstencil_attachment = true;
                }
            }

            local_pCreateInfos[idx0].initialize(&pCreateInfos[idx0], uses_color_attachment, uses_depthstencil_attachment);

            if (pCreateInfos[idx0].basePipelineHandle) {
                local_pCreateInfos[idx0].basePipelineHandle = layer_data->Unwrap(pCreateInfos[idx0].basePipelineHandle);
            }
            if (pCreateInfos[idx0].layout) {
                local_pCreateInfos[idx0].layout = layer_data->Unwrap(pCreateInfos[idx0].layout);
            }
            if (pCreateInfos[idx0].pStages) {
                for (uint32_t idx1 = 0; idx1 < pCreateInfos[idx0].stageCount; ++idx1) {
                    if (pCreateInfos[idx0].pStages[idx1].module) {
                        local_pCreateInfos[idx0].pStages[idx1].module = layer_data->Unwrap(pCreateInfos[idx0].pStages[idx1].module);
                    }
                }
            }
            if (pCreateInfos[idx0].renderPass) {
                local_pCreateInfos[idx0].renderPass = layer_data->Unwrap(pCreateInfos[idx0].renderPass);
            }
        }
    }
    if (pipelineCache) {
        pipelineCache = layer_data->Unwrap(pipelineCache);
    }

    VkResult result = layer_data->device_dispatch_table.CreateGraphicsPipelines(device, pipelineCache, createInfoCount,
                                                                                local_pCreateInfos->ptr(), pAllocator, pPipelines);
    delete[] local_pCreateInfos;
    {
        for (uint32_t i = 0; i < createInfoCount; ++i) {
            if (pPipelines[i] != VK_NULL_HANDLE) {
                pPipelines[i] = layer_data->WrapNew(pPipelines[i]);
            }
        }
    }
    return result;
}

template <typename T>
static void UpdateCreateRenderPassState(ValidationObject *layer_data, const T *pCreateInfo, VkRenderPass renderPass) {
    auto &renderpass_state = layer_data->renderpasses_states[renderPass];

    for (uint32_t subpass = 0; subpass < pCreateInfo->subpassCount; ++subpass) {
        bool uses_color = false;
        for (uint32_t i = 0; i < pCreateInfo->pSubpasses[subpass].colorAttachmentCount && !uses_color; ++i)
            if (pCreateInfo->pSubpasses[subpass].pColorAttachments[i].attachment != VK_ATTACHMENT_UNUSED) uses_color = true;

        bool uses_depthstencil = false;
        if (pCreateInfo->pSubpasses[subpass].pDepthStencilAttachment)
            if (pCreateInfo->pSubpasses[subpass].pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED)
                uses_depthstencil = true;

        if (uses_color) renderpass_state.subpasses_using_color_attachment.insert(subpass);
        if (uses_depthstencil) renderpass_state.subpasses_using_depthstencil_attachment.insert(subpass);
    }
}

VkResult DispatchCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    VkResult result = layer_data->device_dispatch_table.CreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
    if (!wrap_handles) return result;
    if (VK_SUCCESS == result) {
        write_dispatch_lock_guard_t lock(dispatch_lock);
        UpdateCreateRenderPassState(layer_data, pCreateInfo, *pRenderPass);
        *pRenderPass = layer_data->WrapNew(*pRenderPass);
    }
    return result;
}

VkResult DispatchCreateRenderPass2KHR(VkDevice device, const VkRenderPassCreateInfo2KHR *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    VkResult result = layer_data->device_dispatch_table.CreateRenderPass2KHR(device, pCreateInfo, pAllocator, pRenderPass);
    if (!wrap_handles) return result;
    if (VK_SUCCESS == result) {
        write_dispatch_lock_guard_t lock(dispatch_lock);
        UpdateCreateRenderPassState(layer_data, pCreateInfo, *pRenderPass);
        *pRenderPass = layer_data->WrapNew(*pRenderPass);
    }
    return result;
}

void DispatchDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.DestroyRenderPass(device, renderPass, pAllocator);
    uint64_t renderPass_id = reinterpret_cast<uint64_t &>(renderPass);

    auto iter = unique_id_mapping.pop(renderPass_id);
    if (iter != unique_id_mapping.end()) {
        renderPass = (VkRenderPass)iter->second;
    } else {
        renderPass = (VkRenderPass)0;
    }

    layer_data->device_dispatch_table.DestroyRenderPass(device, renderPass, pAllocator);

    write_dispatch_lock_guard_t lock(dispatch_lock);
    layer_data->renderpasses_states.erase(renderPass);
}

VkResult DispatchCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    safe_VkSwapchainCreateInfoKHR *local_pCreateInfo = NULL;
    if (pCreateInfo) {
        local_pCreateInfo = new safe_VkSwapchainCreateInfoKHR(pCreateInfo);
        local_pCreateInfo->oldSwapchain = layer_data->Unwrap(pCreateInfo->oldSwapchain);
        // Surface is instance-level object
        local_pCreateInfo->surface = layer_data->Unwrap(pCreateInfo->surface);
    }

    VkResult result = layer_data->device_dispatch_table.CreateSwapchainKHR(device, local_pCreateInfo->ptr(), pAllocator, pSwapchain);
    delete local_pCreateInfo;

    if (VK_SUCCESS == result) {
        *pSwapchain = layer_data->WrapNew(*pSwapchain);
    }
    return result;
}

VkResult DispatchCreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR *pCreateInfos,
                                           const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchains) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.CreateSharedSwapchainsKHR(device, swapchainCount, pCreateInfos, pAllocator,
                                                                           pSwapchains);
    safe_VkSwapchainCreateInfoKHR *local_pCreateInfos = NULL;
    {
        if (pCreateInfos) {
            local_pCreateInfos = new safe_VkSwapchainCreateInfoKHR[swapchainCount];
            for (uint32_t i = 0; i < swapchainCount; ++i) {
                local_pCreateInfos[i].initialize(&pCreateInfos[i]);
                if (pCreateInfos[i].surface) {
                    // Surface is instance-level object
                    local_pCreateInfos[i].surface = layer_data->Unwrap(pCreateInfos[i].surface);
                }
                if (pCreateInfos[i].oldSwapchain) {
                    local_pCreateInfos[i].oldSwapchain = layer_data->Unwrap(pCreateInfos[i].oldSwapchain);
                }
            }
        }
    }
    VkResult result = layer_data->device_dispatch_table.CreateSharedSwapchainsKHR(device, swapchainCount, local_pCreateInfos->ptr(),
                                                                                  pAllocator, pSwapchains);
    delete[] local_pCreateInfos;
    if (VK_SUCCESS == result) {
        for (uint32_t i = 0; i < swapchainCount; i++) {
            pSwapchains[i] = layer_data->WrapNew(pSwapchains[i]);
        }
    }
    return result;
}

VkResult DispatchGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
                                       VkImage *pSwapchainImages) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.GetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
    VkSwapchainKHR wrapped_swapchain_handle = swapchain;
    if (VK_NULL_HANDLE != swapchain) {
        swapchain = layer_data->Unwrap(swapchain);
    }
    VkResult result =
        layer_data->device_dispatch_table.GetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
    if ((VK_SUCCESS == result) || (VK_INCOMPLETE == result)) {
        if ((*pSwapchainImageCount > 0) && pSwapchainImages) {
            write_dispatch_lock_guard_t lock(dispatch_lock);
            auto &wrapped_swapchain_image_handles = layer_data->swapchain_wrapped_image_handle_map[wrapped_swapchain_handle];
            for (uint32_t i = static_cast<uint32_t>(wrapped_swapchain_image_handles.size()); i < *pSwapchainImageCount; i++) {
                wrapped_swapchain_image_handles.emplace_back(layer_data->WrapNew(pSwapchainImages[i]));
            }
            for (uint32_t i = 0; i < *pSwapchainImageCount; i++) {
                pSwapchainImages[i] = wrapped_swapchain_image_handles[i];
            }
        }
    }
    return result;
}

void DispatchDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.DestroySwapchainKHR(device, swapchain, pAllocator);
    write_dispatch_lock_guard_t lock(dispatch_lock);

    auto &image_array = layer_data->swapchain_wrapped_image_handle_map[swapchain];
    for (auto &image_handle : image_array) {
        unique_id_mapping.erase(HandleToUint64(image_handle));
    }
    layer_data->swapchain_wrapped_image_handle_map.erase(swapchain);
    lock.unlock();

    uint64_t swapchain_id = HandleToUint64(swapchain);

    auto iter = unique_id_mapping.pop(swapchain_id);
    if (iter != unique_id_mapping.end()) {
        swapchain = (VkSwapchainKHR)iter->second;
    } else {
        swapchain = (VkSwapchainKHR)0;
    }

    layer_data->device_dispatch_table.DestroySwapchainKHR(device, swapchain, pAllocator);
}

VkResult DispatchQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(queue), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.QueuePresentKHR(queue, pPresentInfo);
    safe_VkPresentInfoKHR *local_pPresentInfo = NULL;
    {
        if (pPresentInfo) {
            local_pPresentInfo = new safe_VkPresentInfoKHR(pPresentInfo);
            if (local_pPresentInfo->pWaitSemaphores) {
                for (uint32_t index1 = 0; index1 < local_pPresentInfo->waitSemaphoreCount; ++index1) {
                    local_pPresentInfo->pWaitSemaphores[index1] = layer_data->Unwrap(pPresentInfo->pWaitSemaphores[index1]);
                }
            }
            if (local_pPresentInfo->pSwapchains) {
                for (uint32_t index1 = 0; index1 < local_pPresentInfo->swapchainCount; ++index1) {
                    local_pPresentInfo->pSwapchains[index1] = layer_data->Unwrap(pPresentInfo->pSwapchains[index1]);
                }
            }
        }
    }
    VkResult result = layer_data->device_dispatch_table.QueuePresentKHR(queue, local_pPresentInfo->ptr());

    // pResults is an output array embedded in a structure. The code generator neglects to copy back from the safe_* version,
    // so handle it as a special case here:
    if (pPresentInfo && pPresentInfo->pResults) {
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
            pPresentInfo->pResults[i] = local_pPresentInfo->pResults[i];
        }
    }
    delete local_pPresentInfo;
    return result;
}

void DispatchDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.DestroyDescriptorPool(device, descriptorPool, pAllocator);
    write_dispatch_lock_guard_t lock(dispatch_lock);

    // remove references to implicitly freed descriptor sets
    for(auto descriptor_set : layer_data->pool_descriptor_sets_map[descriptorPool]) {
        unique_id_mapping.erase(reinterpret_cast<uint64_t &>(descriptor_set));
    }
    layer_data->pool_descriptor_sets_map.erase(descriptorPool);
    lock.unlock();

    uint64_t descriptorPool_id = reinterpret_cast<uint64_t &>(descriptorPool);

    auto iter = unique_id_mapping.pop(descriptorPool_id);
    if (iter != unique_id_mapping.end()) {
        descriptorPool = (VkDescriptorPool)iter->second;
    } else {
        descriptorPool = (VkDescriptorPool)0;
    }

    layer_data->device_dispatch_table.DestroyDescriptorPool(device, descriptorPool, pAllocator);
}

VkResult DispatchResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.ResetDescriptorPool(device, descriptorPool, flags);
    VkDescriptorPool local_descriptor_pool = VK_NULL_HANDLE;
    {
        local_descriptor_pool = layer_data->Unwrap(descriptorPool);
    }
    VkResult result = layer_data->device_dispatch_table.ResetDescriptorPool(device, local_descriptor_pool, flags);
    if (VK_SUCCESS == result) {
        write_dispatch_lock_guard_t lock(dispatch_lock);
        // remove references to implicitly freed descriptor sets
        for(auto descriptor_set : layer_data->pool_descriptor_sets_map[descriptorPool]) {
            unique_id_mapping.erase(reinterpret_cast<uint64_t &>(descriptor_set));
        }
        layer_data->pool_descriptor_sets_map[descriptorPool].clear();
    }

    return result;
}

VkResult DispatchAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                        VkDescriptorSet *pDescriptorSets) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.AllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
    safe_VkDescriptorSetAllocateInfo *local_pAllocateInfo = NULL;
    {
        if (pAllocateInfo) {
            local_pAllocateInfo = new safe_VkDescriptorSetAllocateInfo(pAllocateInfo);
            if (pAllocateInfo->descriptorPool) {
                local_pAllocateInfo->descriptorPool = layer_data->Unwrap(pAllocateInfo->descriptorPool);
            }
            if (local_pAllocateInfo->pSetLayouts) {
                for (uint32_t index1 = 0; index1 < local_pAllocateInfo->descriptorSetCount; ++index1) {
                    local_pAllocateInfo->pSetLayouts[index1] = layer_data->Unwrap(local_pAllocateInfo->pSetLayouts[index1]);
                }
            }
        }
    }
    VkResult result = layer_data->device_dispatch_table.AllocateDescriptorSets(
        device, (const VkDescriptorSetAllocateInfo *)local_pAllocateInfo, pDescriptorSets);
    if (local_pAllocateInfo) {
        delete local_pAllocateInfo;
    }
    if (VK_SUCCESS == result) {
        write_dispatch_lock_guard_t lock(dispatch_lock);
        auto &pool_descriptor_sets = layer_data->pool_descriptor_sets_map[pAllocateInfo->descriptorPool];
        for (uint32_t index0 = 0; index0 < pAllocateInfo->descriptorSetCount; index0++) {
            pDescriptorSets[index0] = layer_data->WrapNew(pDescriptorSets[index0]);
            pool_descriptor_sets.insert(pDescriptorSets[index0]);
        }
    }
    return result;
}

VkResult DispatchFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
                                    const VkDescriptorSet *pDescriptorSets) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.FreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
    VkDescriptorSet *local_pDescriptorSets = NULL;
    VkDescriptorPool local_descriptor_pool = VK_NULL_HANDLE;
    {
        local_descriptor_pool = layer_data->Unwrap(descriptorPool);
        if (pDescriptorSets) {
            local_pDescriptorSets = new VkDescriptorSet[descriptorSetCount];
            for (uint32_t index0 = 0; index0 < descriptorSetCount; ++index0) {
                local_pDescriptorSets[index0] = layer_data->Unwrap(pDescriptorSets[index0]);
            }
        }
    }
    VkResult result = layer_data->device_dispatch_table.FreeDescriptorSets(device, local_descriptor_pool, descriptorSetCount,
                                                                           (const VkDescriptorSet *)local_pDescriptorSets);
    if (local_pDescriptorSets) delete[] local_pDescriptorSets;
    if ((VK_SUCCESS == result) && (pDescriptorSets)) {
        write_dispatch_lock_guard_t lock(dispatch_lock);
        auto &pool_descriptor_sets = layer_data->pool_descriptor_sets_map[descriptorPool];
        for (uint32_t index0 = 0; index0 < descriptorSetCount; index0++) {
            VkDescriptorSet handle = pDescriptorSets[index0];
            pool_descriptor_sets.erase(handle);
            uint64_t unique_id = reinterpret_cast<uint64_t &>(handle);
            unique_id_mapping.erase(unique_id);
        }
    }
    return result;
}

// This is the core version of this routine.  The extension version is below.
VkResult DispatchCreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR *pCreateInfo,
                                                const VkAllocationCallbacks *pAllocator,
                                                VkDescriptorUpdateTemplateKHR *pDescriptorUpdateTemplate) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.CreateDescriptorUpdateTemplate(device, pCreateInfo, pAllocator,
                                                                                pDescriptorUpdateTemplate);
    safe_VkDescriptorUpdateTemplateCreateInfo var_local_pCreateInfo;
    safe_VkDescriptorUpdateTemplateCreateInfo *local_pCreateInfo = NULL;
    if (pCreateInfo) {
        local_pCreateInfo = &var_local_pCreateInfo;
        local_pCreateInfo->initialize(pCreateInfo);
        if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET) {
            local_pCreateInfo->descriptorSetLayout = layer_data->Unwrap(pCreateInfo->descriptorSetLayout);
        }
        if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
            local_pCreateInfo->pipelineLayout = layer_data->Unwrap(pCreateInfo->pipelineLayout);
        }
    }
    VkResult result = layer_data->device_dispatch_table.CreateDescriptorUpdateTemplate(device, local_pCreateInfo->ptr(), pAllocator,
                                                                                       pDescriptorUpdateTemplate);
    if (VK_SUCCESS == result) {
        *pDescriptorUpdateTemplate = layer_data->WrapNew(*pDescriptorUpdateTemplate);

        // Shadow template createInfo for later updates
        if (local_pCreateInfo) {
            write_dispatch_lock_guard_t lock(dispatch_lock);
            std::unique_ptr<TEMPLATE_STATE> template_state(new TEMPLATE_STATE(*pDescriptorUpdateTemplate, local_pCreateInfo));
            layer_data->desc_template_createinfo_map[(uint64_t)*pDescriptorUpdateTemplate] = std::move(template_state);
        }
    }
    return result;
}

// This is the extension version of this routine.  The core version is above.
VkResult DispatchCreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR *pCreateInfo,
                                                   const VkAllocationCallbacks *pAllocator,
                                                   VkDescriptorUpdateTemplateKHR *pDescriptorUpdateTemplate) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.CreateDescriptorUpdateTemplateKHR(device, pCreateInfo, pAllocator,
                                                                                   pDescriptorUpdateTemplate);
    safe_VkDescriptorUpdateTemplateCreateInfo var_local_pCreateInfo;
    safe_VkDescriptorUpdateTemplateCreateInfo *local_pCreateInfo = NULL;
    if (pCreateInfo) {
        local_pCreateInfo = &var_local_pCreateInfo;
        local_pCreateInfo->initialize(pCreateInfo);
        if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET) {
            local_pCreateInfo->descriptorSetLayout = layer_data->Unwrap(pCreateInfo->descriptorSetLayout);
        }
        if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
            local_pCreateInfo->pipelineLayout = layer_data->Unwrap(pCreateInfo->pipelineLayout);
        }
    }
    VkResult result = layer_data->device_dispatch_table.CreateDescriptorUpdateTemplateKHR(device, local_pCreateInfo->ptr(),
                                                                                          pAllocator, pDescriptorUpdateTemplate);

    if (VK_SUCCESS == result) {
        *pDescriptorUpdateTemplate = layer_data->WrapNew(*pDescriptorUpdateTemplate);

        // Shadow template createInfo for later updates
        if (local_pCreateInfo) {
            write_dispatch_lock_guard_t lock(dispatch_lock);
            std::unique_ptr<TEMPLATE_STATE> template_state(new TEMPLATE_STATE(*pDescriptorUpdateTemplate, local_pCreateInfo));
            layer_data->desc_template_createinfo_map[(uint64_t)*pDescriptorUpdateTemplate] = std::move(template_state);
        }
    }
    return result;
}

// This is the core version of this routine.  The extension version is below.
void DispatchDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate,
                                             const VkAllocationCallbacks *pAllocator) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.DestroyDescriptorUpdateTemplate(device, descriptorUpdateTemplate, pAllocator);
    write_dispatch_lock_guard_t lock(dispatch_lock);
    uint64_t descriptor_update_template_id = reinterpret_cast<uint64_t &>(descriptorUpdateTemplate);
    layer_data->desc_template_createinfo_map.erase(descriptor_update_template_id);
    lock.unlock();

    auto iter = unique_id_mapping.pop(descriptor_update_template_id);
    if (iter != unique_id_mapping.end()) {
        descriptorUpdateTemplate = (VkDescriptorUpdateTemplate)iter->second;
    } else {
        descriptorUpdateTemplate = (VkDescriptorUpdateTemplate)0;
    }

    layer_data->device_dispatch_table.DestroyDescriptorUpdateTemplate(device, descriptorUpdateTemplate, pAllocator);
}

// This is the extension version of this routine.  The core version is above.
void DispatchDestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate,
                                                const VkAllocationCallbacks *pAllocator) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.DestroyDescriptorUpdateTemplateKHR(device, descriptorUpdateTemplate, pAllocator);
    write_dispatch_lock_guard_t lock(dispatch_lock);
    uint64_t descriptor_update_template_id = reinterpret_cast<uint64_t &>(descriptorUpdateTemplate);
    layer_data->desc_template_createinfo_map.erase(descriptor_update_template_id);
    lock.unlock();

    auto iter = unique_id_mapping.pop(descriptor_update_template_id);
    if (iter != unique_id_mapping.end()) {
        descriptorUpdateTemplate = (VkDescriptorUpdateTemplate)iter->second;
    } else {
        descriptorUpdateTemplate = (VkDescriptorUpdateTemplate)0;
    }

    layer_data->device_dispatch_table.DestroyDescriptorUpdateTemplateKHR(device, descriptorUpdateTemplate, pAllocator);
}

void *BuildUnwrappedUpdateTemplateBuffer(ValidationObject *layer_data, uint64_t descriptorUpdateTemplate, const void *pData) {
    auto const template_map_entry = layer_data->desc_template_createinfo_map.find(descriptorUpdateTemplate);
    auto const &create_info = template_map_entry->second->create_info;
    size_t allocation_size = 0;
    std::vector<std::tuple<size_t, VulkanObjectType, uint64_t, size_t>> template_entries;

    for (uint32_t i = 0; i < create_info.descriptorUpdateEntryCount; i++) {
        for (uint32_t j = 0; j < create_info.pDescriptorUpdateEntries[i].descriptorCount; j++) {
            size_t offset = create_info.pDescriptorUpdateEntries[i].offset + j * create_info.pDescriptorUpdateEntries[i].stride;
            char *update_entry = (char *)(pData) + offset;

            switch (create_info.pDescriptorUpdateEntries[i].descriptorType) {
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
                    auto image_entry = reinterpret_cast<VkDescriptorImageInfo *>(update_entry);
                    allocation_size = std::max(allocation_size, offset + sizeof(VkDescriptorImageInfo));

                    VkDescriptorImageInfo *wrapped_entry = new VkDescriptorImageInfo(*image_entry);
                    wrapped_entry->sampler = layer_data->Unwrap(image_entry->sampler);
                    wrapped_entry->imageView = layer_data->Unwrap(image_entry->imageView);
                    template_entries.emplace_back(offset, kVulkanObjectTypeImage, CastToUint64(wrapped_entry), 0);
                } break;

                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                    auto buffer_entry = reinterpret_cast<VkDescriptorBufferInfo *>(update_entry);
                    allocation_size = std::max(allocation_size, offset + sizeof(VkDescriptorBufferInfo));

                    VkDescriptorBufferInfo *wrapped_entry = new VkDescriptorBufferInfo(*buffer_entry);
                    wrapped_entry->buffer = layer_data->Unwrap(buffer_entry->buffer);
                    template_entries.emplace_back(offset, kVulkanObjectTypeBuffer, CastToUint64(wrapped_entry), 0);
                } break;

                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
                    auto buffer_view_handle = reinterpret_cast<VkBufferView *>(update_entry);
                    allocation_size = std::max(allocation_size, offset + sizeof(VkBufferView));

                    VkBufferView wrapped_entry = layer_data->Unwrap(*buffer_view_handle);
                    template_entries.emplace_back(offset, kVulkanObjectTypeBufferView, CastToUint64(wrapped_entry), 0);
                } break;
                case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT: {
                    size_t numBytes = create_info.pDescriptorUpdateEntries[i].descriptorCount;
                    allocation_size = std::max(allocation_size, offset + numBytes);
                    // nothing to unwrap, just plain data
                    template_entries.emplace_back(offset, kVulkanObjectTypeUnknown, CastToUint64(update_entry),
                                                  numBytes);
                    // to break out of the loop
                    j = create_info.pDescriptorUpdateEntries[i].descriptorCount;
                } break;
                default:
                    assert(0);
                    break;
            }
        }
    }
    // Allocate required buffer size and populate with source/unwrapped data
    void *unwrapped_data = malloc(allocation_size);
    for (auto &this_entry : template_entries) {
        VulkanObjectType type = std::get<1>(this_entry);
        void *destination = (char *)unwrapped_data + std::get<0>(this_entry);
        uint64_t source = std::get<2>(this_entry);
        size_t size = std::get<3>(this_entry);

        if (size != 0) {
            assert(type == kVulkanObjectTypeUnknown);
            memcpy(destination, CastFromUint64<void *>(source), size);
        } else {
            switch (type) {
                case kVulkanObjectTypeImage:
                    *(reinterpret_cast<VkDescriptorImageInfo *>(destination)) =
                        *(reinterpret_cast<VkDescriptorImageInfo *>(source));
                    delete CastFromUint64<VkDescriptorImageInfo *>(source);
                    break;
                case kVulkanObjectTypeBuffer:
                    *(reinterpret_cast<VkDescriptorBufferInfo *>(destination)) =
                        *(CastFromUint64<VkDescriptorBufferInfo *>(source));
                    delete CastFromUint64<VkDescriptorBufferInfo *>(source);
                    break;
                case kVulkanObjectTypeBufferView:
                    *(reinterpret_cast<VkBufferView *>(destination)) = CastFromUint64<VkBufferView>(source);
                    break;
                default:
                    assert(0);
                    break;
            }
        }
    }
    return (void *)unwrapped_data;
}

void DispatchUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet,
                                             VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void *pData) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.UpdateDescriptorSetWithTemplate(device, descriptorSet, descriptorUpdateTemplate,
                                                                                 pData);
    uint64_t template_handle = reinterpret_cast<uint64_t &>(descriptorUpdateTemplate);
    void *unwrapped_buffer = nullptr;
    {
        read_dispatch_lock_guard_t lock(dispatch_lock);
        descriptorSet = layer_data->Unwrap(descriptorSet);
        descriptorUpdateTemplate = (VkDescriptorUpdateTemplate)layer_data->Unwrap(descriptorUpdateTemplate);
        unwrapped_buffer = BuildUnwrappedUpdateTemplateBuffer(layer_data, template_handle, pData);
    }
    layer_data->device_dispatch_table.UpdateDescriptorSetWithTemplate(device, descriptorSet, descriptorUpdateTemplate, unwrapped_buffer);
    free(unwrapped_buffer);
}

void DispatchUpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet,
                                                VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void *pData) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.UpdateDescriptorSetWithTemplateKHR(device, descriptorSet, descriptorUpdateTemplate,
                                                                                    pData);
    uint64_t template_handle = reinterpret_cast<uint64_t &>(descriptorUpdateTemplate);
    void *unwrapped_buffer = nullptr;
    {
        read_dispatch_lock_guard_t lock(dispatch_lock);
        descriptorSet = layer_data->Unwrap(descriptorSet);
        descriptorUpdateTemplate = layer_data->Unwrap(descriptorUpdateTemplate);
        unwrapped_buffer = BuildUnwrappedUpdateTemplateBuffer(layer_data, template_handle, pData);
    }
    layer_data->device_dispatch_table.UpdateDescriptorSetWithTemplateKHR(device, descriptorSet, descriptorUpdateTemplate, unwrapped_buffer);
    free(unwrapped_buffer);
}

void DispatchCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer,
                                                 VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout,
                                                 uint32_t set, const void *pData) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(commandBuffer), layer_data_map);
    if (!wrap_handles)
        return layer_data->device_dispatch_table.CmdPushDescriptorSetWithTemplateKHR(commandBuffer, descriptorUpdateTemplate,
                                                                                     layout, set, pData);
    uint64_t template_handle = reinterpret_cast<uint64_t &>(descriptorUpdateTemplate);
    void *unwrapped_buffer = nullptr;
    {
        read_dispatch_lock_guard_t lock(dispatch_lock);
        descriptorUpdateTemplate = layer_data->Unwrap(descriptorUpdateTemplate);
        layout = layer_data->Unwrap(layout);
        unwrapped_buffer = BuildUnwrappedUpdateTemplateBuffer(layer_data, template_handle, pData);
    }
    layer_data->device_dispatch_table.CmdPushDescriptorSetWithTemplateKHR(commandBuffer, descriptorUpdateTemplate, layout, set,
                                                                 unwrapped_buffer);
    free(unwrapped_buffer);
}

VkResult DispatchGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                                       VkDisplayPropertiesKHR *pProperties) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    VkResult result =
        layer_data->instance_dispatch_table.GetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, pPropertyCount, pProperties);
    if (!wrap_handles) return result;
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pProperties) {
        for (uint32_t idx0 = 0; idx0 < *pPropertyCount; ++idx0) {
            pProperties[idx0].display = layer_data->MaybeWrapDisplay(pProperties[idx0].display, layer_data);
        }
    }
    return result;
}

VkResult DispatchGetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                                        VkDisplayProperties2KHR *pProperties) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    VkResult result =
        layer_data->instance_dispatch_table.GetPhysicalDeviceDisplayProperties2KHR(physicalDevice, pPropertyCount, pProperties);
    if (!wrap_handles) return result;
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pProperties) {
        for (uint32_t idx0 = 0; idx0 < *pPropertyCount; ++idx0) {
            pProperties[idx0].displayProperties.display =
                layer_data->MaybeWrapDisplay(pProperties[idx0].displayProperties.display, layer_data);
        }
    }
    return result;
}

VkResult DispatchGetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                                            VkDisplayPlanePropertiesKHR *pProperties) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    VkResult result =
        layer_data->instance_dispatch_table.GetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, pPropertyCount, pProperties);
    if (!wrap_handles) return result;
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pProperties) {
        for (uint32_t idx0 = 0; idx0 < *pPropertyCount; ++idx0) {
            VkDisplayKHR &opt_display = pProperties[idx0].currentDisplay;
            if (opt_display) opt_display = layer_data->MaybeWrapDisplay(opt_display, layer_data);
        }
    }
    return result;
}

VkResult DispatchGetPhysicalDeviceDisplayPlaneProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                                             VkDisplayPlaneProperties2KHR *pProperties) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    VkResult result = layer_data->instance_dispatch_table.GetPhysicalDeviceDisplayPlaneProperties2KHR(physicalDevice,
                                                                                                      pPropertyCount, pProperties);
    if (!wrap_handles) return result;
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pProperties) {
        for (uint32_t idx0 = 0; idx0 < *pPropertyCount; ++idx0) {
            VkDisplayKHR &opt_display = pProperties[idx0].displayPlaneProperties.currentDisplay;
            if (opt_display) opt_display = layer_data->MaybeWrapDisplay(opt_display, layer_data);
        }
    }
    return result;
}

VkResult DispatchGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t *pDisplayCount,
                                                     VkDisplayKHR *pDisplays) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    VkResult result = layer_data->instance_dispatch_table.GetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex,
                                                                                              pDisplayCount, pDisplays);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pDisplays) {
    if (!wrap_handles) return result;
        for (uint32_t i = 0; i < *pDisplayCount; ++i) {
            if (pDisplays[i]) pDisplays[i] = layer_data->MaybeWrapDisplay(pDisplays[i], layer_data);
        }
    }
    return result;
}

VkResult DispatchGetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t *pPropertyCount,
                                             VkDisplayModePropertiesKHR *pProperties) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    if (!wrap_handles)
        return layer_data->instance_dispatch_table.GetDisplayModePropertiesKHR(physicalDevice, display, pPropertyCount,
                                                                               pProperties);
    {
        display = layer_data->Unwrap(display);
    }

    VkResult result = layer_data->instance_dispatch_table.GetDisplayModePropertiesKHR(physicalDevice, display, pPropertyCount, pProperties);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pProperties) {
        for (uint32_t idx0 = 0; idx0 < *pPropertyCount; ++idx0) {
            pProperties[idx0].displayMode = layer_data->WrapNew(pProperties[idx0].displayMode);
        }
    }
    return result;
}

VkResult DispatchGetDisplayModeProperties2KHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t *pPropertyCount,
                                              VkDisplayModeProperties2KHR *pProperties) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(physicalDevice), layer_data_map);
    if (!wrap_handles)
        return layer_data->instance_dispatch_table.GetDisplayModeProperties2KHR(physicalDevice, display, pPropertyCount,
                                                                                pProperties);
    {
        display = layer_data->Unwrap(display);
    }

    VkResult result =
        layer_data->instance_dispatch_table.GetDisplayModeProperties2KHR(physicalDevice, display, pPropertyCount, pProperties);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pProperties) {
        for (uint32_t idx0 = 0; idx0 < *pPropertyCount; ++idx0) {
            pProperties[idx0].displayModeProperties.displayMode = layer_data->WrapNew(pProperties[idx0].displayModeProperties.displayMode);
        }
    }
    return result;
}

VkResult DispatchDebugMarkerSetObjectTagEXT(VkDevice device, const VkDebugMarkerObjectTagInfoEXT *pTagInfo) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.DebugMarkerSetObjectTagEXT(device, pTagInfo);
    safe_VkDebugMarkerObjectTagInfoEXT local_tag_info(pTagInfo);
    {
        auto it = unique_id_mapping.find(reinterpret_cast<uint64_t &>(local_tag_info.object));
        if (it != unique_id_mapping.end()) {
            local_tag_info.object = it->second;
        }
    }
    VkResult result = layer_data->device_dispatch_table.DebugMarkerSetObjectTagEXT(device, 
                                                                                   reinterpret_cast<VkDebugMarkerObjectTagInfoEXT *>(&local_tag_info));
    return result;
}

VkResult DispatchDebugMarkerSetObjectNameEXT(VkDevice device, const VkDebugMarkerObjectNameInfoEXT *pNameInfo) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.DebugMarkerSetObjectNameEXT(device, pNameInfo);
    safe_VkDebugMarkerObjectNameInfoEXT local_name_info(pNameInfo);
    {
        auto it = unique_id_mapping.find(reinterpret_cast<uint64_t &>(local_name_info.object));
        if (it != unique_id_mapping.end()) {
            local_name_info.object = it->second;
        }
    }
    VkResult result = layer_data->device_dispatch_table.DebugMarkerSetObjectNameEXT(
        device, reinterpret_cast<VkDebugMarkerObjectNameInfoEXT *>(&local_name_info));
    return result;
}

// VK_EXT_debug_utils
VkResult DispatchSetDebugUtilsObjectTagEXT(VkDevice device, const VkDebugUtilsObjectTagInfoEXT *pTagInfo) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.SetDebugUtilsObjectTagEXT(device, pTagInfo);
    safe_VkDebugUtilsObjectTagInfoEXT local_tag_info(pTagInfo);
    {
        auto it = unique_id_mapping.find(reinterpret_cast<uint64_t &>(local_tag_info.objectHandle));
        if (it != unique_id_mapping.end()) {
            local_tag_info.objectHandle = it->second;
        }
    }
    VkResult result = layer_data->device_dispatch_table.SetDebugUtilsObjectTagEXT(
        device, reinterpret_cast<const VkDebugUtilsObjectTagInfoEXT *>(&local_tag_info));
    return result;
}

VkResult DispatchSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT *pNameInfo) {
    auto layer_data = GetLayerDataPtr(get_dispatch_key(device), layer_data_map);
    if (!wrap_handles) return layer_data->device_dispatch_table.SetDebugUtilsObjectNameEXT(device, pNameInfo);
    safe_VkDebugUtilsObjectNameInfoEXT local_name_info(pNameInfo);
    {
        auto it = unique_id_mapping.find(reinterpret_cast<uint64_t &>(local_name_info.objectHandle));
        if (it != unique_id_mapping.end()) {
            local_name_info.objectHandle = it->second;
        }
    }
    VkResult result = layer_data->device_dispatch_table.SetDebugUtilsObjectNameEXT(
        device, reinterpret_cast<const VkDebugUtilsObjectNameInfoEXT *>(&local_name_info));
    return result;
}

"""
    # Separate generated text for source and headers
    ALL_SECTIONS = ['source_file', 'header_file']
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        self.INDENT_SPACES = 4
        self.instance_extensions = []
        self.device_extensions = []
        # Commands which are not autogenerated but still intercepted
        self.no_autogen_list = [
            'vkCreateInstance',
            'vkDestroyInstance',
            'vkCreateDevice',
            'vkDestroyDevice',
            'vkCreateComputePipelines',
            'vkCreateGraphicsPipelines',
            'vkCreateSwapchainKHR',
            'vkCreateSharedSwapchainsKHR',
            'vkGetSwapchainImagesKHR',
            'vkDestroySwapchainKHR',
            'vkQueuePresentKHR',
            'vkResetDescriptorPool',
            'vkDestroyDescriptorPool',
            'vkAllocateDescriptorSets',
            'vkFreeDescriptorSets',
            'vkCreateDescriptorUpdateTemplate',
            'vkCreateDescriptorUpdateTemplateKHR',
            'vkDestroyDescriptorUpdateTemplate',
            'vkDestroyDescriptorUpdateTemplateKHR',
            'vkUpdateDescriptorSetWithTemplate',
            'vkUpdateDescriptorSetWithTemplateKHR',
            'vkCmdPushDescriptorSetWithTemplateKHR',
            'vkDebugMarkerSetObjectTagEXT',
            'vkDebugMarkerSetObjectNameEXT',
            'vkCreateRenderPass',
            'vkCreateRenderPass2KHR',
            'vkDestroyRenderPass',
            'vkSetDebugUtilsObjectNameEXT',
            'vkSetDebugUtilsObjectTagEXT',
            'vkGetPhysicalDeviceDisplayPropertiesKHR',
            'vkGetPhysicalDeviceDisplayProperties2KHR',
            'vkGetPhysicalDeviceDisplayPlanePropertiesKHR',
            'vkGetPhysicalDeviceDisplayPlaneProperties2KHR',
            'vkGetDisplayPlaneSupportedDisplaysKHR',
            'vkGetDisplayModePropertiesKHR',
            'vkGetDisplayModeProperties2KHR',
            'vkEnumerateInstanceExtensionProperties',
            'vkEnumerateInstanceLayerProperties',
            'vkEnumerateDeviceExtensionProperties',
            'vkEnumerateDeviceLayerProperties',
            'vkEnumerateInstanceVersion',
            ]
        self.headerVersion = None
        # Internal state - accumulators for different inner block text
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])

        self.cmdMembers = []
        self.cmd_feature_protect = []  # Save ifdef's for each command
        self.cmd_info_data = []        # Save the cmdinfo data for wrapping the handles when processing is complete
        self.structMembers = []        # List of StructMemberData records for all Vulkan structs
        self.extension_structs = []    # List of all structs or sister-structs containing handles
                                       # A sister-struct may contain no handles but shares a structextends attribute with one that does
        self.pnext_extension_structs = []    # List of all structs which can be extended by a pnext chain
        self.structTypes = dict()      # Map of Vulkan struct typename to required VkStructureType
        self.struct_member_dict = dict()
        # Named tuples to store struct and command data
        self.StructType = namedtuple('StructType', ['name', 'value'])
        self.CmdMemberData = namedtuple('CmdMemberData', ['name', 'members'])
        self.CmdInfoData = namedtuple('CmdInfoData', ['name', 'cmdinfo'])
        self.CmdExtraProtect = namedtuple('CmdExtraProtect', ['name', 'extra_protect'])

        self.CommandParam = namedtuple('CommandParam', ['type', 'name', 'ispointer', 'isconst', 'iscount', 'len', 'extstructs', 'cdecl', 'islocal', 'iscreate', 'isdestroy', 'feature_protect'])
        self.StructMemberData = namedtuple('StructMemberData', ['name', 'members'])
    #
    def incIndent(self, indent):
        inc = ' ' * self.INDENT_SPACES
        if indent:
            return indent + inc
        return inc
    #
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
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        # Initialize members that require the tree
        self.handle_types = GetHandleTypes(self.registry.tree)
        self.type_categories = GetTypeCategories(self.registry.tree)
        # Output Copyright
        self.appendSection('header_file', self.inline_copyright_message)
        # Multiple inclusion protection & C++ namespace.
        self.header = False
        if (self.genOpts.filename and 'h' == self.genOpts.filename[-1]):
            self.header = True
            self.appendSection('header_file', '#pragma once')
            self.appendSection('header_file', '')
            self.appendSection('header_file', '#if defined(LAYER_CHASSIS_CAN_WRAP_HANDLES)')
            self.appendSection('header_file', 'extern bool wrap_handles;')
            self.appendSection('header_file', '#else')
            self.appendSection('header_file', 'extern bool wrap_handles;')
            self.appendSection('header_file', '#endif')

    # Now that the data is all collected and complete, generate and output the wrapping/unwrapping routines
    def endFile(self):
        self.struct_member_dict = dict(self.structMembers)
        # Generate the list of APIs that might need to handle wrapped extension structs
        self.GenerateCommandWrapExtensionList()
        # Write out wrapping/unwrapping functions
        self.WrapCommands()
        # Build and write out pNext processing function
        extension_proc = self.build_extension_processing_func()

        if not self.header:
            write(self.inline_copyright_message, file=self.outFile)
            self.newline()
            write('#include <mutex>', file=self.outFile)
            write('#include "chassis.h"', file=self.outFile)
            write('#include "layer_chassis_dispatch.h"', file=self.outFile)
            write('#include "vk_layer_utils.h"', file=self.outFile)
            self.newline()
            write('// This intentionally includes a cpp file', file=self.outFile)
            write('#include "vk_safe_struct.cpp"', file=self.outFile)
            self.newline()
            write('// shared_mutex support added in MSVC 2015 update 2', file=self.outFile)
            write('#if defined(_MSC_FULL_VER) && _MSC_FULL_VER >= 190023918 && NTDDI_VERSION > NTDDI_WIN10_RS2', file=self.outFile)
            write('    #include <shared_mutex>', file=self.outFile)
            write('    typedef std::shared_mutex dispatch_lock_t;', file=self.outFile)
            write('    typedef std::shared_lock<dispatch_lock_t> read_dispatch_lock_guard_t;', file=self.outFile)
            write('    typedef std::unique_lock<dispatch_lock_t> write_dispatch_lock_guard_t;', file=self.outFile)
            write('#else', file=self.outFile)
            write('    typedef std::mutex dispatch_lock_t;', file=self.outFile)
            write('    typedef std::unique_lock<dispatch_lock_t> read_dispatch_lock_guard_t;', file=self.outFile)
            write('    typedef std::unique_lock<dispatch_lock_t> write_dispatch_lock_guard_t;', file=self.outFile)
            write('#endif', file=self.outFile)
            write('dispatch_lock_t dispatch_lock;', file=self.outFile)
            self.newline()
            write('// Unique Objects pNext extension handling function', file=self.outFile)
            write('%s' % extension_proc, file=self.outFile)
            self.newline()
            write('// Manually written Dispatch routines', file=self.outFile)
            write('%s' % self.inline_custom_source_preamble, file=self.outFile)
            self.newline()
            if (self.sections['source_file']):
                write('\n'.join(self.sections['source_file']), end=u'', file=self.outFile)
        else:
            self.newline()
            if (self.sections['header_file']):
                write('\n'.join(self.sections['header_file']), end=u'', file=self.outFile)

        # Finish processing in superclass
        OutputGenerator.endFile(self)
    #
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
    def endFeature(self):
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    def genType(self, typeinfo, name, alias):
        OutputGenerator.genType(self, typeinfo, name, alias)
        typeElem = typeinfo.elem
        # If the type is a struct type, traverse the imbedded <member> tags generating a structure.
        # Otherwise, emit the tag text.
        category = typeElem.get('category')
        if (category == 'struct' or category == 'union'):
            self.genStruct(typeinfo, name, alias)
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
            extstructs = self.registry.validextensionstructs[typeName] if name == 'pNext' else None
            membersInfo.append(self.CommandParam(type=type,
                                                 name=name,
                                                 ispointer=self.paramIsPointer(member),
                                                 isconst=True if 'const' in cdecl else False,
                                                 iscount=True if name in lens else False,
                                                 len=self.getLen(member),
                                                 extstructs=extstructs,
                                                 cdecl=cdecl,
                                                 islocal=False,
                                                 iscreate=False,
                                                 isdestroy=False,
                                                 feature_protect=self.featureExtraProtect))
        self.structMembers.append(self.StructMemberData(name=typeName, members=membersInfo))

    #
    # Determine if a struct has an NDO as a member or an embedded member
    def struct_contains_ndo(self, struct_item):
        struct_member_dict = dict(self.structMembers)
        struct_members = struct_member_dict[struct_item]

        for member in struct_members:
            if self.handle_types.IsNonDispatchable(member.type):
                return True
            elif member.type in struct_member_dict:
                if self.struct_contains_ndo(member.type) == True:
                    return True
        return False
    #
    # Return list of struct members which contain, or which sub-structures contain
    # an NDO in a given list of parameters or members
    def getParmeterStructsWithNdos(self, item_list):
        struct_list = set()
        for item in item_list:
            paramtype = item.find('type')
            typecategory = self.type_categories[paramtype.text]
            if typecategory == 'struct':
                if self.struct_contains_ndo(paramtype.text) == True:
                    struct_list.add(item)
        return struct_list
    #
    # Return list of non-dispatchable objects from a given list of parameters or members
    def getNdosInParameterList(self, item_list, create_func):
        ndo_list = set()
        if create_func == True:
            member_list = item_list[0:-1]
        else:
            member_list = item_list
        for item in member_list:
            if self.handle_types.IsNonDispatchable(paramtype.text):
                ndo_list.add(item)
        return ndo_list
    #
    # Construct list of extension structs containing handles, or extension structs that share a structextends attribute
    # WITH an extension struct containing handles. All extension structs in any pNext chain will have to be copied.
    # TODO: make this recursive -- structs buried three or more levels deep are not searched for extensions
    def GenerateCommandWrapExtensionList(self):
        for struct in self.structMembers:
            if (len(struct.members) > 1) and struct.members[1].extstructs is not None:
                found = False;
                for item in struct.members[1].extstructs:
                    if item != '' and item not in self.pnext_extension_structs:
                        self.pnext_extension_structs.append(item)
                    if item != '' and self.struct_contains_ndo(item) == True:
                        found = True
                if found == True:
                    for item in struct.members[1].extstructs:
                        if item != '' and item not in self.extension_structs:
                            self.extension_structs.append(item)
    #
    # Returns True if a struct may have a pNext chain containing an NDO
    def StructWithExtensions(self, struct_type):
        if struct_type in self.struct_member_dict:
            param_info = self.struct_member_dict[struct_type]
            if (len(param_info) > 1) and param_info[1].extstructs is not None:
                for item in param_info[1].extstructs:
                    if item in self.extension_structs:
                        return True
        return False
    #
    # Generate pNext handling function
    def build_extension_processing_func(self):
        # Construct helper functions to build and free pNext extension chains
        pnext_proc = ''
        pnext_proc += 'void WrapPnextChainHandles(ValidationObject *layer_data, const void *pNext) {\n'
        pnext_proc += '    void *cur_pnext = const_cast<void *>(pNext);\n'
        pnext_proc += '    while (cur_pnext != NULL) {\n'
        pnext_proc += '        VkBaseOutStructure *header = reinterpret_cast<VkBaseOutStructure *>(cur_pnext);\n\n'
        pnext_proc += '        switch (header->sType) {\n'
        for item in self.pnext_extension_structs:
            struct_info = self.struct_member_dict[item]
            indent = '                '
            (tmp_decl, tmp_pre, tmp_post) = self.uniquify_members(struct_info, indent, 'safe_struct->', 0, False, False, False, False)
            # Only process extension structs containing handles
            if not tmp_pre:
                continue
            if struct_info[0].feature_protect is not None:
                pnext_proc += '#ifdef %s \n' % struct_info[0].feature_protect
            pnext_proc += '            case %s: {\n' % self.structTypes[item].value
            pnext_proc += '                    safe_%s *safe_struct = reinterpret_cast<safe_%s *>(cur_pnext);\n' % (item, item)
            # Generate code to unwrap the handles
            pnext_proc += tmp_pre
            pnext_proc += '                } break;\n'
            if struct_info[0].feature_protect is not None:
                pnext_proc += '#endif // %s \n' % struct_info[0].feature_protect
            pnext_proc += '\n'
        pnext_proc += '            default:\n'
        pnext_proc += '                break;\n'
        pnext_proc += '        }\n\n'
        pnext_proc += '        // Process the next structure in the chain\n'
        pnext_proc += '        cur_pnext = header->pNext;\n'
        pnext_proc += '    }\n'
        pnext_proc += '}\n'
        return pnext_proc

    #
    # Generate source for creating a non-dispatchable object
    def generate_create_ndo_code(self, indent, proto, params, cmd_info):
        create_ndo_code = ''
        handle_type = params[-1].find('type')
        if self.handle_types.IsNonDispatchable(handle_type.text):
            # Check for special case where multiple handles are returned
            ndo_array = False
            if cmd_info[-1].len is not None:
                ndo_array = True;
            handle_name = params[-1].find('name')
            create_ndo_code += '%sif (VK_SUCCESS == result) {\n' % (indent)
            indent = self.incIndent(indent)
            ndo_dest = '*%s' % handle_name.text
            if ndo_array == True:
                create_ndo_code += '%sfor (uint32_t index0 = 0; index0 < %s; index0++) {\n' % (indent, cmd_info[-1].len)
                indent = self.incIndent(indent)
                ndo_dest = '%s[index0]' % cmd_info[-1].name
            create_ndo_code += '%s%s = layer_data->WrapNew(%s);\n' % (indent, ndo_dest, ndo_dest)
            if ndo_array == True:
                indent = self.decIndent(indent)
                create_ndo_code += '%s}\n' % indent
            indent = self.decIndent(indent)
            create_ndo_code += '%s}\n' % (indent)
        return create_ndo_code
    #
    # Generate source for destroying a non-dispatchable object
    def generate_destroy_ndo_code(self, indent, proto, cmd_info):
        destroy_ndo_code = ''
        ndo_array = False
        if True in [destroy_txt in proto.text for destroy_txt in ['Destroy', 'Free']]:
            # Check for special case where multiple handles are returned
            if cmd_info[-1].len is not None:
                ndo_array = True;
                param = -1
            else:
                param = -2
            if self.handle_types.IsNonDispatchable(cmd_info[param].type):
                if ndo_array == True:
                    # This API is freeing an array of handles.  Remove them from the unique_id map.
                    destroy_ndo_code += '%sif ((VK_SUCCESS == result) && (%s)) {\n' % (indent, cmd_info[param].name)
                    indent = self.incIndent(indent)
                    destroy_ndo_code += '%sfor (uint32_t index0 = 0; index0 < %s; index0++) {\n' % (indent, cmd_info[param].len)
                    indent = self.incIndent(indent)
                    destroy_ndo_code += '%s%s handle = %s[index0];\n' % (indent, cmd_info[param].type, cmd_info[param].name)
                    destroy_ndo_code += '%suint64_t unique_id = reinterpret_cast<uint64_t &>(handle);\n' % (indent)
                    destroy_ndo_code += '%sunique_id_mapping.erase(unique_id);\n' % (indent)
                    indent = self.decIndent(indent);
                    destroy_ndo_code += '%s}\n' % indent
                    indent = self.decIndent(indent);
                    destroy_ndo_code += '%s}\n' % indent
                else:
                    # Remove a single handle from the map
                    destroy_ndo_code += '%suint64_t %s_id = reinterpret_cast<uint64_t &>(%s);\n' % (indent, cmd_info[param].name, cmd_info[param].name)
                    destroy_ndo_code += '%sauto iter = unique_id_mapping.pop(%s_id);\n' % (indent, cmd_info[param].name)
                    destroy_ndo_code += '%sif (iter != unique_id_mapping.end()) {\n' % (indent)
                    indent = self.incIndent(indent)
                    destroy_ndo_code += '%s%s = (%s)iter->second;\n' % (indent, cmd_info[param].name, cmd_info[param].type)
                    indent = self.decIndent(indent);
                    destroy_ndo_code += '%s} else {\n' % (indent)
                    indent = self.incIndent(indent)
                    destroy_ndo_code += '%s%s = (%s)0;\n' % (indent, cmd_info[param].name, cmd_info[param].type)
                    indent = self.decIndent(indent);
                    destroy_ndo_code += '%s}\n' % (indent)

        return ndo_array, destroy_ndo_code

    #
    # Clean up local declarations
    def cleanUpLocalDeclarations(self, indent, prefix, name, len, index):
        cleanup = ''
        if len is not None:
            cleanup = '%sif (local_%s%s) {\n' % (indent, prefix, name)
            cleanup += '%s    delete[] local_%s%s;\n' % (indent, prefix, name)
            cleanup += "%s}\n" % (indent)
        return cleanup
    #
    # Output UO code for a single NDO (ndo_count is NULL) or a counted list of NDOs
    def outputNDOs(self, ndo_type, ndo_name, ndo_count, prefix, index, indent, destroy_func, destroy_array, top_level):
        decl_code = ''
        pre_call_code = ''
        post_call_code = ''
        if ndo_count is not None:
            if top_level == True:
                decl_code += '%s%s var_local_%s%s[DISPATCH_MAX_STACK_ALLOCATIONS];\n' % (indent, ndo_type, prefix, ndo_name)
                decl_code += '%s%s *local_%s%s = NULL;\n' % (indent, ndo_type, prefix, ndo_name)
            pre_call_code += '%s    if (%s%s) {\n' % (indent, prefix, ndo_name)
            indent = self.incIndent(indent)
            if top_level == True:
                pre_call_code += '%s    local_%s%s = %s > DISPATCH_MAX_STACK_ALLOCATIONS ? new %s[%s] : var_local_%s%s;\n' % (indent, prefix, ndo_name, ndo_count, ndo_type, ndo_count, prefix, ndo_name)
                pre_call_code += '%s    for (uint32_t %s = 0; %s < %s; ++%s) {\n' % (indent, index, index, ndo_count, index)
                indent = self.incIndent(indent)
                pre_call_code += '%s    local_%s%s[%s] = layer_data->Unwrap(%s[%s]);\n' % (indent, prefix, ndo_name, index, ndo_name, index)
            else:
                pre_call_code += '%s    for (uint32_t %s = 0; %s < %s; ++%s) {\n' % (indent, index, index, ndo_count, index)
                indent = self.incIndent(indent)
                pre_call_code += '%s    %s%s[%s] = layer_data->Unwrap(%s%s[%s]);\n' % (indent, prefix, ndo_name, index, prefix, ndo_name, index)
            indent = self.decIndent(indent)
            pre_call_code += '%s    }\n' % indent
            indent = self.decIndent(indent)
            pre_call_code += '%s    }\n' % indent
            if top_level == True:
                post_call_code += '%sif (local_%s%s != var_local_%s%s)\n' % (indent, prefix, ndo_name, prefix, ndo_name)
                indent = self.incIndent(indent)
                post_call_code += '%sdelete[] local_%s;\n' % (indent, ndo_name)
        else:
            if top_level == True:
                if (destroy_func == False) or (destroy_array == True):
                    pre_call_code += '%s    %s = layer_data->Unwrap(%s);\n' % (indent, ndo_name, ndo_name)
            else:
                # Make temp copy of this var with the 'local' removed. It may be better to not pass in 'local_'
                # as part of the string and explicitly print it
                fix = str(prefix).strip('local_');
                pre_call_code += '%s    if (%s%s) {\n' % (indent, fix, ndo_name)
                indent = self.incIndent(indent)
                pre_call_code += '%s    %s%s = layer_data->Unwrap(%s%s);\n' % (indent, prefix, ndo_name, fix, ndo_name)
                indent = self.decIndent(indent)
                pre_call_code += '%s    }\n' % indent
        return decl_code, pre_call_code, post_call_code
    #
    # first_level_param indicates if elements are passed directly into the function else they're below a ptr/struct
    # create_func means that this is API creates or allocates NDOs
    # destroy_func indicates that this API destroys or frees NDOs
    # destroy_array means that the destroy_func operated on an array of NDOs
    def uniquify_members(self, members, indent, prefix, array_index, create_func, destroy_func, destroy_array, first_level_param):
        decls = ''
        pre_code = ''
        post_code = ''
        index = 'index%s' % str(array_index)
        array_index += 1
        # Process any NDOs in this structure and recurse for any sub-structs in this struct
        for member in members:
            process_pnext = self.StructWithExtensions(member.type)
            # Handle NDOs
            if self.handle_types.IsNonDispatchable(member.type):
                count_name = member.len
                if (count_name is not None):
                    if first_level_param == False:
                        count_name = '%s%s' % (prefix, member.len)

                if (first_level_param == False) or (create_func == False) or (not '*' in member.cdecl):
                    (tmp_decl, tmp_pre, tmp_post) = self.outputNDOs(member.type, member.name, count_name, prefix, index, indent, destroy_func, destroy_array, first_level_param)
                    decls += tmp_decl
                    pre_code += tmp_pre
                    post_code += tmp_post
            # Handle Structs that contain NDOs at some level
            elif member.type in self.struct_member_dict:
                # Structs at first level will have an NDO, OR, we need a safe_struct for the pnext chain
                if self.struct_contains_ndo(member.type) == True or process_pnext:
                    struct_info = self.struct_member_dict[member.type]
                    # TODO (jbolz): Can this use paramIsPointer?
                    ispointer = '*' in member.cdecl;
                    # Struct Array
                    if member.len is not None:
                        # Update struct prefix
                        if first_level_param == True:
                            new_prefix = 'local_%s' % member.name
                            # Declare safe_VarType for struct
                            decls += '%ssafe_%s *%s = NULL;\n' % (indent, member.type, new_prefix)
                        else:
                            new_prefix = '%s%s' % (prefix, member.name)
                        pre_code += '%s    if (%s%s) {\n' % (indent, prefix, member.name)
                        indent = self.incIndent(indent)
                        if first_level_param == True:
                            pre_code += '%s    %s = new safe_%s[%s];\n' % (indent, new_prefix, member.type, member.len)
                        pre_code += '%s    for (uint32_t %s = 0; %s < %s%s; ++%s) {\n' % (indent, index, index, prefix, member.len, index)
                        indent = self.incIndent(indent)
                        if first_level_param == True:
                            pre_code += '%s    %s[%s].initialize(&%s[%s]);\n' % (indent, new_prefix, index, member.name, index)
                            if process_pnext:
                                pre_code += '%s    WrapPnextChainHandles(layer_data, %s[%s].pNext);\n' % (indent, new_prefix, index)
                        local_prefix = '%s[%s].' % (new_prefix, index)
                        # Process sub-structs in this struct
                        (tmp_decl, tmp_pre, tmp_post) = self.uniquify_members(struct_info, indent, local_prefix, array_index, create_func, destroy_func, destroy_array, False)
                        decls += tmp_decl
                        pre_code += tmp_pre
                        post_code += tmp_post
                        indent = self.decIndent(indent)
                        pre_code += '%s    }\n' % indent
                        indent = self.decIndent(indent)
                        pre_code += '%s    }\n' % indent
                        if first_level_param == True:
                            post_code += self.cleanUpLocalDeclarations(indent, prefix, member.name, member.len, index)
                    # Single Struct
                    elif ispointer:
                        # Update struct prefix
                        if first_level_param == True:
                            new_prefix = 'local_%s->' % member.name
                            decls += '%ssafe_%s var_local_%s%s;\n' % (indent, member.type, prefix, member.name)
                            decls += '%ssafe_%s *local_%s%s = NULL;\n' % (indent, member.type, prefix, member.name)
                        else:
                            new_prefix = '%s%s->' % (prefix, member.name)
                        # Declare safe_VarType for struct
                        pre_code += '%s    if (%s%s) {\n' % (indent, prefix, member.name)
                        indent = self.incIndent(indent)
                        if first_level_param == True:
                            pre_code += '%s    local_%s%s = &var_local_%s%s;\n' % (indent, prefix, member.name, prefix, member.name);
                            pre_code += '%s    local_%s%s->initialize(%s);\n' % (indent, prefix, member.name, member.name)
                        # Process sub-structs in this struct
                        (tmp_decl, tmp_pre, tmp_post) = self.uniquify_members(struct_info, indent, new_prefix, array_index, create_func, destroy_func, destroy_array, False)
                        decls += tmp_decl
                        pre_code += tmp_pre
                        post_code += tmp_post
                        if process_pnext:
                            pre_code += '%s    WrapPnextChainHandles(layer_data, local_%s%s->pNext);\n' % (indent, prefix, member.name)
                        indent = self.decIndent(indent)
                        pre_code += '%s    }\n' % indent
                        if first_level_param == True:
                            post_code += self.cleanUpLocalDeclarations(indent, prefix, member.name, member.len, index)
                    else:
                        # Update struct prefix
                        if first_level_param == True:
                            sys.exit(1)
                        else:
                            new_prefix = '%s%s.' % (prefix, member.name)
                        # Process sub-structs in this struct
                        (tmp_decl, tmp_pre, tmp_post) = self.uniquify_members(struct_info, indent, new_prefix, array_index, create_func, destroy_func, destroy_array, False)
                        decls += tmp_decl
                        pre_code += tmp_pre
                        post_code += tmp_post
                        if process_pnext:
                            pre_code += '%s    WrapPnextChainHandles(layer_data, local_%s%s.pNext);\n' % (indent, prefix, member.name)
        return decls, pre_code, post_code
    #
    # For a particular API, generate the non-dispatchable-object wrapping/unwrapping code
    def generate_wrapping_code(self, cmd):
        indent = '    '
        proto = cmd.find('proto/name')
        params = cmd.findall('param')

        if proto.text is not None:
            cmd_member_dict = dict(self.cmdMembers)
            cmd_info = cmd_member_dict[proto.text]
            # Handle ndo create/allocate operations
            if cmd_info[0].iscreate:
                create_ndo_code = self.generate_create_ndo_code(indent, proto, params, cmd_info)
            else:
                create_ndo_code = ''
            # Handle ndo destroy/free operations
            if cmd_info[0].isdestroy:
                (destroy_array, destroy_ndo_code) = self.generate_destroy_ndo_code(indent, proto, cmd_info)
            else:
                destroy_array = False
                destroy_ndo_code = ''
            paramdecl = ''
            param_pre_code = ''
            param_post_code = ''
            create_func = True if create_ndo_code else False
            destroy_func = True if destroy_ndo_code else False
            (paramdecl, param_pre_code, param_post_code) = self.uniquify_members(cmd_info, indent, '', 0, create_func, destroy_func, destroy_array, True)
            param_post_code += create_ndo_code
            if destroy_ndo_code:
                if destroy_array == True:
                    param_post_code += destroy_ndo_code
                else:
                    param_pre_code += destroy_ndo_code
            if param_pre_code:
                if (not destroy_func) or (destroy_array):
                    param_pre_code = '%s{\n%s%s}\n' % ('    ', param_pre_code, indent)
        return paramdecl, param_pre_code, param_post_code
    #
    # Capture command parameter info needed to wrap NDOs as well as handling some boilerplate code
    def genCmd(self, cmdinfo, cmdname, alias):

        # Add struct-member type information to command parameter information
        OutputGenerator.genCmd(self, cmdinfo, cmdname, alias)
        members = cmdinfo.elem.findall('.//param')
        # Iterate over members once to get length parameters for arrays
        lens = set()
        for member in members:
            len = self.getLen(member)
            if len:
                lens.add(len)
        struct_member_dict = dict(self.structMembers)
        # Generate member info
        membersInfo = []
        for member in members:
            # Get type and name of member
            info = self.getTypeNameTuple(member)
            type = info[0]
            name = info[1]
            cdecl = self.makeCParamDecl(member, 0)
            # Check for parameter name in lens set
            iscount = True if name in lens else False
            len = self.getLen(member)
            isconst = True if 'const' in cdecl else False
            ispointer = self.paramIsPointer(member)
            # Mark param as local if it is an array of NDOs
            islocal = False;
            if self.handle_types.IsNonDispatchable(type):
                if (len is not None) and (isconst == True):
                    islocal = True
            # Or if it's a struct that contains an NDO
            elif type in struct_member_dict:
                if self.struct_contains_ndo(type) == True:
                    islocal = True
            isdestroy = True if True in [destroy_txt in cmdname for destroy_txt in ['Destroy', 'Free']] else False
            iscreate = True if True in [create_txt in cmdname for create_txt in ['Create', 'Allocate', 'GetRandROutputDisplayEXT', 'RegisterDeviceEvent', 'RegisterDisplayEvent']] else False
            extstructs = self.registry.validextensionstructs[type] if name == 'pNext' else None
            membersInfo.append(self.CommandParam(type=type,
                                                 name=name,
                                                 ispointer=ispointer,
                                                 isconst=isconst,
                                                 iscount=iscount,
                                                 len=len,
                                                 extstructs=extstructs,
                                                 cdecl=cdecl,
                                                 islocal=islocal,
                                                 iscreate=iscreate,
                                                 isdestroy=isdestroy,
                                                 feature_protect=self.featureExtraProtect))
        self.cmdMembers.append(self.CmdMemberData(name=cmdname, members=membersInfo))
        self.cmd_info_data.append(self.CmdInfoData(name=cmdname, cmdinfo=cmdinfo))
        self.cmd_feature_protect.append(self.CmdExtraProtect(name=cmdname, extra_protect=self.featureExtraProtect))
    #
    # Create prototype for dispatch header file
    def GenDispatchFunctionPrototype(self, cmdinfo, ifdef_text):
        decls = self.makeCDecls(cmdinfo.elem)
        func_sig = decls[0][:-1]
        func_sig = func_sig.replace("VKAPI_ATTR ", "")
        func_sig = func_sig.replace("VKAPI_CALL ", "Dispatch")
        func_sig += ';'
        dispatch_prototype = ''
        if ifdef_text is not None:
            dispatch_prototype = '#ifdef %s\n' % ifdef_text
        dispatch_prototype += func_sig
        if ifdef_text is not None:
            dispatch_prototype += '\n#endif // %s' % ifdef_text
        return dispatch_prototype
    #
    # Create code to wrap NDOs as well as handling some boilerplate code
    def WrapCommands(self):
        cmd_member_dict = dict(self.cmdMembers)
        cmd_info_dict = dict(self.cmd_info_data)
        cmd_protect_dict = dict(self.cmd_feature_protect)

        for api_call in self.cmdMembers:
            cmdname = api_call.name
            cmdinfo = cmd_info_dict[api_call.name]
            feature_extra_protect = cmd_protect_dict[api_call.name]

            # Add fuction prototype to header data
            self.appendSection('header_file', self.GenDispatchFunctionPrototype(cmdinfo, feature_extra_protect))

            if cmdname in self.no_autogen_list:
                decls = self.makeCDecls(cmdinfo.elem)
                self.appendSection('source_file', '')
                self.appendSection('source_file', '// Skip %s dispatch, manually generated' % cmdname)
                continue

            # Generate NDO wrapping/unwrapping code for all parameters
            (api_decls, api_pre, api_post) = self.generate_wrapping_code(cmdinfo.elem)
            # If API doesn't contain NDO's, we still need to make a down-chain call
            down_chain_call_only = False
            if not api_decls and not api_pre and not api_post:
                down_chain_call_only = True
            if (feature_extra_protect is not None):
                self.appendSection('source_file', '')
                self.appendSection('source_file', '#ifdef ' + feature_extra_protect)

            decls = self.makeCDecls(cmdinfo.elem)
            func_sig = decls[0][:-1]
            func_sig = func_sig.replace("VKAPI_ATTR ", "")
            func_sig = func_sig.replace("VKAPI_CALL ", "Dispatch")
            self.appendSection('source_file', '')
            self.appendSection('source_file', func_sig)
            self.appendSection('source_file', '{')
            # Setup common to call wrappers, first parameter is always dispatchable
            dispatchable_type = cmdinfo.elem.find('param/type').text
            dispatchable_name = cmdinfo.elem.find('param/name').text

            # Gather the parameter items
            params = cmdinfo.elem.findall('param/name')
            # Pull out the text for each of the parameters, separate them by commas in a list
            paramstext = ', '.join([str(param.text) for param in params])
            wrapped_paramstext = paramstext
            # If any of these paramters has been replaced by a local var, fix up the list
            params = cmd_member_dict[cmdname]
            for param in params:
                if param.islocal == True or self.StructWithExtensions(param.type):
                    if param.ispointer == True:
                        wrapped_paramstext = wrapped_paramstext.replace(param.name, '(%s %s*)local_%s' % ('const', param.type, param.name))
                    else:
                        wrapped_paramstext = wrapped_paramstext.replace(param.name, '(%s %s)local_%s' % ('const', param.type, param.name))

            # First, add check and down-chain call. Use correct dispatch table
            dispatch_table_type = "device_dispatch_table"
            if dispatchable_type in ["VkPhysicalDevice", "VkInstance"]:
                dispatch_table_type = "instance_dispatch_table"

            api_func = cmdinfo.elem.attrib.get('name').replace('vk','layer_data->%s.',1) % dispatch_table_type
            # Call to get the layer_data pointer
            self.appendSection('source_file', '    auto layer_data = GetLayerDataPtr(get_dispatch_key(%s), layer_data_map);' % dispatchable_name)
            # Put all this together for the final down-chain call
            if not down_chain_call_only:
                unwrapped_dispatch_call = api_func + '(' + paramstext + ')'
                self.appendSection('source_file', '    if (!wrap_handles) return %s;' % unwrapped_dispatch_call)

            # Handle return values, if any
            resulttype = cmdinfo.elem.find('proto/type')
            if (resulttype is not None and resulttype.text == 'void'):
              resulttype = None
            if (resulttype is not None):
                assignresult = resulttype.text + ' result = '
            else:
                assignresult = ''
            # Pre-pend declarations and pre-api-call codegen
            if api_decls:
                self.appendSection('source_file', "\n".join(str(api_decls).rstrip().split("\n")))
            if api_pre:
                self.appendSection('source_file', "\n".join(str(api_pre).rstrip().split("\n")))
            # Generate the wrapped dispatch call 
            self.appendSection('source_file', '    ' + assignresult + api_func + '(' + wrapped_paramstext + ');')

            # And add the post-API-call codegen
            self.appendSection('source_file', "\n".join(str(api_post).rstrip().split("\n")))
            # Handle the return result variable, if any
            if (resulttype is not None):
                self.appendSection('source_file', '    return result;')
            self.appendSection('source_file', '}')
            if (feature_extra_protect is not None):
                self.appendSection('source_file', '#endif // '+ feature_extra_protect)

