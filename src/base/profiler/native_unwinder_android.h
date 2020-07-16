// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_NATIVE_UNWINDER_ANDROID_H_
#define BASE_PROFILER_NATIVE_UNWINDER_ANDROID_H_

#include "base/profiler/unwinder.h"

namespace unwindstack {
class Maps;
class Memory;
}  // namespace unwindstack

namespace base {

// Native unwinder implementation for Android, using libunwindstack.
class NativeUnwinderAndroid : public Unwinder {
 public:
  // Creates maps object from /proc/self/maps for use by NativeUnwinderAndroid.
  // Since this is an expensive call, the maps object should be re-used across
  // all profiles in a process.
  static std::unique_ptr<unwindstack::Maps> CreateMaps();
  static std::unique_ptr<unwindstack::Memory> CreateProcessMemory();
  // Adds modules found from executable loaded memory regions to |module_cache|.
  static void AddInitialModulesFromMaps(
      const unwindstack::Maps& memory_regions_map,
      ModuleCache* module_cache);

  // |exclude_module_with_base_address| is used to exclude a specific module
  // and let another unwinder take control. TryUnwind() will exit with
  // UNRECOGNIZED_FRAME and CanUnwindFrom() will return false when a frame is
  // encountered in that module.
  NativeUnwinderAndroid(unwindstack::Maps* memory_regions_map,
                        unwindstack::Memory* process_memory,
                        uintptr_t exclude_module_with_base_address = 0);
  ~NativeUnwinderAndroid() override;

  NativeUnwinderAndroid(const NativeUnwinderAndroid&) = delete;
  NativeUnwinderAndroid& operator=(const NativeUnwinderAndroid&) = delete;

  // Unwinder
  void AddInitialModules(ModuleCache* module_cache) override;
  bool CanUnwindFrom(const Frame& current_frame) const override;
  UnwindResult TryUnwind(RegisterContext* thread_context,
                         uintptr_t stack_top,
                         ModuleCache* module_cache,
                         std::vector<Frame>* stack) const override;

 private:
  void EmitDexFrame(uintptr_t dex_pc,
                    ModuleCache* module_cache,
                    std::vector<Frame>* stack) const;

  unwindstack::Maps* const memory_regions_map_;
  unwindstack::Memory* const process_memory_;
  const uintptr_t exclude_module_with_base_address_;
};

}  // namespace base

#endif  // BASE_PROFILER_NATIVE_UNWINDER_ANDROID_H_
