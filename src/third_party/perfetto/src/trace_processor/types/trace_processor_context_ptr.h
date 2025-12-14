/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_TYPES_TRACE_PROCESSOR_CONTEXT_PTR_H_
#define SRC_TRACE_PROCESSOR_TYPES_TRACE_PROCESSOR_CONTEXT_PTR_H_

#include <cstddef>
#include <memory>

namespace perfetto::trace_processor {

// Small shim class to handle owning pointers in TraceProcessorContext objects
// both at the root level and recursively.
template <typename T>
struct TraceProcessorContextPtr {
 public:
  TraceProcessorContextPtr() = default;
  explicit TraceProcessorContextPtr(std::unique_ptr<T> owned)
      : owned_(std::move(owned)), ptr_(owned_.get()) {}
  explicit TraceProcessorContextPtr(T* _ptr) : ptr_(_ptr) {}

  template <class... Args>
  static TraceProcessorContextPtr<T> MakeRoot(Args&&... args) {
    return TraceProcessorContextPtr<T>(
        std::make_unique<T>(std::forward<Args>(args)...));
  }

  TraceProcessorContextPtr& operator=(std::unique_ptr<T> owned) {
    owned_ = std::move(owned);
    ptr_ = owned_.get();
    return *this;
  }

  TraceProcessorContextPtr<T> Fork() const {
    return TraceProcessorContextPtr<T>(ptr_);
  }
  void reset(T* ptr) {
    owned_.reset(ptr);
    ptr_ = ptr;
  }

  T* get() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  T* operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  std::unique_ptr<T> owned_;
  T* ptr_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_TYPES_TRACE_PROCESSOR_CONTEXT_PTR_H_
