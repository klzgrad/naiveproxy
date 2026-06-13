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
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/regex.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/sort.h"
#include "src/trace_processor/core/util/span.h"
#include "src/trace_processor/util/glob.h"

namespace perfetto::trace_processor::core::interpreter::ops {

namespace {

struct SortToken {
  uint32_t index;
  uint32_t buf_offset;
};

struct StringSortToken {
  std::string_view str_view;
  StringPool::Id id;
};

// Crossover point where our custom RadixSort starts becoming faster than
// std::stable_sort.
//
// Empirically chosen by looking at the crossover point of benchmarks
// BM_DataframeSortLsdRadix and BM_DataframeSortLsdStd.
constexpr uint32_t kStableSortCutoff = 4096;

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

}  // namespace

void SortRowLayoutImpl(const Slab<uint8_t>& buffer,
                       uint32_t stride,
                       Span<uint32_t>& indices) {
  auto num_indices = static_cast<size_t>(indices.e - indices.b);

  // Single element is always sorted.
  if (num_indices <= 1) {
    return;
  }

  const uint8_t* buf = buffer.data();

  // Initially do *not* default initialize the array for performance.
  std::unique_ptr<SortToken[]> p(new SortToken[num_indices]);
  std::unique_ptr<SortToken[]> q;
  for (uint32_t i = 0; i < num_indices; ++i) {
    p[i] = {indices.b[i], i * stride};
  }

  SortToken* res;
  if (num_indices < kStableSortCutoff) {
    std::stable_sort(p.get(), p.get() + num_indices,
                     [buf, stride](const SortToken& a, const SortToken& b) {
                       return memcmp(buf + a.buf_offset, buf + b.buf_offset,
                                     stride) < 0;
                     });
    res = p.get();
  } else {
    // We declare q above and populate it here because res might point to q
    // so we need to make sure that q outlives the end of this block.
    // Initially do *not* default initialize the arrays for performance.
    q.reset(new SortToken[num_indices]);
    std::unique_ptr<uint32_t[]> counts(new uint32_t[1 << 16]);
    res = core::RadixSort(
        p.get(), p.get() + num_indices, q.get(), counts.get(), stride,
        [buf](const SortToken& token) { return buf + token.buf_offset; });
  }

  for (uint32_t i = 0; i < num_indices; ++i) {
    indices.b[i] = res[i].index;
  }
}

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
  auto* sorted = core::MsdRadixSort(
      ids_to_sort.get(), ids_to_sort.get() + rank_map.size(), scratch.get(),
      [](const StringSortToken& token) { return token.str_view; });
  for (uint32_t rank = 0; rank < rank_map.size(); ++rank) {
    auto* it = rank_map.Find(sorted[rank].id);
    PERFETTO_DCHECK(it);
    *it = rank;
  }
}

void DistinctImpl(const Slab<uint8_t>& buffer,
                  uint32_t stride,
                  Span<uint32_t>& indices) {
  if (indices.empty()) {
    return;
  }

  const uint8_t* row_ptr = buffer.data();

  std::unordered_set<std::string_view> seen_rows;
  seen_rows.reserve(indices.size());
  uint32_t* write_ptr = indices.b;
  for (const uint32_t* it = indices.b; it != indices.e; ++it) {
    std::string_view row_view(reinterpret_cast<const char*>(row_ptr), stride);
    *write_ptr = *it;
    write_ptr += seen_rows.insert(row_view).second;
    row_ptr += stride;
  }
  indices.e = write_ptr;
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
  DistinctImpl(buffer, stride, indices);
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
  SortRowLayoutImpl(buffer, stride, indices);
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

namespace {

// Helper: builds P2C CSR structure from TreeState's parent array.
void BuildP2CFromTreeState(TreeState* ts) {
  if (ts->p2c_valid) {
    return;
  }
  uint32_t n = ts->row_count;

  // Use scratch2 for child_counts.
  uint32_t* child_counts = ts->scratch2.begin();
  memset(child_counts, 0, n * sizeof(uint32_t));

  uint32_t root_count = 0;
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t parent = ts->parent[i];
    if (parent == kNullParent) {
      ++root_count;
    } else {
      ++child_counts[parent];
    }
  }

  // Compute offsets (prefix sum).
  ts->p2c_offsets[0] = 0;
  for (uint32_t i = 0; i < n; ++i) {
    ts->p2c_offsets[i + 1] = ts->p2c_offsets[i] + child_counts[i];
  }

  // Fill children and roots.
  uint32_t root_idx = 0;
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t parent = ts->parent[i];
    if (parent == kNullParent) {
      ts->p2c_roots[root_idx++] = i;
    } else {
      uint32_t pos = ts->p2c_offsets[parent + 1] - child_counts[parent];
      ts->p2c_children[pos] = i;
      --child_counts[parent];
    }
  }
  ts->p2c_root_count = root_count;
  ts->p2c_valid = true;
}

