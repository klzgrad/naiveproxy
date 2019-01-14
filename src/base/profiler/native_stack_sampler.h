// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_NATIVE_STACK_SAMPLER_H_
#define BASE_PROFILER_NATIVE_STACK_SAMPLER_H_

#include <memory>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/profiler/stack_sampling_profiler.h"
#include "base/threading/platform_thread.h"

namespace base {

class NativeStackSamplerTestDelegate;

// NativeStackSampler is an implementation detail of StackSamplingProfiler. It
// abstracts the native implementation required to record a set of stack frames
// for a given thread.
class NativeStackSampler {
 public:
  // This class contains a buffer for stack copies that can be shared across
  // multiple instances of NativeStackSampler.
  class StackBuffer {
   public:
    StackBuffer(size_t buffer_size);
    ~StackBuffer();

    void* buffer() const { return buffer_.get(); }
    size_t size() const { return size_; }

   private:
    // The word-aligned buffer.
    const std::unique_ptr<uintptr_t[]> buffer_;

    // The size of the buffer.
    const size_t size_;

    DISALLOW_COPY_AND_ASSIGN(StackBuffer);
  };

  virtual ~NativeStackSampler();

  // Creates a stack sampler that records samples for thread with |thread_id|.
  // Returns null if this platform does not support stack sampling.
  static std::unique_ptr<NativeStackSampler> Create(
      PlatformThreadId thread_id,
      NativeStackSamplerTestDelegate* test_delegate);

  // Gets the required size of the stack buffer.
  static size_t GetStackBufferSize();

  // Creates an instance of the a stack buffer that can be used for calls to
  // any NativeStackSampler object.
  static std::unique_ptr<StackBuffer> CreateStackBuffer();

  // The following functions are all called on the SamplingThread (not the
  // thread being sampled).

  // Notifies the sampler that we're starting to record a new profile.
  virtual void ProfileRecordingStarting() = 0;

  // Records a set of frames and returns them.
  virtual std::vector<StackSamplingProfiler::Frame> RecordStackFrames(
      StackBuffer* stackbuffer,
      StackSamplingProfiler::ProfileBuilder* profile_builder) = 0;

 protected:
  NativeStackSampler();

 private:
  DISALLOW_COPY_AND_ASSIGN(NativeStackSampler);
};

// NativeStackSamplerTestDelegate provides seams for test code to execute during
// stack collection.
class BASE_EXPORT NativeStackSamplerTestDelegate {
 public:
  virtual ~NativeStackSamplerTestDelegate();

  // Called after copying the stack and resuming the target thread, but prior to
  // walking the stack. Invoked on the SamplingThread.
  virtual void OnPreStackWalk() = 0;

 protected:
  NativeStackSamplerTestDelegate();

 private:
  DISALLOW_COPY_AND_ASSIGN(NativeStackSamplerTestDelegate);
};

}  // namespace base

#endif  // BASE_PROFILER_NATIVE_STACK_SAMPLER_H_

