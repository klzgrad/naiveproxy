// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/stack_copier_signal.h"

#include "base/profiler/metadata_recorder.h"
#include "base/profiler/sample_metadata.h"
#include "base/profiler/stack_buffer.h"
#include "base/profiler/suspendable_thread_delegate.h"

namespace base {

StackCopierSignal::StackCopierSignal(
    std::unique_ptr<ThreadDelegate> thread_delegate)
    : thread_delegate_(std::move(thread_delegate)) {}

StackCopierSignal::~StackCopierSignal() = default;

bool StackCopierSignal::CopyStack(StackBuffer* stack_buffer,
                                  uintptr_t* stack_top,
                                  ProfileBuilder* profile_builder,
                                  RegisterContext* thread_context) {
  // TODO(wittman): Implement signal-based stack copying.
  return false;
}

}  // namespace base