// Runs a single propagate-down pass over the BFS order for one column.
// |Combine| is a lambda (parent_val, child_val) -> new_child_val.
template <typename T, typename Combine>
void PropagateDownBfs(T* d,
                      Combine combine,
                      const uint32_t* queue,
                      uint32_t queue_end,
                      const uint32_t* parent_arr) {
  for (uint32_t qi = 0; qi < queue_end; ++qi) {
    uint32_t node = queue[qi];
    uint32_t p = parent_arr[node];
    if (p != kNullParent) {
      d[node] = combine(d[p], d[node]);
    }
  }
}

// Dispatches on storage type then agg_op for a single propagate-down spec.
template <typename T>
void PropagateDownColumnTyped(T* d,
                              PropagateAggOp agg_op,
                              const uint32_t* queue,
                              uint32_t queue_end,
                              const uint32_t* parent_arr) {
  switch (agg_op) {
    case PropagateAggOp::kSum:
      PropagateDownBfs(
          d, [](auto p, auto c) { return p + c; }, queue, queue_end,
          parent_arr);
      break;
    case PropagateAggOp::kMin:
      PropagateDownBfs(
          d, [](auto p, auto c) { return std::min(p, c); }, queue, queue_end,
          parent_arr);
      break;
    case PropagateAggOp::kMax:
      PropagateDownBfs(
          d, [](auto p, auto c) { return std::max(p, c); }, queue, queue_end,
          parent_arr);
      break;
    case PropagateAggOp::kFirst:
      PropagateDownBfs(
          d, [](auto p, auto) { return p; }, queue, queue_end, parent_arr);
      break;
    case PropagateAggOp::kLast:
      break;
  }
}

void PropagateDownColumn(uint8_t* data,
                         StorageType storage_type,
                         PropagateAggOp agg_op,
                         const uint32_t* queue,
                         uint32_t queue_end,
                         const uint32_t* parent_arr) {
  if (storage_type.Is<Uint32>()) {
    PropagateDownColumnTyped(reinterpret_cast<uint32_t*>(data), agg_op, queue,
                             queue_end, parent_arr);
  } else if (storage_type.Is<Int32>()) {
    PropagateDownColumnTyped(reinterpret_cast<int32_t*>(data), agg_op, queue,
                             queue_end, parent_arr);
  } else if (storage_type.Is<Int64>()) {
    PropagateDownColumnTyped(reinterpret_cast<int64_t*>(data), agg_op, queue,
                             queue_end, parent_arr);
  } else if (storage_type.Is<Double>()) {
    PropagateDownColumnTyped(reinterpret_cast<double*>(data), agg_op, queue,
                             queue_end, parent_arr);
  } else {
    PERFETTO_FATAL("Unsupported storage type for propagate down");
  }
}

}  // namespace

