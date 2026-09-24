/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_CORE_UTIL_SPAN_H_
#define SRC_TRACE_PROCESSOR_CORE_UTIL_SPAN_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "perfetto/base/logging.h"

namespace perfetto::trace_processor::core {

// Represents a contiguous sequence of elements of an arbitrary type T.
// Basically a very simple backport of std::span to C++17.
template <typename T>
struct Span {
  using value_type = T;
  using const_iterator = T*;

  T* b = nullptr;
  T* e = nullptr;

  Span() = default;
  Span(T* _b, T* _e) : b(_b), e(_e) {}

  T* begin() const { return b; }
  T* end() const { return e; }
  T* data() const { return b; }
  size_t size() const { return static_cast<size_t>(e - b); }
  bool empty() const { return b == e; }
  T& operator[](size_t index) const { return b[index]; }
  Span<T> subspan(size_t offset, size_t count) const {
    PERFETTO_DCHECK(offset <= size());
    PERFETTO_DCHECK(count <= size() - offset);
    return Span<T>(b + offset, b + offset + count);
  }
};

template <typename T, size_t N>
Span<T> MakeMutableSpan(T (&values)[N]) {
  return Span<T>(values, values + N);
}

template <typename T, size_t N>
Span<const T> MakeSpan(const T (&values)[N]) {
  return Span<const T>(values, values + N);
}

template <typename T>
Span<T> MakeMutableSpan(std::vector<T>& values) {
  return Span<T>(values.data(), values.data() + values.size());
}

template <typename T>
Span<const T> MakeSpan(const std::vector<T>& values) {
  return Span<const T>(values.data(), values.data() + values.size());
}

template <typename T>
Span<const uint8_t> AsBytes(Span<T> values) {
  const auto* begin = reinterpret_cast<const uint8_t*>(values.begin());
  return Span<const uint8_t>(begin, begin + values.size() * sizeof(T));
}

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_UTIL_SPAN_H_
