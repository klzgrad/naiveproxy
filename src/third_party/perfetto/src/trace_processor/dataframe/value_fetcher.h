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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_VALUE_FETCHER_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_VALUE_FETCHER_H_

#include <cstddef>
#include <cstdint>

#include "perfetto/base/logging.h"

namespace perfetto::trace_processor::dataframe {

// Fetcher for values from an aribtrary indexed source. The meaning of the index
// in each of the *Value methods varies depending on where this class is used.
//
// Note: all the methods in this class are declared but not defined as this
// class is simply an interface which needs to be subclassed and all
// methods/variables implemented. The methods are intentionally not defined to
// cause link errors if not implemented.
struct ValueFetcher {
  using Type = int;
  static const Type kInt64;
  static const Type kDouble;
  static const Type kString;
  static const Type kNull;

  // Functions for operating on scalar values. The caller should know that
  // the value at the give index is a scalar and not an iterator.

  // Fetches an int64_t value at the given index.
  int64_t GetInt64Value(uint32_t);
  // Fetches a double value at the given index.
  double GetDoubleValue(uint32_t);
  // Fetches a string value at the given index.
  const char* GetStringValue(uint32_t);
  // Fetches the type of the value at the given index.
  Type GetValueType(uint32_t);

  // Functions for operating on iterators. The caller should know that
  // the value at the given index is an iterator and not a scalar.

  // Initializes the iterator at the given index. Returns true if the
  // iterator has elements, false otherwise.
  bool IteratorInit(uint32_t);
  // Forwards the iterator to the next value and returns true if the iterator
  // has more elements, false otherwise.
  bool IteratorNext(uint32_t);
};

// ErrorValueFetcher is a dummy implementation of ValueFetcher that returns
// an error value for all methods. This is used in cases where a
// ValueFetcher is required but no actual data is available (e.g. where you are
// iterating over a dataframe without filtering).
struct ErrorValueFetcher : public ValueFetcher {
  static const Type kInt64 = 0;
  static const Type kDouble = 1;
  static const Type kString = 2;
  static const Type kNull = 3;
  static int64_t GetInt64Value(uint32_t) {
    PERFETTO_FATAL("Dummy implementation; should not be called");
  }
  static double GetDoubleValue(uint32_t) {
    PERFETTO_FATAL("Dummy implementation; should not be called");
  }
  static const char* GetStringValue(uint32_t) {
    PERFETTO_FATAL("Dummy implementation; should not be called");
  }
  static Type GetValueType(uint32_t) {
    PERFETTO_FATAL("Dummy implementation; should not be called");
  }
  static bool IteratorInit(uint32_t) { PERFETTO_FATAL("Unsupported"); }
  static bool IteratorNext(uint32_t) { PERFETTO_FATAL("Unsupported"); }
};

}  // namespace perfetto::trace_processor::dataframe

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_VALUE_FETCHER_H_
