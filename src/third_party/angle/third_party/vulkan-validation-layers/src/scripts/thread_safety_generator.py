#!/usr/bin/python3 -i
#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2020 The Khronos Group Inc.
# Copyright (c) 2015-2020 Valve Corporation
# Copyright (c) 2015-2020 LunarG, Inc.
# Copyright (c) 2015-2020 Google Inc.
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
# Author: Mike Stroyan <stroyan@google.com>
# Author: Mark Lobodzinski <mark@lunarg.com>

import os,re,sys
from generator import *
from common_codegen import *

# ThreadGeneratorOptions - subclass of GeneratorOptions.
#
# Adds options used by ThreadOutputGenerator objects during threading
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
class ThreadGeneratorOptions(GeneratorOptions):
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
                 protectFile = True,
                 protectFeature = True,
                 apicall = '',
                 apientry = '',
                 apientryp = '',
                 indentFuncProto = True,
                 indentFuncPointer = False,
                 alignFuncParam = 0,
                 expandEnumerants = True):
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
        self.protectFile     = protectFile
        self.protectFeature  = protectFeature
        self.apicall         = apicall
        self.apientry        = apientry
        self.apientryp       = apientryp
        self.indentFuncProto = indentFuncProto
        self.indentFuncPointer = indentFuncPointer
        self.alignFuncParam  = alignFuncParam
        self.expandEnumerants = expandEnumerants


# ThreadOutputGenerator - subclass of OutputGenerator.
# Generates Thread checking framework
#
# ---- methods ----
# ThreadOutputGenerator(errFile, warnFile, diagFile) - args as for
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
class ThreadOutputGenerator(OutputGenerator):
    """Generate specified API interfaces in a specific style, such as a C header"""

    inline_copyright_message = """
// This file is ***GENERATED***.  Do Not Edit.
// See thread_safety_generator.py for modifications.

/* Copyright (c) 2015-2020 The Khronos Group Inc.
 * Copyright (c) 2015-2020 Valve Corporation
 * Copyright (c) 2015-2020 LunarG, Inc.
 * Copyright (c) 2015-2020 Google Inc.
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

 # Note that the inline_custom_header_preamble template below contains three embedded template expansion identifiers.
 # These get replaced with generated code sections, and are labeled:
 #  o COUNTER_CLASS_DEFINITIONS_TEMPLATE
 #  o COUNTER_CLASS_INSTANCES_TEMPLATE
 #  o COUNTER_CLASS_BODIES_TEMPLATE
    inline_custom_header_preamble = """
#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

