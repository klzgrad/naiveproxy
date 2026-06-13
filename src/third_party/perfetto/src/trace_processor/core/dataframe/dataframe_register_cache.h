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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_DATAFRAME_REGISTER_CACHE_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_DATAFRAME_REGISTER_CACHE_H_

#include <cstdint>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "src/trace_processor/core/interpreter/bytecode_builder.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"

namespace perfetto::trace_processor::core::dataframe {

// A helper that wraps BytecodeBuilder's AllocateRegister with a cache keyed
// by (reg_type, void* address). This allows callers to cache registers for
// columns/indexes using their pointer identity as the key.
class DataframeRegisterCache {
 public:
  explicit DataframeRegisterCache(interpreter::BytecodeBuilder& builder)
      : builder_(builder) {}

  template <typename T>
  struct CachedRegister {
    interpreter::RwHandle<T> reg;
    bool inserted;
  };

  // Gets a register from the cache, or allocates a new one if not found.
  // The key is formed from (reg_type, ptr). Returns the register and whether
  // it was newly inserted.
  template <typename T>
  CachedRegister<T> GetOrAllocate(uint32_t reg_type, const void* ptr) {
    CacheKey key{reg_type, ptr};
    auto* it = cache_.Find(key);
    if (it) {
      return {interpreter::RwHandle<T>{it->index}, false};
    }
    auto reg = builder_.AllocateRegister<T>();
    cache_[key] = interpreter::HandleBase{reg.index};
    return {reg, true};
  }

  void Clear() { cache_.Clear(); }

 private:
  struct CacheKey {
    uint32_t reg_type;
    const void* ptr;

    bool operator==(const CacheKey& o) const {
      return reg_type == o.reg_type && ptr == o.ptr;
    }

    friend base::MurmurHashCombiner PerfettoHashValue(
        base::MurmurHashCombiner h,
        const CacheKey& k) {
      return base::MurmurHashCombiner::Combine(
          std::move(h), k.reg_type,
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(k.ptr)));
    }
  };

  base::FlatHashMapV2<CacheKey, interpreter::HandleBase> cache_;
  interpreter::BytecodeBuilder& builder_;
};

}  // namespace perfetto::trace_processor::core::dataframe

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_DATAFRAME_REGISTER_CACHE_H_
