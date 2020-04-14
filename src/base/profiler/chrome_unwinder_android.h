// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_CHROME_UNWINDER_ANDROID_H_
#define BASE_PROFILER_CHROME_UNWINDER_ANDROID_H_

#include "base/profiler/unwinder.h"

#include "base/base_export.h"
#include "base/optional.h"
#include "base/profiler/arm_cfi_table.h"
#include "base/profiler/register_context.h"
#include "base/sampling_heap_profiler/module_cache.h"

namespace base {

// Chrome unwinder implementation for Android, using ArmCfiTable.
class BASE_EXPORT ChromeUnwinderAndroid : public Unwinder {
 public:
  ChromeUnwinderAndroid(const ArmCFITable* cfi_table);
  ~ChromeUnwinderAndroid() override;
  ChromeUnwinderAndroid(const ChromeUnwinderAndroid&) = delete;
  ChromeUnwinderAndroid& operator=(const ChromeUnwinderAndroid&) = delete;

  void SetExpectedChromeModuleIdForTesting(const std::string& chrome_module_id);

  // Unwinder:
  void AddNonNativeModules(ModuleCache* module_cache) override;
  bool CanUnwindFrom(const Frame* current_frame) const override;
  UnwindResult TryUnwind(RegisterContext* thread_context,
                         uintptr_t stack_top,
                         ModuleCache* module_cache,
                         std::vector<Frame>* stack) const override;

  static bool StepForTesting(RegisterContext* thread_context,
                             uintptr_t stack_top,
                             const ArmCFITable::FrameEntry& entry) {
    return Step(thread_context, stack_top, entry);
  }

 private:
  static bool Step(RegisterContext* thread_context,
                   uintptr_t stack_top,
                   const ArmCFITable::FrameEntry& entry);

  const ArmCFITable* cfi_table_;
  std::string chrome_module_id_;
};

}  // namespace base

#endif  // BASE_PROFILER_CHROME_UNWINDER_ANDROID_H_