VK_DEFINE_NON_DISPATCHABLE_HANDLE(DISTINCT_NONDISPATCHABLE_PHONY_HANDLE)
// The following line must match the vulkan_core.h condition guarding VK_DEFINE_NON_DISPATCHABLE_HANDLE
#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || \
    defined(_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
// If pointers are 64-bit, then there can be separate counters for each
// NONDISPATCHABLE_HANDLE type.  Otherwise they are all typedef uint64_t.
#define DISTINCT_NONDISPATCHABLE_HANDLES
// Make sure we catch any disagreement between us and the vulkan definition
static_assert(std::is_pointer<DISTINCT_NONDISPATCHABLE_PHONY_HANDLE>::value,
              "Mismatched non-dispatchable handle handle, expected pointer type.");
#else
// Make sure we catch any disagreement between us and the vulkan definition
static_assert(std::is_same<uint64_t, DISTINCT_NONDISPATCHABLE_PHONY_HANDLE>::value,
              "Mismatched non-dispatchable handle handle, expected uint64_t.");
#endif

// Suppress unused warning on Linux
#if defined(__GNUC__)
#define DECORATE_UNUSED __attribute__((unused))
#else
#define DECORATE_UNUSED
#endif

// clang-format off
static const char DECORATE_UNUSED *kVUID_Threading_Info = "UNASSIGNED-Threading-Info";
static const char DECORATE_UNUSED *kVUID_Threading_MultipleThreads = "UNASSIGNED-Threading-MultipleThreads";
static const char DECORATE_UNUSED *kVUID_Threading_SingleThreadReuse = "UNASSIGNED-Threading-SingleThreadReuse";
// clang-format on

#undef DECORATE_UNUSED

class ObjectUseData
{
public:
    class WriteReadCount
    {
    public:
        WriteReadCount(int64_t v) : count(v) {}

        int32_t GetReadCount() const { return (int32_t)(count & 0xFFFFFFFF); }
        int32_t GetWriteCount() const { return (int32_t)(count >> 32); }

    private:
        int64_t count;
    };

    ObjectUseData() : thread(0), writer_reader_count(0) {
        // silence -Wunused-private-field warning
        padding[0] = 0;
    }

    WriteReadCount AddWriter() {
        int64_t prev = writer_reader_count.fetch_add(1ULL << 32);
        return WriteReadCount(prev);
    }
    WriteReadCount AddReader() {
        int64_t prev = writer_reader_count.fetch_add(1ULL);
        return WriteReadCount(prev);
    }
    WriteReadCount RemoveWriter() {
        int64_t prev = writer_reader_count.fetch_add(-(1LL << 32));
        return WriteReadCount(prev);
    }
    WriteReadCount RemoveReader() {
        int64_t prev = writer_reader_count.fetch_add(-1LL);
        return WriteReadCount(prev);
    }
    WriteReadCount GetCount() {
        return WriteReadCount(writer_reader_count);
    }

    void WaitForObjectIdle(bool is_writer)  {
        // Wait for thread-safe access to object instead of skipping call.
        while (GetCount().GetReadCount() > (int)(!is_writer) || GetCount().GetWriteCount() > (int)is_writer) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }

    std::atomic<loader_platform_thread_id> thread;

private:
    // need to update write and read counts atomically. Writer in high
    // 32 bits, reader in low 32 bits.
    std::atomic<int64_t> writer_reader_count;

    // Put each lock on its own cache line to avoid false cache line sharing.
    char padding[(-int(sizeof(std::atomic<loader_platform_thread_id>) + sizeof(std::atomic<int64_t>))) & 63];
};


template <typename T>
class counter {
public:
    const char *typeName;
    VulkanObjectType object_type;
    ValidationObject *object_data;

    vl_concurrent_unordered_map<T, std::shared_ptr<ObjectUseData>, 6> object_table;

    void CreateObject(T object) {
        object_table.insert(object, std::make_shared<ObjectUseData>());
    }

    void DestroyObject(T object) {
        if (object) {
            object_table.erase(object);
        }
    }

    std::shared_ptr<ObjectUseData> FindObject(T object) {
        assert(object_table.contains(object));
        auto iter = std::move(object_table.find(object));
        if (iter != object_table.end()) {
            return std::move(iter->second);
        } else {
            object_data->LogError(object, kVUID_Threading_Info,
                    "Couldn't find %s Object 0x%" PRIxLEAST64
                    ". This should not happen and may indicate a bug in the application.",
                    object_string[object_type], (uint64_t)(object));
            return nullptr;
        }
    }

    void StartWrite(T object, const char *api_name) {
        if (object == VK_NULL_HANDLE) {
            return;
        }
        bool skip = false;
        loader_platform_thread_id tid = loader_platform_get_thread_id();

        auto use_data = FindObject(object);
        if (!use_data) {
            return;
        }
        const ObjectUseData::WriteReadCount prevCount = use_data->AddWriter();

        if (prevCount.GetReadCount() == 0 && prevCount.GetWriteCount() == 0) {
            // There is no current use of the object.  Record writer thread.
            use_data->thread = tid;
        } else {
            if (prevCount.GetReadCount() == 0) {
                assert(prevCount.GetWriteCount() != 0);
                // There are no readers.  Two writers just collided.
                if (use_data->thread != tid) {
                    skip |= object_data->LogError(object, kVUID_Threading_MultipleThreads,
                        "THREADING ERROR : %s(): object of type %s is simultaneously used in "
                        "thread 0x%" PRIx64 " and thread 0x%" PRIx64, api_name,
                        typeName, (uint64_t)use_data->thread.load(std::memory_order_relaxed), (uint64_t)tid);
                    if (skip) {
                        // Wait for thread-safe access to object instead of skipping call.
                        use_data->WaitForObjectIdle(true);
                        // There is now no current use of the object.  Record writer thread.
                        use_data->thread = tid;
                    } else {
                        // There is now no current use of the object.  Record writer thread.
                        use_data->thread = tid;
                    }
                } else {
                    // This is either safe multiple use in one call, or recursive use.
                    // There is no way to make recursion safe.  Just forge ahead.
                }
            } else {
                // There are readers.  This writer collided with them.
                if (use_data->thread != tid) {
                    skip |= object_data->LogError(object, kVUID_Threading_MultipleThreads,
                        "THREADING ERROR : %s(): object of type %s is simultaneously used in "
                        "thread 0x%" PRIx64 " and thread 0x%" PRIx64, api_name,
                        typeName, (uint64_t)use_data->thread.load(std::memory_order_relaxed), (uint64_t)tid);
                    if (skip) {
                        // Wait for thread-safe access to object instead of skipping call.
                        use_data->WaitForObjectIdle(true);
                        // There is now no current use of the object.  Record writer thread.
                        use_data->thread = tid;
                    } else {
                        // Continue with an unsafe use of the object.
                        use_data->thread = tid;
                    }
                } else {
                    // This is either safe multiple use in one call, or recursive use.
                    // There is no way to make recursion safe.  Just forge ahead.
                }
            }
        }
    }

    void FinishWrite(T object, const char *api_name) {
        if (object == VK_NULL_HANDLE) {
            return;
        }
        // Object is no longer in use
        auto use_data = FindObject(object);
        if (!use_data) {
            return;
        }
        use_data->RemoveWriter();
    }

    void StartRead(T object, const char *api_name) {
        if (object == VK_NULL_HANDLE) {
            return;
        }
        bool skip = false;
        loader_platform_thread_id tid = loader_platform_get_thread_id();

        auto use_data = FindObject(object);
        if (!use_data) {
            return;
        }
        const ObjectUseData::WriteReadCount prevCount = use_data->AddReader();

        if (prevCount.GetReadCount() == 0 && prevCount.GetWriteCount() == 0) {
            // There is no current use of the object.
            use_data->thread = tid;
        } else if (prevCount.GetWriteCount() > 0 && use_data->thread != tid) {
            // There is a writer of the object.
            skip |= object_data->LogError(object, kVUID_Threading_MultipleThreads,
                "THREADING ERROR : %s(): object of type %s is simultaneously used in "
                "thread 0x%" PRIx64 " and thread 0x%" PRIx64, api_name,
                typeName, (uint64_t)use_data->thread.load(std::memory_order_relaxed), (uint64_t)tid);
            if (skip) {
                // Wait for thread-safe access to object instead of skipping call.
                use_data->WaitForObjectIdle(false);
                use_data->thread = tid;
            }
        } else {
            // There are other readers of the object.
        }
    }
    void FinishRead(T object, const char *api_name) {
        if (object == VK_NULL_HANDLE) {
            return;
        }

        auto use_data = FindObject(object);
        if (!use_data) {
            return;
        }
        use_data->RemoveReader();
    }
    counter(const char *name = "", VulkanObjectType type = kVulkanObjectTypeUnknown, ValidationObject *val_obj = nullptr) {
            typeName = name;
        object_type = type;
        object_data = val_obj;
    }

private:
};

class ThreadSafety : public ValidationObject {
public:

    ReadWriteLock thread_safety_lock;

    // Override chassis read/write locks for this validation object
    // This override takes a deferred lock. i.e. it is not acquired.
    virtual read_lock_guard_t read_lock() {
        return read_lock_guard_t(validation_object_mutex, std::defer_lock);
    }
    virtual write_lock_guard_t write_lock() {
        return write_lock_guard_t(validation_object_mutex, std::defer_lock);
    }

    // If this ThreadSafety is for a VkDevice, then parent_instance points to the
    // ThreadSafety object of its parent VkInstance. This is used to get to the counters
    // for objects created with the instance as parent.
    ThreadSafety *parent_instance;

    vl_concurrent_unordered_map<VkCommandBuffer, VkCommandPool, 6> command_pool_map;
    std::unordered_map<VkCommandPool, std::unordered_set<VkCommandBuffer>> pool_command_buffers_map;
    std::unordered_map<VkDevice, std::unordered_set<VkQueue>> device_queues_map;

    // Track per-descriptorsetlayout and per-descriptorset whether UPDATE_AFTER_BIND is used.
    // This is used to (sloppily) implement the relaxed externsync rules for UPDATE_AFTER_BIND
    // descriptors. We model updates of UPDATE_AFTER_BIND descriptors as if they were reads
    // rather than writes, because they only conflict with the set being freed or reset.
    //
    // We don't track the UPDATE_AFTER_BIND state per-binding for a couple reasons:
    // (1) We only have one counter per object, and if we treated non-UAB as writes
    //     and UAB as reads then they'd appear to conflict with each other.
    // (2) Avoid additional tracking of descriptor binding state in the descriptor set
    //     layout, and tracking of which bindings are accessed by a VkDescriptorUpdateTemplate.
    vl_concurrent_unordered_map<VkDescriptorSetLayout, bool, 4> dsl_update_after_bind_map;
    vl_concurrent_unordered_map<VkDescriptorSet, bool, 6> ds_update_after_bind_map;
    bool DsUpdateAfterBind(VkDescriptorSet) const;

    counter<VkCommandBuffer> c_VkCommandBuffer;
    counter<VkDevice> c_VkDevice;
    counter<VkInstance> c_VkInstance;
    counter<VkQueue> c_VkQueue;
#ifdef DISTINCT_NONDISPATCHABLE_HANDLES

    // Special entry to allow tracking of command pool Reset and Destroy
    counter<VkCommandPool> c_VkCommandPoolContents;
COUNTER_CLASS_DEFINITIONS_TEMPLATE

#else   // DISTINCT_NONDISPATCHABLE_HANDLES
    // Special entry to allow tracking of command pool Reset and Destroy
    counter<uint64_t> c_VkCommandPoolContents;

    counter<uint64_t> c_uint64_t;
#endif  // DISTINCT_NONDISPATCHABLE_HANDLES

    ThreadSafety(ThreadSafety *parent)
        : parent_instance(parent),
          c_VkCommandBuffer("VkCommandBuffer", kVulkanObjectTypeCommandBuffer, this),
          c_VkDevice("VkDevice", kVulkanObjectTypeDevice, this),
          c_VkInstance("VkInstance", kVulkanObjectTypeInstance, this),
          c_VkQueue("VkQueue", kVulkanObjectTypeQueue, this),
          c_VkCommandPoolContents("VkCommandPool", kVulkanObjectTypeCommandPool, this),

#ifdef DISTINCT_NONDISPATCHABLE_HANDLES
COUNTER_CLASS_INSTANCES_TEMPLATE


#else   // DISTINCT_NONDISPATCHABLE_HANDLES
          c_uint64_t("NON_DISPATCHABLE_HANDLE", kVulkanObjectTypeUnknown, this)
#endif  // DISTINCT_NONDISPATCHABLE_HANDLES
    {
        container_type = LayerObjectTypeThreading;
    };

#define WRAPPER(type)                                                \\
    void StartWriteObject(type object, const char *api_name) {       \\
        c_##type.StartWrite(object, api_name);                       \\
    }                                                                \\
    void FinishWriteObject(type object, const char *api_name) {      \\
        c_##type.FinishWrite(object, api_name);                      \\
    }                                                                \\
    void StartReadObject(type object, const char *api_name) {        \\
        c_##type.StartRead(object, api_name);                        \\
    }                                                                \\
    void FinishReadObject(type object, const char *api_name) {       \\
        c_##type.FinishRead(object, api_name);                       \\
    }                                                                \\
    void CreateObject(type object) {                                 \\
        c_##type.CreateObject(object);                               \\
    }                                                                \\
    void DestroyObject(type object) {                                \\
        c_##type.DestroyObject(object);                              \\
        c_##type.DestroyObject(object);                              \\
    }

#define WRAPPER_PARENT_INSTANCE(type)                                                           \\
    void StartWriteObjectParentInstance(type object, const char *api_name) {                    \\
        (parent_instance ? parent_instance : this)->c_##type.StartWrite(object, api_name);      \\
    }                                                                                           \\
    void FinishWriteObjectParentInstance(type object, const char *api_name) {                   \\
        (parent_instance ? parent_instance : this)->c_##type.FinishWrite(object, api_name);     \\
    }                                                                                           \\
    void StartReadObjectParentInstance(type object, const char *api_name) {                     \\
        (parent_instance ? parent_instance : this)->c_##type.StartRead(object, api_name);       \\
    }                                                                                           \\
    void FinishReadObjectParentInstance(type object, const char *api_name) {                    \\
        (parent_instance ? parent_instance : this)->c_##type.FinishRead(object, api_name);      \\
    }                                                                                           \\
    void CreateObjectParentInstance(type object) {                                              \\
        (parent_instance ? parent_instance : this)->c_##type.CreateObject(object);              \\
    }                                                                                           \\
    void DestroyObjectParentInstance(type object) {                                             \\
        (parent_instance ? parent_instance : this)->c_##type.DestroyObject(object);             \\
    }

WRAPPER_PARENT_INSTANCE(VkDevice)
WRAPPER_PARENT_INSTANCE(VkInstance)
WRAPPER(VkQueue)
#ifdef DISTINCT_NONDISPATCHABLE_HANDLES
COUNTER_CLASS_BODIES_TEMPLATE

#else   // DISTINCT_NONDISPATCHABLE_HANDLES
WRAPPER(uint64_t)
WRAPPER_PARENT_INSTANCE(uint64_t)
#endif  // DISTINCT_NONDISPATCHABLE_HANDLES

    void CreateObject(VkCommandBuffer object) {
        c_VkCommandBuffer.CreateObject(object);
    }
    void DestroyObject(VkCommandBuffer object) {
        c_VkCommandBuffer.DestroyObject(object);
    }

    // VkCommandBuffer needs check for implicit use of command pool
    void StartWriteObject(VkCommandBuffer object, const char *api_name, bool lockPool = true) {
        if (lockPool) {
            auto iter = command_pool_map.find(object);
            if (iter != command_pool_map.end()) {
                VkCommandPool pool = iter->second;
                StartWriteObject(pool, api_name);
            }
        }
        c_VkCommandBuffer.StartWrite(object, api_name);
    }
    void FinishWriteObject(VkCommandBuffer object, const char *api_name, bool lockPool = true) {
        c_VkCommandBuffer.FinishWrite(object, api_name);
        if (lockPool) {
            auto iter = command_pool_map.find(object);
            if (iter != command_pool_map.end()) {
                VkCommandPool pool = iter->second;
                FinishWriteObject(pool, api_name);
            }
        }
    }
    void StartReadObject(VkCommandBuffer object, const char *api_name) {
        auto iter = command_pool_map.find(object);
        if (iter != command_pool_map.end()) {
            VkCommandPool pool = iter->second;
            // We set up a read guard against the "Contents" counter to catch conflict vs. vkResetCommandPool and vkDestroyCommandPool
            // while *not* establishing a read guard against the command pool counter itself to avoid false postives for
            // non-externally sync'd command buffers
            c_VkCommandPoolContents.StartRead(pool, api_name);
        }
        c_VkCommandBuffer.StartRead(object, api_name);
    }
    void FinishReadObject(VkCommandBuffer object, const char *api_name) {
        c_VkCommandBuffer.FinishRead(object, api_name);
        auto iter = command_pool_map.find(object);
        if (iter != command_pool_map.end()) {
            VkCommandPool pool = iter->second;
            c_VkCommandPoolContents.FinishRead(pool, api_name);
        }
    }

void PostCallRecordGetPhysicalDeviceDisplayPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPropertiesKHR*                     pProperties,
    VkResult                                    result);

void PostCallRecordGetPhysicalDeviceDisplayProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayProperties2KHR*                    pProperties,
    VkResult                                    result);

void PreCallRecordGetDisplayPlaneCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkDisplayPlaneInfo2KHR*               pDisplayPlaneInfo,
    VkDisplayPlaneCapabilities2KHR*             pCapabilities);

void PostCallRecordGetDisplayPlaneCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkDisplayPlaneInfo2KHR*               pDisplayPlaneInfo,
    VkDisplayPlaneCapabilities2KHR*             pCapabilities,
    VkResult                                    result);

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT

void PostCallRecordGetRandROutputDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    Display*                                    dpy,
    RROutput                                    rrOutput,
    VkDisplayKHR*                               pDisplay,
    VkResult                                    result);

#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT"""


    inline_custom_source_preamble = """
void ThreadSafety::PreCallRecordAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                                                       VkCommandBuffer *pCommandBuffers) {
    StartReadObjectParentInstance(device, "vkAllocateCommandBuffers");
    StartWriteObject(pAllocateInfo->commandPool, "vkAllocateCommandBuffers");
}

void ThreadSafety::PostCallRecordAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                                                        VkCommandBuffer *pCommandBuffers, VkResult result) {
    FinishReadObjectParentInstance(device, "vkAllocateCommandBuffers");
    FinishWriteObject(pAllocateInfo->commandPool, "vkAllocateCommandBuffers");

    // Record mapping from command buffer to command pool
    if(pCommandBuffers) {
        auto lock = write_lock_guard_t(thread_safety_lock);
        auto &pool_command_buffers = pool_command_buffers_map[pAllocateInfo->commandPool];
        for (uint32_t index = 0; index < pAllocateInfo->commandBufferCount; index++) {
            command_pool_map.insert_or_assign(pCommandBuffers[index], pAllocateInfo->commandPool);
            CreateObject(pCommandBuffers[index]);
            pool_command_buffers.insert(pCommandBuffers[index]);
        }
    }
}


void ThreadSafety::PreCallRecordCreateDescriptorSetLayout(
    VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout) {
    StartReadObjectParentInstance(device, "vkCreateDescriptorSetLayout");
}

void ThreadSafety::PostCallRecordCreateDescriptorSetLayout(
    VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout,
    VkResult                                    result) {
    FinishReadObjectParentInstance(device, "vkCreateDescriptorSetLayout");
    if (result == VK_SUCCESS) {
        CreateObject(*pSetLayout);

        // Check whether any binding uses UPDATE_AFTER_BIND
        bool update_after_bind = false;
        const auto *flags_create_info = lvl_find_in_chain<VkDescriptorSetLayoutBindingFlagsCreateInfoEXT>(pCreateInfo->pNext);
        if (flags_create_info) {
            for (uint32_t i = 0; i < flags_create_info->bindingCount; ++i) {
                if (flags_create_info->pBindingFlags[i] & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT) {
                    update_after_bind = true;
                    break;
                }
            }
        }
        dsl_update_after_bind_map.insert_or_assign(*pSetLayout, update_after_bind);
    }
}

void ThreadSafety::PreCallRecordAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                                       VkDescriptorSet *pDescriptorSets) {
    StartReadObjectParentInstance(device, "vkAllocateDescriptorSets");
    StartWriteObject(pAllocateInfo->descriptorPool, "vkAllocateDescriptorSets");
    // Host access to pAllocateInfo::descriptorPool must be externally synchronized
}

void ThreadSafety::PostCallRecordAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                                        VkDescriptorSet *pDescriptorSets, VkResult result) {
    FinishReadObjectParentInstance(device, "vkAllocateDescriptorSets");
    FinishWriteObject(pAllocateInfo->descriptorPool, "vkAllocateDescriptorSets");
    // Host access to pAllocateInfo::descriptorPool must be externally synchronized
    if (VK_SUCCESS == result) {
        auto lock = write_lock_guard_t(thread_safety_lock);
        auto &pool_descriptor_sets = pool_descriptor_sets_map[pAllocateInfo->descriptorPool];
        for (uint32_t index0 = 0; index0 < pAllocateInfo->descriptorSetCount; index0++) {
            CreateObject(pDescriptorSets[index0]);
            pool_descriptor_sets.insert(pDescriptorSets[index0]);

            auto iter = dsl_update_after_bind_map.find(pAllocateInfo->pSetLayouts[index0]);
            if (iter != dsl_update_after_bind_map.end()) {
                ds_update_after_bind_map.insert_or_assign(pDescriptorSets[index0], iter->second);
            } else {
                assert(0 && "descriptor set layout not found");
            }
        }
    }
}

void ThreadSafety::PreCallRecordFreeDescriptorSets(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets) {
    StartReadObjectParentInstance(device, "vkFreeDescriptorSets");
    StartWriteObject(descriptorPool, "vkFreeDescriptorSets");
    if (pDescriptorSets) {
        for (uint32_t index=0; index < descriptorSetCount; index++) {
            StartWriteObject(pDescriptorSets[index], "vkFreeDescriptorSets");
        }
    }
    // Host access to descriptorPool must be externally synchronized
    // Host access to each member of pDescriptorSets must be externally synchronized
}

void ThreadSafety::PostCallRecordFreeDescriptorSets(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    VkResult                                    result) {
    FinishReadObjectParentInstance(device, "vkFreeDescriptorSets");
    FinishWriteObject(descriptorPool, "vkFreeDescriptorSets");
    if (pDescriptorSets) {
        for (uint32_t index=0; index < descriptorSetCount; index++) {
            FinishWriteObject(pDescriptorSets[index], "vkFreeDescriptorSets");
        }
    }
    // Host access to descriptorPool must be externally synchronized
    // Host access to each member of pDescriptorSets must be externally synchronized
    // Host access to pAllocateInfo::descriptorPool must be externally synchronized
    if (VK_SUCCESS == result) {
        auto lock = write_lock_guard_t(thread_safety_lock);
        auto &pool_descriptor_sets = pool_descriptor_sets_map[descriptorPool];
        for (uint32_t index0 = 0; index0 < descriptorSetCount; index0++) {
            DestroyObject(pDescriptorSets[index0]);
            pool_descriptor_sets.erase(pDescriptorSets[index0]);
        }
    }
}

void ThreadSafety::PreCallRecordDestroyDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator) {
    StartReadObjectParentInstance(device, "vkDestroyDescriptorPool");
    StartWriteObject(descriptorPool, "vkDestroyDescriptorPool");
    // Host access to descriptorPool must be externally synchronized
    auto lock = read_lock_guard_t(thread_safety_lock);
    auto iterator = pool_descriptor_sets_map.find(descriptorPool);
    // Possible to have no descriptor sets allocated from pool
    if (iterator != pool_descriptor_sets_map.end()) {
        for(auto descriptor_set : pool_descriptor_sets_map[descriptorPool]) {
            StartWriteObject(descriptor_set, "vkDestroyDescriptorPool");
        }
    }
}

void ThreadSafety::PostCallRecordDestroyDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator) {
    FinishReadObjectParentInstance(device, "vkDestroyDescriptorPool");
    FinishWriteObject(descriptorPool, "vkDestroyDescriptorPool");
    DestroyObject(descriptorPool);
    // Host access to descriptorPool must be externally synchronized
    {
        auto lock = write_lock_guard_t(thread_safety_lock);
        // remove references to implicitly freed descriptor sets
        for(auto descriptor_set : pool_descriptor_sets_map[descriptorPool]) {
            FinishWriteObject(descriptor_set, "vkDestroyDescriptorPool");
            DestroyObject(descriptor_set);
        }
        pool_descriptor_sets_map[descriptorPool].clear();
        pool_descriptor_sets_map.erase(descriptorPool);
    }
}

void ThreadSafety::PreCallRecordResetDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags) {
    StartReadObjectParentInstance(device, "vkResetDescriptorPool");
    StartWriteObject(descriptorPool, "vkResetDescriptorPool");
    // Host access to descriptorPool must be externally synchronized
    // any sname:VkDescriptorSet objects allocated from pname:descriptorPool must be externally synchronized between host accesses
    auto lock = read_lock_guard_t(thread_safety_lock);
    auto iterator = pool_descriptor_sets_map.find(descriptorPool);
    // Possible to have no descriptor sets allocated from pool
    if (iterator != pool_descriptor_sets_map.end()) {
        for(auto descriptor_set : pool_descriptor_sets_map[descriptorPool]) {
            StartWriteObject(descriptor_set, "vkResetDescriptorPool");
        }
    }
}

void ThreadSafety::PostCallRecordResetDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags,
    VkResult                                    result) {
    FinishReadObjectParentInstance(device, "vkResetDescriptorPool");
    FinishWriteObject(descriptorPool, "vkResetDescriptorPool");
    // Host access to descriptorPool must be externally synchronized
    // any sname:VkDescriptorSet objects allocated from pname:descriptorPool must be externally synchronized between host accesses
    if (VK_SUCCESS == result) {
        // remove references to implicitly freed descriptor sets
        auto lock = write_lock_guard_t(thread_safety_lock);
        for(auto descriptor_set : pool_descriptor_sets_map[descriptorPool]) {
            FinishWriteObject(descriptor_set, "vkResetDescriptorPool");
            DestroyObject(descriptor_set);
        }
        pool_descriptor_sets_map[descriptorPool].clear();
    }
}

bool ThreadSafety::DsUpdateAfterBind(VkDescriptorSet set) const
{
    auto iter = ds_update_after_bind_map.find(set);
    if (iter != ds_update_after_bind_map.end()) {
        return iter->second;
    }
    return false;
}

void ThreadSafety::PreCallRecordUpdateDescriptorSets(
    VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies) {
    StartReadObjectParentInstance(device, "vkUpdateDescriptorSets");
    if (pDescriptorWrites) {
        for (uint32_t index=0; index < descriptorWriteCount; index++) {
            auto dstSet = pDescriptorWrites[index].dstSet;
            bool update_after_bind = DsUpdateAfterBind(dstSet);
            if (update_after_bind) {
                StartReadObject(dstSet, "vkUpdateDescriptorSets");
            } else {
                StartWriteObject(dstSet, "vkUpdateDescriptorSets");
            }
        }
    }
    if (pDescriptorCopies) {
        for (uint32_t index=0; index < descriptorCopyCount; index++) {
            auto dstSet = pDescriptorCopies[index].dstSet;
            bool update_after_bind = DsUpdateAfterBind(dstSet);
            if (update_after_bind) {
                StartReadObject(dstSet, "vkUpdateDescriptorSets");
            } else {
                StartWriteObject(dstSet, "vkUpdateDescriptorSets");
            }
            StartReadObject(pDescriptorCopies[index].srcSet, "vkUpdateDescriptorSets");
        }
    }
    // Host access to pDescriptorWrites[].dstSet must be externally synchronized
    // Host access to pDescriptorCopies[].dstSet must be externally synchronized
}

void ThreadSafety::PostCallRecordUpdateDescriptorSets(
    VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies) {
    FinishReadObjectParentInstance(device, "vkUpdateDescriptorSets");
    if (pDescriptorWrites) {
        for (uint32_t index=0; index < descriptorWriteCount; index++) {
            auto dstSet = pDescriptorWrites[index].dstSet;
            bool update_after_bind = DsUpdateAfterBind(dstSet);
            if (update_after_bind) {
                FinishReadObject(dstSet, "vkUpdateDescriptorSets");
            } else {
                FinishWriteObject(dstSet, "vkUpdateDescriptorSets");
            }
        }
    }
    if (pDescriptorCopies) {
        for (uint32_t index=0; index < descriptorCopyCount; index++) {
            auto dstSet = pDescriptorCopies[index].dstSet;
            bool update_after_bind = DsUpdateAfterBind(dstSet);
            if (update_after_bind) {
                FinishReadObject(dstSet, "vkUpdateDescriptorSets");
            } else {
                FinishWriteObject(dstSet, "vkUpdateDescriptorSets");
            }
            FinishReadObject(pDescriptorCopies[index].srcSet, "vkUpdateDescriptorSets");
        }
    }
    // Host access to pDescriptorWrites[].dstSet must be externally synchronized
    // Host access to pDescriptorCopies[].dstSet must be externally synchronized
}

void ThreadSafety::PreCallRecordUpdateDescriptorSetWithTemplate(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const void*                                 pData) {
    StartReadObjectParentInstance(device, "vkUpdateDescriptorSetWithTemplate");
    StartReadObject(descriptorUpdateTemplate, "vkUpdateDescriptorSetWithTemplate");

    bool update_after_bind = DsUpdateAfterBind(descriptorSet);
    if (update_after_bind) {
        StartReadObject(descriptorSet, "vkUpdateDescriptorSetWithTemplate");
    } else {
        StartWriteObject(descriptorSet, "vkUpdateDescriptorSetWithTemplate");
    }
    // Host access to descriptorSet must be externally synchronized
}

void ThreadSafety::PostCallRecordUpdateDescriptorSetWithTemplate(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const void*                                 pData) {
    FinishReadObjectParentInstance(device, "vkUpdateDescriptorSetWithTemplate");
    FinishReadObject(descriptorUpdateTemplate, "vkUpdateDescriptorSetWithTemplate");

    bool update_after_bind = DsUpdateAfterBind(descriptorSet);
    if (update_after_bind) {
        FinishReadObject(descriptorSet, "vkUpdateDescriptorSetWithTemplate");
    } else {
        FinishWriteObject(descriptorSet, "vkUpdateDescriptorSetWithTemplate");
    }
    // Host access to descriptorSet must be externally synchronized
}

void ThreadSafety::PreCallRecordUpdateDescriptorSetWithTemplateKHR(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const void*                                 pData) {
    StartReadObjectParentInstance(device, "vkUpdateDescriptorSetWithTemplateKHR");
    StartReadObject(descriptorUpdateTemplate, "vkUpdateDescriptorSetWithTemplateKHR");

    bool update_after_bind = DsUpdateAfterBind(descriptorSet);
    if (update_after_bind) {
        StartReadObject(descriptorSet, "vkUpdateDescriptorSetWithTemplateKHR");
    } else {
        StartWriteObject(descriptorSet, "vkUpdateDescriptorSetWithTemplateKHR");
    }
    // Host access to descriptorSet must be externally synchronized
}

void ThreadSafety::PostCallRecordUpdateDescriptorSetWithTemplateKHR(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const void*                                 pData) {
    FinishReadObjectParentInstance(device, "vkUpdateDescriptorSetWithTemplateKHR");
    FinishReadObject(descriptorUpdateTemplate, "vkUpdateDescriptorSetWithTemplateKHR");

    bool update_after_bind = DsUpdateAfterBind(descriptorSet);
    if (update_after_bind) {
        FinishReadObject(descriptorSet, "vkUpdateDescriptorSetWithTemplateKHR");
    } else {
        FinishWriteObject(descriptorSet, "vkUpdateDescriptorSetWithTemplateKHR");
    }
    // Host access to descriptorSet must be externally synchronized
}

void ThreadSafety::PreCallRecordFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
                                                   const VkCommandBuffer *pCommandBuffers) {
    const bool lockCommandPool = false;  // pool is already directly locked
    StartReadObjectParentInstance(device, "vkFreeCommandBuffers");
    StartWriteObject(commandPool, "vkFreeCommandBuffers");
    if(pCommandBuffers) {
        // Even though we're immediately "finishing" below, we still are testing for concurrency with any call in process
        // so this isn't a no-op
        // The driver may immediately reuse command buffers in another thread.
        // These updates need to be done before calling down to the driver.
        auto lock = write_lock_guard_t(thread_safety_lock);
        auto &pool_command_buffers = pool_command_buffers_map[commandPool];
        for (uint32_t index = 0; index < commandBufferCount; index++) {
            StartWriteObject(pCommandBuffers[index], "vkFreeCommandBuffers", lockCommandPool);
            FinishWriteObject(pCommandBuffers[index], "vkFreeCommandBuffers", lockCommandPool);
            DestroyObject(pCommandBuffers[index]);
            pool_command_buffers.erase(pCommandBuffers[index]);
            command_pool_map.erase(pCommandBuffers[index]);
        }
    }
}

void ThreadSafety::PostCallRecordFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
                                                    const VkCommandBuffer *pCommandBuffers) {
    FinishReadObjectParentInstance(device, "vkFreeCommandBuffers");
    FinishWriteObject(commandPool, "vkFreeCommandBuffers");
}

void ThreadSafety::PreCallRecordCreateCommandPool(
    VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool) {
    StartReadObjectParentInstance(device, "vkCreateCommandPool");
}

void ThreadSafety::PostCallRecordCreateCommandPool(
    VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool,
    VkResult                                    result) {
    FinishReadObjectParentInstance(device, "vkCreateCommandPool");
    if (result == VK_SUCCESS) {
        CreateObject(*pCommandPool);
        c_VkCommandPoolContents.CreateObject(*pCommandPool);
    }
}

void ThreadSafety::PreCallRecordResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
    StartReadObjectParentInstance(device, "vkResetCommandPool");
    StartWriteObject(commandPool, "vkResetCommandPool");
    // Check for any uses of non-externally sync'd command buffers (for example from vkCmdExecuteCommands)
    c_VkCommandPoolContents.StartWrite(commandPool, "vkResetCommandPool");
    // Host access to commandPool must be externally synchronized
}

void ThreadSafety::PostCallRecordResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags, VkResult result) {
    FinishReadObjectParentInstance(device, "vkResetCommandPool");
    FinishWriteObject(commandPool, "vkResetCommandPool");
    c_VkCommandPoolContents.FinishWrite(commandPool, "vkResetCommandPool");
    // Host access to commandPool must be externally synchronized
}

void ThreadSafety::PreCallRecordDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator) {
    StartReadObjectParentInstance(device, "vkDestroyCommandPool");
    StartWriteObject(commandPool, "vkDestroyCommandPool");
    // Check for any uses of non-externally sync'd command buffers (for example from vkCmdExecuteCommands)
    c_VkCommandPoolContents.StartWrite(commandPool, "vkDestroyCommandPool");
    // Host access to commandPool must be externally synchronized

    auto lock = write_lock_guard_t(thread_safety_lock);
    // The driver may immediately reuse command buffers in another thread.
    // These updates need to be done before calling down to the driver.
    // remove references to implicitly freed command pools
    for(auto command_buffer : pool_command_buffers_map[commandPool]) {
        DestroyObject(command_buffer);
    }
    pool_command_buffers_map[commandPool].clear();
    pool_command_buffers_map.erase(commandPool);
}

void ThreadSafety::PostCallRecordDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator) {
    FinishReadObjectParentInstance(device, "vkDestroyCommandPool");
    FinishWriteObject(commandPool, "vkDestroyCommandPool");
    DestroyObject(commandPool);
    c_VkCommandPoolContents.FinishWrite(commandPool, "vkDestroyCommandPool");
    c_VkCommandPoolContents.DestroyObject(commandPool);
}

// GetSwapchainImages can return a non-zero count with a NULL pSwapchainImages pointer.  Let's avoid crashes by ignoring
// pSwapchainImages.
void ThreadSafety::PreCallRecordGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
                                                      VkImage *pSwapchainImages) {
    StartReadObjectParentInstance(device, "vkGetSwapchainImagesKHR");
    StartReadObject(swapchain, "vkGetSwapchainImagesKHR");
}

void ThreadSafety::PostCallRecordGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
                                                       VkImage *pSwapchainImages, VkResult result) {
    FinishReadObjectParentInstance(device, "vkGetSwapchainImagesKHR");
    FinishReadObject(swapchain, "vkGetSwapchainImagesKHR");
    if (pSwapchainImages != NULL) {
        auto lock = write_lock_guard_t(thread_safety_lock);
        auto &wrapped_swapchain_image_handles = swapchain_wrapped_image_handle_map[swapchain];
        for (uint32_t i = static_cast<uint32_t>(wrapped_swapchain_image_handles.size()); i < *pSwapchainImageCount; i++) {
            CreateObject(pSwapchainImages[i]);
            wrapped_swapchain_image_handles.emplace_back(pSwapchainImages[i]);
        }
    }
}

void ThreadSafety::PreCallRecordDestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator) {
    StartReadObjectParentInstance(device, "vkDestroySwapchainKHR");
    StartWriteObject(swapchain, "vkDestroySwapchainKHR");
    // Host access to swapchain must be externally synchronized
    auto lock = read_lock_guard_t(thread_safety_lock);
    for (auto &image_handle : swapchain_wrapped_image_handle_map[swapchain]) {
        StartWriteObject(image_handle, "vkDestroySwapchainKHR");
    }
}

void ThreadSafety::PostCallRecordDestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator) {
    FinishReadObjectParentInstance(device, "vkDestroySwapchainKHR");
    FinishWriteObject(swapchain, "vkDestroySwapchainKHR");
    DestroyObject(swapchain);
    // Host access to swapchain must be externally synchronized
    auto lock = write_lock_guard_t(thread_safety_lock);
    for (auto &image_handle : swapchain_wrapped_image_handle_map[swapchain]) {
        FinishWriteObject(image_handle, "vkDestroySwapchainKHR");
        DestroyObject(image_handle);
    }
    swapchain_wrapped_image_handle_map.erase(swapchain);
}

void ThreadSafety::PreCallRecordDestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator) {
    StartWriteObjectParentInstance(device, "vkDestroyDevice");
    // Host access to device must be externally synchronized
}

void ThreadSafety::PostCallRecordDestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator) {
    FinishWriteObjectParentInstance(device, "vkDestroyDevice");
    DestroyObjectParentInstance(device);
    // Host access to device must be externally synchronized
    auto lock = write_lock_guard_t(thread_safety_lock);
    for (auto &queue : device_queues_map[device]) {
        DestroyObject(queue);
    }
    device_queues_map[device].clear();
}

void ThreadSafety::PreCallRecordGetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue) {
    StartReadObjectParentInstance(device, "vkGetDeviceQueue");
}

void ThreadSafety::PostCallRecordGetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue) {
    FinishReadObjectParentInstance(device, "vkGetDeviceQueue");
    CreateObject(*pQueue);
    auto lock = write_lock_guard_t(thread_safety_lock);
    device_queues_map[device].insert(*pQueue);
}

void ThreadSafety::PreCallRecordGetDeviceQueue2(
    VkDevice                                    device,
    const VkDeviceQueueInfo2*                   pQueueInfo,
    VkQueue*                                    pQueue) {
    StartReadObjectParentInstance(device, "vkGetDeviceQueue2");
}

void ThreadSafety::PostCallRecordGetDeviceQueue2(
    VkDevice                                    device,
    const VkDeviceQueueInfo2*                   pQueueInfo,
    VkQueue*                                    pQueue) {
    FinishReadObjectParentInstance(device, "vkGetDeviceQueue2");
    CreateObject(*pQueue);
    auto lock = write_lock_guard_t(thread_safety_lock);
    device_queues_map[device].insert(*pQueue);
}

void ThreadSafety::PostCallRecordGetPhysicalDeviceDisplayPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPropertiesKHR*                     pProperties,
    VkResult                                    result) {
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) return;
    if (pProperties) {
        for (uint32_t i = 0; i < *pPropertyCount; ++i) {
            CreateObject(pProperties[i].display);
        }
    }
}

void ThreadSafety::PostCallRecordGetPhysicalDeviceDisplayProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayProperties2KHR*                    pProperties,
    VkResult                                    result) {
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) return;
    if (pProperties) {
        for (uint32_t i = 0; i < *pPropertyCount; ++i) {
            CreateObject(pProperties[i].displayProperties.display);
        }
    }
}

void ThreadSafety::PreCallRecordGetDisplayPlaneSupportedDisplaysKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    planeIndex,
    uint32_t*                                   pDisplayCount,
    VkDisplayKHR*                               pDisplays) {
    // Nothing to do for this pre-call function
}

void ThreadSafety::PostCallRecordGetDisplayPlaneSupportedDisplaysKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    planeIndex,
    uint32_t*                                   pDisplayCount,
    VkDisplayKHR*                               pDisplays,
    VkResult                                    result) {
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) return;
    if (pDisplays) {
        for (uint32_t index = 0; index < *pDisplayCount; index++) {
            CreateObject(pDisplays[index]);
        }
    }
}

void ThreadSafety::PreCallRecordGetDisplayModePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModePropertiesKHR*                 pProperties) {
    StartReadObject(display, "vkGetDisplayModePropertiesKHR");
}

void ThreadSafety::PostCallRecordGetDisplayModePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModePropertiesKHR*                 pProperties,
    VkResult                                    result) {
    FinishReadObject(display, "vkGetDisplayModePropertiesKHR");
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) return;
    if (pProperties != nullptr) {
        for (uint32_t index = 0; index < *pPropertyCount; index++) {
            CreateObject(pProperties[index].displayMode);
        }
    }
}

void ThreadSafety::PreCallRecordGetDisplayModeProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModeProperties2KHR*                pProperties) {
    StartReadObject(display, "vkGetDisplayModeProperties2KHR");
}

void ThreadSafety::PostCallRecordGetDisplayModeProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModeProperties2KHR*                pProperties,
    VkResult                                    result) {
    FinishReadObject(display, "vkGetDisplayModeProperties2KHR");
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) return;
    if (pProperties != nullptr) {
        for (uint32_t index = 0; index < *pPropertyCount; index++) {
            CreateObject(pProperties[index].displayModeProperties.displayMode);
        }
    }
}

void ThreadSafety::PreCallRecordGetDisplayPlaneCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkDisplayPlaneInfo2KHR*               pDisplayPlaneInfo,
    VkDisplayPlaneCapabilities2KHR*             pCapabilities) {
    StartWriteObject(pDisplayPlaneInfo->mode, "vkGetDisplayPlaneCapabilities2KHR");
}

void ThreadSafety::PostCallRecordGetDisplayPlaneCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkDisplayPlaneInfo2KHR*               pDisplayPlaneInfo,
    VkDisplayPlaneCapabilities2KHR*             pCapabilities,
    VkResult                                    result) {
    FinishWriteObject(pDisplayPlaneInfo->mode, "vkGetDisplayPlaneCapabilities2KHR");
}

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT

void ThreadSafety::PostCallRecordGetRandROutputDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    Display*                                    dpy,
    RROutput                                    rrOutput,
    VkDisplayKHR*                               pDisplay,
    VkResult                                    result) {
    if ((result != VK_SUCCESS) || (pDisplay == nullptr)) return;
    CreateObject(*pDisplay);
}

#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT


"""


    # This is an ordered list of sections in the header file.
    ALL_SECTIONS = ['command']
    def __init__(self,
                 errFile = sys.stderr,
                 warnFile = sys.stderr,
                 diagFile = sys.stdout):
        OutputGenerator.__init__(self, errFile, warnFile, diagFile)
        # Internal state - accumulators for different inner block text
        self.sections = dict([(section, []) for section in self.ALL_SECTIONS])
        self.non_dispatchable_types = set()

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

    # Map paramtype to a thread safety suffix, either 'ParentInstance' or ''
    def paramSuffix(self, paramtype):
        if paramtype is not None:
            paramtype = paramtype.text
        else:
            paramtype = 'None'

        # Use 'in' to check the types, to handle suffixes and pointers, except for VkDevice
        # which can be confused with VkDeviceMemory
        suffix = ''
        if 'VkSurface' in paramtype or 'VkDebugReportCallback' in paramtype or 'VkDebugUtilsMessenger' in paramtype or 'VkDevice' == paramtype or 'VkDevice*' == paramtype or 'VkInstance' in paramtype:
            suffix = 'ParentInstance'
        return suffix

    def makeThreadUseBlock(self, cmd, name, functionprefix):
        """Generate C function pointer typedef for <command> Element"""
        paramdecl = ''
        # Find and add any parameters that are thread unsafe
        params = cmd.findall('param')
        for param in params:
            paramname = param.find('name')
            if False: # self.paramIsPointer(param):
                paramdecl += '    // not watching use of pointer ' + paramname.text + '\n'
            else:
                externsync = param.attrib.get('externsync')
                if externsync == 'true':
                    if self.paramIsArray(param):
                        paramdecl += 'if (' + paramname.text + ') {\n'
                        paramdecl += '    for (uint32_t index=0; index < ' + param.attrib.get('len') + '; index++) {\n'
                        paramdecl += '        ' + functionprefix + 'WriteObject' + self.paramSuffix(param.find('type')) + '(' + paramname.text + '[index], "' + name + '");\n'
                        paramdecl += '    }\n'
                        paramdecl += '}\n'
                    else:
                        paramdecl += functionprefix + 'WriteObject' + self.paramSuffix(param.find('type')) + '(' + paramname.text + ', "' + name + '");\n'
                        if ('Destroy' in name or 'Free' in name) and functionprefix == 'Finish':
                            paramdecl += 'DestroyObject' + self.paramSuffix(param.find('type')) + '(' + paramname.text + ');\n'
                elif (param.attrib.get('externsync')):
                    if self.paramIsArray(param):
                        # Externsync can list pointers to arrays of members to synchronize
                        paramdecl += 'if (' + paramname.text + ') {\n'
                        paramdecl += '    for (uint32_t index=0; index < ' + param.attrib.get('len') + '; index++) {\n'
                        second_indent = '    '
                        for member in externsync.split(","):
                            # Replace first empty [] in member name with index
                            element = member.replace('[]','[index]',1)

                            # XXX TODO: Can we do better to lookup types of externsync members?
                            suffix = ''
                            if 'surface' in member:
                                suffix = 'ParentInstance'

                            if '[]' in element:
                                # TODO: These null checks can be removed if threading ends up behind parameter
                                #       validation in layer order
                                element_ptr = element.split('[]')[0]
                                paramdecl += '        if (' + element_ptr + ') {\n'
                                # Replace any second empty [] in element name with inner array index based on mapping array
                                # names like "pSomeThings[]" to "someThingCount" array size. This could be more robust by
                                # mapping a param member name to a struct type and "len" attribute.
                                limit = element[0:element.find('s[]')] + 'Count'
                                dotp = limit.rfind('.p')
                                limit = limit[0:dotp+1] + limit[dotp+2:dotp+3].lower() + limit[dotp+3:]
                                paramdecl += '            for (uint32_t index2=0; index2 < '+limit+'; index2++) {\n'
                                element = element.replace('[]','[index2]')
                                second_indent = '        '
                                paramdecl += '        ' + second_indent + functionprefix + 'WriteObject' + suffix + '(' + element + ', "' + name + '");\n'
                                paramdecl += '            }\n'
                                paramdecl += '        }\n'
                            else:
                                paramdecl += '    ' + second_indent + functionprefix + 'WriteObject' + suffix + '(' + element + ', "' + name + '");\n'
                        paramdecl += '    }\n'
                        paramdecl += '}\n'
                    else:
                        # externsync can list members to synchronize
                        for member in externsync.split(","):
                            member = str(member).replace("::", "->")
                            member = str(member).replace(".", "->")
                            # XXX TODO: Can we do better to lookup types of externsync members?
                            suffix = ''
                            if 'surface' in member:
                                suffix = 'ParentInstance'
                            paramdecl += '    ' + functionprefix + 'WriteObject' + suffix + '(' + member + ', "' + name + '");\n'
                elif self.paramIsPointer(param) and ('Create' in name or 'Allocate' in name) and functionprefix == 'Finish':
                    paramtype = param.find('type')
                    if paramtype is not None:
                        paramtype = paramtype.text
                    else:
                        paramtype = 'None'
                    if paramtype in self.handle_types:
                        indent = ''
                        create_pipelines_call = True
                        # The CreateXxxPipelines APIs can return a list of partly created pipelines upon failure
                        if not ('Create' in name and 'Pipelines' in name):
                            paramdecl += 'if (result == VK_SUCCESS) {\n'
                            create_pipelines_call = False
                            indent = '    '
                        if self.paramIsArray(param):
                            # Add pointer dereference for array counts that are pointer values
                            dereference = ''
                            for candidate in params:
                                if param.attrib.get('len') == candidate.find('name').text:
                                    if self.paramIsPointer(candidate):
                                        dereference = '*'
                            param_len = str(param.attrib.get('len')).replace("::", "->")
                            paramdecl += indent + 'if (' + paramname.text + ') {\n'
                            paramdecl += indent + '    for (uint32_t index = 0; index < ' + dereference + param_len + '; index++) {\n'
                            if create_pipelines_call:
                                paramdecl += indent + '        if (!pPipelines[index]) continue;\n'
                            paramdecl += indent + '        CreateObject' + self.paramSuffix(param.find('type')) + '(' + paramname.text + '[index]);\n'
                            paramdecl += indent + '    }\n'
                            paramdecl += indent + '}\n'
                        else:
                            paramdecl += '    CreateObject' + self.paramSuffix(param.find('type')) + '(*' + paramname.text + ');\n'
                        if not create_pipelines_call:
                            paramdecl += '}\n'
                else:
                    paramtype = param.find('type')
                    if paramtype is not None:
                        paramtype = paramtype.text
                    else:
                        paramtype = 'None'
                    if paramtype in self.handle_types and paramtype != 'VkPhysicalDevice':
                        if self.paramIsArray(param) and ('pPipelines' != paramname.text):
                            # Add pointer dereference for array counts that are pointer values
                            dereference = ''
                            for candidate in params:
                                if param.attrib.get('len') == candidate.find('name').text:
                                    if self.paramIsPointer(candidate):
                                        dereference = '*'
                            param_len = str(param.attrib.get('len')).replace("::", "->")
                            paramdecl += 'if (' + paramname.text + ') {\n'
                            paramdecl += '    for (uint32_t index = 0; index < ' + dereference + param_len + '; index++) {\n'
                            paramdecl += '        ' + functionprefix + 'ReadObject' + self.paramSuffix(param.find('type')) + '(' + paramname.text + '[index], "' + name + '");\n'
                            paramdecl += '    }\n'
                            paramdecl += '}\n'
                        elif not self.paramIsPointer(param):
                            # Pointer params are often being created.
                            # They are not being read from.
                            paramdecl += functionprefix + 'ReadObject' + self.paramSuffix(param.find('type')) + '(' + paramname.text + ', "' + name + '");\n'
        explicitexternsyncparams = cmd.findall("param[@externsync]")
        if (explicitexternsyncparams is not None):
            for param in explicitexternsyncparams:
                externsyncattrib = param.attrib.get('externsync')
                paramname = param.find('name')
                paramdecl += '// Host access to '
                if externsyncattrib == 'true':
                    if self.paramIsArray(param):
                        paramdecl += 'each member of ' + paramname.text
                    elif self.paramIsPointer(param):
                        paramdecl += 'the object referenced by ' + paramname.text
                    else:
                        paramdecl += paramname.text
                else:
                    paramdecl += externsyncattrib
                paramdecl += ' must be externally synchronized\n'

        # Find and add any "implicit" parameters that are thread unsafe
        implicitexternsyncparams = cmd.find('implicitexternsyncparams')
        if (implicitexternsyncparams is not None):
            for elem in implicitexternsyncparams:
                paramdecl += '// '
                paramdecl += elem.text
                paramdecl += ' must be externally synchronized between host accesses\n'

        if (paramdecl == ''):
            return None
        else:
            return paramdecl
    def beginFile(self, genOpts):
        OutputGenerator.beginFile(self, genOpts)
        
        # Initialize members that require the tree
        self.handle_types = GetHandleTypes(self.registry.tree)
        self.is_aliased_type = GetHandleAliased(self.registry.tree)

        # TODO: LUGMAL -- remove this and add our copyright
        # User-supplied prefix text, if any (list of strings)
        write(self.inline_copyright_message, file=self.outFile)

        self.header_file = (genOpts.filename == 'thread_safety.h')
        self.source_file = (genOpts.filename == 'thread_safety.cpp')

        if not self.header_file and not self.source_file:
            print("Error: Output Filenames have changed, update generator source.\n")
            sys.exit(1)

        if self.source_file:
            write('#include "chassis.h"', file=self.outFile)
            write('#include "thread_safety.h"', file=self.outFile)
            self.newline()
            write(self.inline_custom_source_preamble, file=self.outFile)


    def endFile(self):

        # Create class definitions
        counter_class_defs = ''
        counter_class_instances = ''
        counter_class_bodies = ''

        for obj in sorted(self.non_dispatchable_types):
            if (not self.is_aliased_type[obj]):
                counter_class_defs += '    counter<%s> c_%s;\n' % (obj, obj)
                obj_type = 'kVulkanObjectType' + obj[2:]
                counter_class_instances += '          c_%s("%s", %s, this),\n' % (obj, obj, obj_type)
                if 'VkSurface' in obj or 'VkDebugReportCallback' in obj or 'VkDebugUtilsMessenger' in obj:
                    counter_class_bodies += 'WRAPPER_PARENT_INSTANCE(%s)\n' % obj
                else:
                    counter_class_bodies += 'WRAPPER(%s)\n' % obj
        if self.header_file:
            class_def = self.inline_custom_header_preamble.replace('COUNTER_CLASS_DEFINITIONS_TEMPLATE', counter_class_defs)
            class_def = class_def.replace('COUNTER_CLASS_INSTANCES_TEMPLATE', counter_class_instances[:-2]) # Kill last comma
            class_def = class_def.replace('COUNTER_CLASS_BODIES_TEMPLATE', counter_class_bodies)
            write(class_def, file=self.outFile)
        write('\n'.join(self.sections['command']), file=self.outFile)
        if self.header_file:
            write('};', file=self.outFile)

        # Finish processing in superclass
        OutputGenerator.endFile(self)

    def beginFeature(self, interface, emit):
        #write('// starting beginFeature', file=self.outFile)
        # Start processing in superclass
        OutputGenerator.beginFeature(self, interface, emit)
        # C-specific
        # Accumulate includes, defines, types, enums, function pointer typedefs,
        # end function prototypes separately for this feature. They're only
        # printed in endFeature().
        self.featureExtraProtect = GetFeatureProtect(interface)
        if (self.featureExtraProtect is not None):
            self.appendSection('command', '\n#ifdef %s' % self.featureExtraProtect)

        #write('// ending beginFeature', file=self.outFile)
    def endFeature(self):
        # C-specific
        if (self.emit):
            if (self.featureExtraProtect is not None):
                self.appendSection('command', '#endif // %s' % self.featureExtraProtect)
        # Finish processing in superclass
        OutputGenerator.endFeature(self)
    #
    # Append a definition to the specified section
    def appendSection(self, section, text):
        self.sections[section].append(text)
    #
    # Type generation
    def genType(self, typeinfo, name, alias):
        OutputGenerator.genType(self, typeinfo, name, alias)
        if self.handle_types.IsNonDispatchable(name):
            self.non_dispatchable_types.add(name)
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
        # Commands shadowed by interface functions and are not implemented
        special_functions = [
            'vkAllocateCommandBuffers',
            'vkFreeCommandBuffers',
            'vkCreateCommandPool',
            'vkResetCommandPool',
            'vkDestroyCommandPool',
            'vkAllocateDescriptorSets',
            'vkFreeDescriptorSets',
            'vkResetDescriptorPool',
            'vkDestroyDescriptorPool',
            'vkQueuePresentKHR',
            'vkGetSwapchainImagesKHR',
            'vkDestroySwapchainKHR',
            'vkDestroyDevice',
            'vkGetDeviceQueue',
            'vkGetDeviceQueue2',
            'vkCreateDescriptorSetLayout',
            'vkUpdateDescriptorSets',
            'vkUpdateDescriptorSetWithTemplate',
            'vkUpdateDescriptorSetWithTemplateKHR',
            'vkGetDisplayPlaneSupportedDisplaysKHR',
            'vkGetDisplayModePropertiesKHR',
            'vkGetDisplayModeProperties2KHR',
            'vkGetDisplayPlaneCapabilities2KHR',
            'vkGetRandROutputDisplayEXT',
        ]
        if name == 'vkQueuePresentKHR' or (name in special_functions and self.source_file):
            return

        if (("DebugMarker" in name or "DebugUtilsObject" in name) and "EXT" in name):
            self.appendSection('command', '// TODO - not wrapping EXT function ' + name)
            return

        # Determine first if this function needs to be intercepted
        startthreadsafety = self.makeThreadUseBlock(cmdinfo.elem, name, 'Start')
        finishthreadsafety = self.makeThreadUseBlock(cmdinfo.elem, name, 'Finish')
        if startthreadsafety is None and finishthreadsafety is None:
            return

        if startthreadsafety is None:
            startthreadsafety = ''
        if finishthreadsafety is None:
            finishthreadsafety = ''

        OutputGenerator.genCmd(self, cmdinfo, name, alias)

        # setup common to call wrappers
        # first parameter is always dispatchable
        dispatchable_type = cmdinfo.elem.find('param/type').text
        dispatchable_name = cmdinfo.elem.find('param/name').text

        decls = self.makeCDecls(cmdinfo.elem)

        result_type = cmdinfo.elem.find('proto/type')

        if self.source_file:
            pre_decl = decls[0][:-1]
            pre_decl = pre_decl.split("VKAPI_CALL ")[1]
            pre_decl = 'void ThreadSafety::PreCallRecord' + pre_decl + ' {'

            # PreCallRecord
            self.appendSection('command', '')
            self.appendSection('command', pre_decl)
            self.appendSection('command', "    " + "\n    ".join(str(startthreadsafety).rstrip().split("\n")))
            self.appendSection('command', '}')

            # PostCallRecord
            post_decl = pre_decl.replace('PreCallRecord', 'PostCallRecord')
            if result_type.text == 'VkResult':
                post_decl = post_decl.replace(')', ',\n    VkResult                                    result)')
            elif result_type.text == 'VkDeviceAddress':
                post_decl = post_decl.replace(')', ',\n    VkDeviceAddress                             result)')
            self.appendSection('command', '')
            self.appendSection('command', post_decl)
            self.appendSection('command', "    " + "\n    ".join(str(finishthreadsafety).rstrip().split("\n")))
            self.appendSection('command', '}')

        if self.header_file:
            pre_decl = decls[0][:-1]
            pre_decl = pre_decl.split("VKAPI_CALL ")[1]
            pre_decl = 'void PreCallRecord' + pre_decl + ';'

            # PreCallRecord
            self.appendSection('command', '')
            self.appendSection('command', pre_decl)

            # PostCallRecord
            post_decl = pre_decl.replace('PreCallRecord', 'PostCallRecord')
            if result_type.text == 'VkResult':
                post_decl = post_decl.replace(')', ',\n    VkResult                                    result)')
            elif result_type.text == 'VkDeviceAddress':
                post_decl = post_decl.replace(')', ',\n    VkDeviceAddress                             result)')
            self.appendSection('command', '')
            self.appendSection('command', post_decl)

    #
    # override makeProtoName to drop the "vk" prefix
    def makeProtoName(self, name, tail):
        return self.genOpts.apientry + name[2:] + tail
