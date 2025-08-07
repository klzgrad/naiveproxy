/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/profiling/memory/log_histogram.h"

#include <stddef.h>

#include <cinttypes>
#include <utility>
#include <vector>

namespace perfetto {
namespace profiling {
namespace {

// Return largest n such that pow(2, n) < value.
size_t Log2LessThan(uint64_t value) {
  size_t i = 0;
  while (value) {
    i++;
    value >>= 1;
  }
  return i;
}

}  // namespace

const uint64_t LogHistogram::kMaxBucket = 0;

std::vector<std::pair<uint64_t, uint64_t>> LogHistogram::GetData() const {
  std::vector<std::pair<uint64_t, uint64_t>> data;
  data.reserve(kBuckets);
  for (size_t i = 0; i < kBuckets; ++i) {
    if (i == kBuckets - 1)
      data.emplace_back(kMaxBucket, values_[i]);
    else
      data.emplace_back(1 << i, values_[i]);
  }
  return data;
}

size_t LogHistogram::GetBucket(uint64_t value) {
  if (value == 0)
    return 0;

  size_t hibit = Log2LessThan(value);
  if (hibit >= kBuckets)
    return kBuckets - 1;
  return hibit;
}

}  // namespace profiling
}  // namespace perfetto
