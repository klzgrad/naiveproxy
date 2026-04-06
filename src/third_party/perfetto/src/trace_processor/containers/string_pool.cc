/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/containers/string_pool.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"

namespace perfetto::trace_processor {

StringPool::StringPool() {
  // Reserve a slot for the null string.
  MaybeLockGuard guard{mutex_, should_acquire_mutex_};
  blocks_[block_index_] = std::make_unique<uint8_t[]>(kBlockSizeBytes);
  block_end_ptrs_[block_index_] = blocks_[block_index_].get();
  InsertInCurrentBlock({});
}

StringPool::Id StringPool::InsertString(base::StringView str) {
  // If the string is over `kMinLargeStringSizeBytes` in size, don't bother
  // adding it to a block, just put it in the large strings vector.
  if (PERFETTO_UNLIKELY(str.size() >= kMinLargeStringSizeBytes)) {
    return InsertLargeString(str);
  }

  // If the current block does not have enough space, go to the next block.
  uint8_t* max_pos = block_end_ptrs_[block_index_] + str.size() + kMetadataSize;
  if (PERFETTO_UNLIKELY(max_pos >
                        blocks_[block_index_].get() + kBlockSizeBytes)) {
    uint32_t new_index = ++block_index_;
    if (PERFETTO_UNLIKELY(new_index >= kMaxBlockCount)) {
      PERFETTO_FATAL(
          "StringPool exceeded maximum number of blocks. This means the bytes "
          "consumed by of unique strings interned in the pool exceeded %u MB. "
          "If your workload legitimately requires more unique strings, "
          "please file a bug. For workarounds, consider turning off parsing "
          "of raw ftrace events as this can generate a very large number of "
          "unique strings. How you do this varies depending on how you are "
          "using trace processor: 1) for the CLI, use the "
          "--no-ftrace-raw flag; 2) for the Python API, set the "
          "ingest_ftrace_in_raw flag to false in TraceProcessorConfig (this "
          "is also the default) 3) for the C++ API, set the "
          "ingest_ftrace_in_raw flag to false in TraceProcessorConfig.",
          (kMaxBlockCount * kBlockSizeBytes) / (1024 * 1024));
    }
    blocks_[new_index] = std::make_unique<uint8_t[]>(kBlockSizeBytes);
    block_end_ptrs_[new_index] = blocks_[new_index].get();
  }

  // Actually perform the insertion.
  return Id::BlockString(block_index_, InsertInCurrentBlock(str));
}

StringPool::Id StringPool::InsertLargeString(base::StringView str) {
  // +1 for the null terminator.
  large_strings_.emplace_back(std::make_unique<char[]>(str.size() + 1),
                              str.size());

  auto& [ptr, size] = large_strings_.back();
  memcpy(ptr.get(), str.data(), str.size());
  ptr[str.size()] = '\0';

  return Id::LargeString(large_strings_.size() - 1);
}

uint32_t StringPool::InsertInCurrentBlock(base::StringView str) {
  uint8_t*& block_end_ref = block_end_ptrs_[block_index_];
  uint8_t* str_start = block_end_ref;

  // Get where we should start writing this string.
  auto str_size = static_cast<uint32_t>(str.size());

  // First write the size of the string.
  memcpy(block_end_ref, &str_size, sizeof(uint32_t));
  block_end_ref += sizeof(uint32_t);

  // Next the string itself.
  if (PERFETTO_LIKELY(str_size > 0)) {
    memcpy(block_end_ref, str.data(), str_size);
  }
  block_end_ref += str_size;

  // Finally add a null terminator.
  *(block_end_ref++) = '\0';
  return static_cast<uint32_t>(str_start - blocks_[block_index_].get());
}

}  // namespace perfetto::trace_processor