void FilterTreeState(InterpreterState& state,
                     const struct FilterTreeState& bc) {
  using B = struct FilterTreeState;

  auto& ts = state.ReadFromRegister(bc.arg<B::tree_state_register>());
  auto& indices = state.ReadFromRegister(bc.arg<B::indices_register>());
  uint32_t n = ts->row_count;

  if (n == 0 || indices.size() == n) {
    return;
  }

  // Ensure P2C is valid.
  BuildP2CFromTreeState(ts.get());

  // Reuse pre-allocated keep_bv (avoids allocation per call).
  ts->keep_bv.resize(n);
  ts->keep_bv.ClearAllBits();
  for (const uint32_t* it = indices.b; it != indices.e; ++it) {
    ts->keep_bv.set(*it);
  }
  const auto& keep_bv = ts->keep_bv;

  // BFS to compute surviving ancestors.
  uint32_t* surviving_ancestor = ts->scratch1.begin();
  uint32_t* queue = ts->scratch1.begin() + n;
  uint32_t* old_to_new = ts->scratch2.begin();

  memset(surviving_ancestor, 0xFF, n * sizeof(uint32_t));
  memset(old_to_new, 0xFF, n * sizeof(uint32_t));

  uint32_t queue_end = 0;
  for (uint32_t i = 0; i < ts->p2c_root_count; ++i) {
    uint32_t root = ts->p2c_roots[i];
    if (keep_bv.is_set(root)) {
      surviving_ancestor[root] = root;
    }
    queue[queue_end++] = root;
  }

  for (uint32_t qi = 0; qi < queue_end; ++qi) {
    uint32_t node = queue[qi];
    uint32_t node_ancestor = surviving_ancestor[node];
    uint32_t cs = ts->p2c_offsets[node];
    uint32_t ce = ts->p2c_offsets[node + 1];
    for (uint32_t ci = cs; ci < ce; ++ci) {
      uint32_t child = ts->p2c_children[ci];
      surviving_ancestor[child] = keep_bv.is_set(child) ? child : node_ancestor;
      queue[queue_end++] = child;
    }
  }

  // Pass 1 (fused): build old_to_new + compact original_rows.
  uint32_t new_count = 0;
  for (uint32_t i = 0; i < n; ++i) {
    if (!keep_bv.is_set(i)) {
      continue;
    }
    uint32_t new_idx = new_count++;
    old_to_new[i] = new_idx;
    ts->original_rows[new_idx] = ts->original_rows[i];
  }

  if (new_count == 0) {
    ts->row_count = 0;
    ts->p2c_valid = false;
    indices.e = indices.b;
    return;
  }

  // Pass 2: parent reparenting (needs full old_to_new from pass 1).
  for (uint32_t i = 0; i < n; ++i) {
    if (!keep_bv.is_set(i)) {
      continue;
    }
    uint32_t new_idx = old_to_new[i];
    uint32_t old_parent = ts->parent[i];
    uint32_t ancestor = (old_parent != kNullParent)
                            ? surviving_ancestor[old_parent]
                            : kNullParent;
    ts->parent[new_idx] =
        (ancestor != kNullParent) ? old_to_new[ancestor] : kNullParent;
  }

  // Compact each column in-place.
  for (auto& col : ts->columns) {
    auto es = static_cast<uint64_t>(col.elem_size);
    uint8_t* data = col.data.begin();
    for (uint32_t i = 0; i < n; ++i) {
      if (!keep_bv.is_set(i)) {
        continue;
      }
      uint32_t new_idx = old_to_new[i];
      if (new_idx != i) {
        memcpy(data + new_idx * es, data + i * es, col.elem_size);
      }
    }
  }

  // Compact null bitvectors (skip non-nullable columns with empty bv).
  for (auto& bv : ts->null_bitvectors) {
    if (bv.size() > 0) {
      bv = std::move(bv).Compact(keep_bv);
    }
  }

  ts->row_count = new_count;
  ts->p2c_valid = false;

  // Reset indices to [0..new_count-1] for subsequent operations.
  std::iota(indices.b, indices.b + new_count, 0u);
  indices.e = indices.b + new_count;
}

void PropagateTreeDown(InterpreterState& state,
                       const struct PropagateTreeDown& bc) {
  using B = struct PropagateTreeDown;
  auto& ts = state.ReadFromRegister(bc.arg<B::tree_state_register>());
  uint32_t spec_start = bc.arg<B::spec_start>();
  uint32_t spec_count = bc.arg<B::spec_count>();

  if (ts->row_count == 0 || spec_count == 0) {
    return;
  }

  BuildP2CFromTreeState(ts.get());

  // BFS from roots, building a topological order in scratch1.
  uint32_t* queue = ts->scratch1.begin();
  uint32_t queue_end = 0;
  for (uint32_t i = 0; i < ts->p2c_root_count; ++i) {
    queue[queue_end++] = ts->p2c_roots[i];
  }
  for (uint32_t qi = 0; qi < queue_end; ++qi) {
    uint32_t cs = ts->p2c_offsets[queue[qi]];
    uint32_t ce = ts->p2c_offsets[queue[qi] + 1];
    for (uint32_t ci = cs; ci < ce; ++ci) {
      queue[queue_end++] = ts->p2c_children[ci];
    }
  }

  // Process only the specs in [spec_start, spec_start + spec_count).
  // For each: copy source → dest, then BFS propagation on dest.
  const uint32_t* parent_arr = ts->parent.begin();
  uint32_t n = ts->row_count;
  for (uint32_t si = spec_start; si < spec_start + spec_count; ++si) {
    const auto& spec = ts->propagate_down_specs[si];
    uint8_t* src_data = ts->columns[spec.source_ts_col].data.begin();
    uint8_t* dst_data = ts->columns[spec.dest_ts_col].data.begin();
    uint32_t byte_count = n * ts->columns[spec.dest_ts_col].elem_size;

    // Copy source data into dest column.
    memcpy(dst_data, src_data, byte_count);

    // BFS propagation on dest data.
    PropagateDownColumn(dst_data, spec.storage_type, spec.agg_op, queue,
                        queue_end, parent_arr);
  }
}

}  // namespace perfetto::trace_processor::core::interpreter::ops
