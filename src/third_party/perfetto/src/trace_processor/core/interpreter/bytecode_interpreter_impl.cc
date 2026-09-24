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

#include "src/trace_processor/core/interpreter/bytecode_interpreter_impl.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string_view>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/regex.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/ops.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/sort.h"
#include "src/trace_processor/core/util/span.h"
#include "src/trace_processor/util/glob.h"

namespace perfetto::trace_processor::core::interpreter::ops {

namespace {

struct StringSortToken {
  std::string_view str_view;
  StringPool::Id id;
};

struct GlobComparator {
  bool operator()(StringPool::Id lhs, const util::GlobMatcher& m) const {
    return m.Matches(pool->Get(lhs));
  }
  const StringPool* pool;
};

struct BitVectorComparator {
  bool operator()(StringPool::Id lhs, const BitVector& m) const {
    return m.is_set(lhs.raw_id());
  }
};

struct RegexComparator {
  bool operator()(StringPool::Id lhs, const base::Regex& r) const {
    return r.PartialMatch(pool->Get(lhs).c_str());
  }
  const StringPool* pool;
};

struct StringSortKey {
  std::string_view operator()(const StringSortToken& token) const {
    return token.str_view;
  }
};

}  // namespace

void FinalizeRanksInMapImpl(
    const StringPool* string_pool,
    std::unique_ptr<base::FlatHashMap<StringPool::Id, uint32_t>>&
        rank_map_ptr) {
  PERFETTO_DCHECK(rank_map_ptr && rank_map_ptr.get());
  auto& rank_map = *rank_map_ptr;

  // Initially do *not* default initialize the array for performance.
  std::unique_ptr<StringSortToken[]> ids_to_sort(
      new StringSortToken[rank_map.size()]);
  std::unique_ptr<StringSortToken[]> scratch(
      new StringSortToken[rank_map.size()]);
  uint32_t i = 0;
  for (auto it = rank_map.GetIterator(); it; ++it) {
    base::StringView str_view = string_pool->Get(it.key());
    ids_to_sort[i++] = StringSortToken{
        std::string_view(str_view.data(), str_view.size()),
        it.key(),
    };
  }
  auto* sorted =
      core::MsdRadixSort(ids_to_sort.get(), ids_to_sort.get() + rank_map.size(),
                         scratch.get(), StringSortKey{});
  for (uint32_t rank = 0; rank < rank_map.size(); ++rank) {
    auto* it = rank_map.Find(sorted[rank].id);
    PERFETTO_DCHECK(it);
    *it = rank;
  }
}

uint32_t* StringFilterGlobImpl(const StringPool* string_pool,
                               const StringPool::Id* data,
                               const char* pattern,
                               const uint32_t* begin,
                               const uint32_t* end,
                               uint32_t* output) {
  auto matcher = util::GlobMatcher::FromPattern(pattern);

  // If glob pattern doesn't involve any special characters, use equality.
  if (matcher.IsEquality()) {
    std::optional<StringPool::Id> id =
        string_pool->GetId(base::StringView(pattern));
    if (!id) {
      return output;
    }
    const uint32_t* o_read = output;
    uint32_t* o_write = output;
    uint32_t id_raw = id->raw_id();
    for (const uint32_t* it = begin; it != end; ++it, ++o_read) {
      if (data[*it].raw_id() == id_raw) {
        *o_write++ = *o_read;
      }
    }
    return o_write;
  }

  // For very big string pools (or small ranges) or pools with large
  // strings run a standard glob function.
  if (size_t(end - begin) < string_pool->size() ||
      string_pool->HasLargeString()) {
    return ops::Filter(data, begin, end, output, matcher,
                       GlobComparator{string_pool});
  }

  // Pre-compute matches for all strings in the pool.
  auto matches =
      BitVector::CreateWithSize(string_pool->MaxSmallStringId().raw_id());
  PERFETTO_DCHECK(!string_pool->HasLargeString());
  for (auto it = string_pool->CreateSmallStringIterator(); it; ++it) {
    auto id = it.StringId();
    matches.change_assume_unset(id.raw_id(),
                                matcher.Matches(string_pool->Get(id)));
  }

  return ops::Filter(data, begin, end, output, matches, BitVectorComparator{});
}

uint32_t* StringFilterRegexImpl(const StringPool* string_pool,
                                const StringPool::Id* data,
                                const char* pattern,
                                const uint32_t* begin,
                                const uint32_t* end,
                                uint32_t* output) {
  auto regex = base::Regex::Create(pattern);
  if (!regex.ok()) {
    return output;
  }
  return ops::Filter(data, begin, end, output, regex.value(),
                     RegexComparator{string_pool});
}

void LimitOffsetIndices(InterpreterState& state,
                        const struct LimitOffsetIndices& bytecode) {
  using B = struct LimitOffsetIndices;
  uint32_t offset_value = bytecode.arg<B::offset_value>();
  uint32_t limit_value = bytecode.arg<B::limit_value>();
  auto& span = state.ReadFromRegister(bytecode.arg<B::update_register>());

  // Apply offset
  auto original_size = static_cast<uint32_t>(span.size());
  uint32_t actual_offset = std::min(offset_value, original_size);
  span.b += actual_offset;

  // Apply limit
  auto size_after_offset = static_cast<uint32_t>(span.size());
  uint32_t actual_limit = std::min(limit_value, size_after_offset);
  span.e = span.b + actual_limit;
}

void CopySpanIntersectingRange(
    InterpreterState& state,
    const struct CopySpanIntersectingRange& bytecode) {
  using B = struct CopySpanIntersectingRange;
  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  const auto& source_range =
      state.ReadFromRegister(bytecode.arg<B::source_range_register>());
  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  PERFETTO_DCHECK(source.size() <= update.size());
  uint32_t* write_ptr = update.b;
  for (const uint32_t* it = source.b; it != source.e; ++it) {
    *write_ptr = *it;
    write_ptr += (*it >= source_range.b && *it < source_range.e);
  }
  update.e = write_ptr;
}

void InitRankMap(InterpreterState& state, const struct InitRankMap& bytecode) {
  using B = struct InitRankMap;

  StringIdToRankMap* rank_map =
      state.MaybeReadFromRegister(bytecode.arg<B::dest_register>());
  if (rank_map) {
    rank_map->get()->Clear();
  } else {
    state.WriteToRegister(
        bytecode.arg<B::dest_register>(),
        std::make_unique<base::FlatHashMap<StringPool::Id, uint32_t>>());
  }
}

void FinalizeRanksInMap(InterpreterState& state,
                        const struct FinalizeRanksInMap& bytecode) {
  using B = struct FinalizeRanksInMap;
  StringIdToRankMap& rank_map_ptr =
      state.ReadFromRegister(bytecode.arg<B::update_register>());
  FinalizeRanksInMapImpl(state.string_pool, rank_map_ptr);
}

void Distinct(InterpreterState& state, const struct Distinct& bytecode) {
  using B = struct Distinct;
  auto& indices = state.ReadFromRegister(bytecode.arg<B::indices_register>());
  if (indices.empty()) {
    return;
  }
  const auto& buffer =
      state.ReadFromRegister(bytecode.arg<B::buffer_register>());
  uint32_t stride = bytecode.arg<B::total_row_stride>();
  core::ops::DistinctRows(buffer, stride, &indices);
}

void SortRowLayout(InterpreterState& state,
                   const struct SortRowLayout& bytecode) {
  using B = struct SortRowLayout;
  auto& indices = state.ReadFromRegister(bytecode.arg<B::indices_register>());
  // Single element is always sorted.
  if (indices.size() <= 1) {
    return;
  }
  const auto& buffer =
      state.ReadFromRegister(bytecode.arg<B::buffer_register>());
  uint32_t stride = bytecode.arg<B::total_row_stride>();
  core::ops::SortRowLayout(buffer, stride, &indices);
}

void TranslateSparseNullIndices(
    InterpreterState& state,
    const struct TranslateSparseNullIndices& bytecode) {
  using B = struct TranslateSparseNullIndices;
  const NullBitvector& nbv =
      state.ReadFromRegister(bytecode.arg<B::null_bv_register>());

  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  PERFETTO_DCHECK(source.size() <= update.size());

  uint32_t* out = update.b;
  for (uint32_t* it = source.b; it != source.e; ++it, ++out) {
    uint32_t s = *it;
    *out = static_cast<uint32_t>(nbv.popcount[s / 64] +
                                 nbv.bv->count_set_bits_until_in_word(s));
  }
  update.e = out;
}

void AllocateRowLayoutBuffer(InterpreterState& state,
                             const struct AllocateRowLayoutBuffer& bytecode) {
  using B = struct AllocateRowLayoutBuffer;
  uint32_t size = bytecode.arg<B::buffer_size>();
  auto dest_reg = bytecode.arg<B::dest_buffer_register>();
  // Return early if buffer already allocated.
  if (state.MaybeReadFromRegister(dest_reg)) {
    return;
  }
  state.WriteToRegister(dest_reg, Slab<uint8_t>::Alloc(size));
}

void CollectIdIntoRankMap(InterpreterState& state,
                          const struct CollectIdIntoRankMap& bytecode) {
  using B = struct CollectIdIntoRankMap;

  StringIdToRankMap& rank_map_ptr =
      state.ReadFromRegister(bytecode.arg<B::rank_map_register>());
  PERFETTO_DCHECK(rank_map_ptr);
  auto& rank_map = *rank_map_ptr;

  const StringPool::Id* data = state.ReadStorageFromRegister<String>(
      bytecode.arg<B::storage_register>());
  const auto& source =
      state.ReadFromRegister(bytecode.arg<B::source_register>());
  for (const uint32_t* it = source.b; it != source.e; ++it) {
    rank_map.Insert(data[*it], 0);
  }
}

void StrideTranslateAndCopySparseNullIndices(
    InterpreterState& state,
    const struct StrideTranslateAndCopySparseNullIndices& bytecode) {
  using B = struct StrideTranslateAndCopySparseNullIndices;
  const NullBitvector& nbv =
      state.ReadFromRegister(bytecode.arg<B::null_bv_register>());

  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  uint32_t stride = bytecode.arg<B::stride>();
  uint32_t offset = bytecode.arg<B::offset>();
  for (uint32_t* it = update.b; it != update.e; it += stride) {
    uint32_t index = *it;
    if (nbv.bv->is_set(index)) {
      it[offset] =
          static_cast<uint32_t>(nbv.popcount[index / 64] +
                                nbv.bv->count_set_bits_until_in_word(index));
    } else {
      it[offset] = std::numeric_limits<uint32_t>::max();
    }
  }
}

void StrideCopyDenseNullIndices(
    InterpreterState& state,
    const struct StrideCopyDenseNullIndices& bytecode) {
  using B = struct StrideCopyDenseNullIndices;
  const NullBitvector& nbv =
      state.ReadFromRegister(bytecode.arg<B::null_bv_register>());

  auto& update = state.ReadFromRegister(bytecode.arg<B::update_register>());
  uint32_t stride = bytecode.arg<B::stride>();
  uint32_t offset = bytecode.arg<B::offset>();
  for (uint32_t* it = update.b; it != update.e; it += stride) {
    it[offset] =
        nbv.bv->is_set(*it) ? *it : std::numeric_limits<uint32_t>::max();
  }
}

}  // namespace perfetto::trace_processor::core::interpreter::ops
