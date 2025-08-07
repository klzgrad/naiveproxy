/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/types.h>
#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/containers/row_map.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/db/query_executor.h"
#include "src/trace_processor/db/table.h"

namespace perfetto::trace_processor {

namespace {

using Range = RowMap::Range;
using Indices = column::DataLayerChain::Indices;
using OrderedIndices = column::DataLayerChain::OrderedIndices;

}  // namespace

void QueryExecutor::ApplyConstraint(const Constraint& c,
                                    const column::DataLayerChain& chain,
                                    RowMap* rm) {
  // Shortcut of empty row map.
  uint32_t rm_size = rm->size();
  if (rm_size == 0)
    return;

  uint32_t rm_first = rm->Get(0);
  if (rm_size == 1) {
    switch (chain.SingleSearch(c.op, c.value, rm_first)) {
      case SingleSearchResult::kMatch:
        return;
      case SingleSearchResult::kNoMatch:
        rm->Clear();
        return;
      case SingleSearchResult::kNeedsFullSearch:
        break;
    }
  }

  switch (chain.ValidateSearchConstraints(c.op, c.value)) {
    case SearchValidationResult::kNoData:
      rm->Clear();
      return;
    case SearchValidationResult::kAllData:
      return;
    case SearchValidationResult::kOk:
      break;
  }

  uint32_t rm_last = rm->Get(rm_size - 1);
  uint32_t range_size = rm_last - rm_first;

  // Always prefer linear search if on a range *except* when the range is small
  // but the last element of the range is large: this will cause a big bitvector
  // to be created which negates the benefits of using linear search over
  // index search.
  bool disallows_index_search = rm->IsRange() && rm_last < range_size * 100;

  // If the number of elements in the rowmap is small or the number of
  // elements is less than 1/10th of the range, use indexed filtering.
  // TODO(b/283763282): use Overlay estimations.
  bool prefers_index_search =
      rm->IsIndexVector() || rm_size < 1024 || rm_size * 10 < range_size;

  if (!disallows_index_search && prefers_index_search) {
    IndexSearch(c, chain, rm);
    return;
  }
  LinearSearch(c, chain, rm);
}

void QueryExecutor::LinearSearch(const Constraint& c,
                                 const column::DataLayerChain& chain,
                                 RowMap* rm) {
  // TODO(b/283763282): Align these to word boundaries.
  Range bounds(rm->Get(0), rm->Get(rm->size() - 1) + 1);

  // Search the storage.
  RangeOrBitVector res = chain.Search(c.op, c.value, bounds);
  if (rm->IsRange()) {
    if (res.IsRange()) {
      Range range = std::move(res).TakeIfRange();
      *rm = RowMap(range.start, range.end);
    } else {
      // The BitVector was already limited on the RowMap when created, so we
      // can take it as it is.
      *rm = RowMap(std::move(res).TakeIfBitVector());
    }
    return;
  }

  if (res.IsRange()) {
    Range range = std::move(res).TakeIfRange();
    rm->Intersect(RowMap(range.start, range.end));
    return;
  }
  rm->Intersect(RowMap(std::move(res).TakeIfBitVector()));
}

void QueryExecutor::IndexSearch(const Constraint& c,
                                const column::DataLayerChain& chain,
                                RowMap* rm) {
  // Create outmost TableIndexVector.
  std::vector<uint32_t> table_indices = std::move(*rm).TakeAsIndexVector();

  Indices indices = Indices::Create(table_indices, Indices::State::kMonotonic);
  chain.IndexSearch(c.op, c.value, indices);

  PERFETTO_DCHECK(indices.tokens.size() <= table_indices.size());
  for (uint32_t i = 0; i < indices.tokens.size(); ++i) {
    table_indices[i] = indices.tokens[i].payload;
  }
  table_indices.resize(indices.tokens.size());
  PERFETTO_DCHECK(std::is_sorted(table_indices.begin(), table_indices.end()));
  *rm = RowMap(std::move(table_indices));
}

void QueryExecutor::SortLegacy(const Table* table,
                               const std::vector<Order>& ob,
                               std::vector<uint32_t>& out) {
  // Setup the sort token payload to match the input vector of indices. The
  // value of the payload will be untouched by the algorithm even while the
  // order changes to match the ordering defined by the input constraint set.
  std::vector<Token> rows(out.size());
  for (uint32_t i = 0; i < out.size(); ++i) {
    rows[i].payload = out[i];
  }

  // As our data is columnar, it's always more efficient to sort one column
  // at a time rather than try and sort lexiographically all at once.
  // To preserve correctness, we need to stably sort the index vector once
  // for each order by in *reverse* order. Reverse order is important as it
  // preserves the lexiographical property.
  //
  // For example, suppose we have the following:
  // Table {
  //   Column x;
  //   Column y
  //   Column z;
  // }
  //
  // Then, to sort "y asc, x desc", we could do one of two things:
  //  1) sort the index vector all at once and on each index, we compare
  //     y then z. This is slow as the data is columnar and we need to
  //     repeatedly branch inside each column.
  //  2) we can stably sort first on x desc and then sort on y asc. This will
  //     first put all the x in the correct order such that when we sort on
  //     y asc, we will have the correct order of x where y is the same (since
  //     the sort is stable).
  //
  // TODO(lalitm): it is possible that we could sort the last constraint (i.e.
  // the first constraint in the below loop) in a non-stable way. However,
  // this is more subtle than it appears as we would then need special
  // handling where there are order bys on a column which is already sorted
  // (e.g. ts, id). Investigate whether the performance gains from this are
  // worthwhile. This also needs changes to the constraint modification logic
  // in DbSqliteTable which currently eliminates constraints on sorted
  // columns.
  for (auto it = ob.rbegin(); it != ob.rend(); ++it) {
    // Reset the index to the payload at the start of each iote
    for (uint32_t i = 0; i < out.size(); ++i) {
      rows[i].index = rows[i].payload;
    }
    table->ChainForColumn(it->col_idx)
        .StableSort(
            rows.data(), rows.data() + rows.size(),
            it->desc ? SortDirection::kDescending : SortDirection::kAscending);
  }

  // Recapture the payload from each of the sort tokens whose order now
  // indicates the order
  for (uint32_t i = 0; i < out.size(); ++i) {
    out[i] = rows[i].payload;
  }
}

void QueryExecutor::BoundedColumnFilterForTesting(
    const Constraint& c,
    const column::DataLayerChain& col,
    RowMap* rm) {
  LinearSearch(c, col, rm);
}

void QueryExecutor::IndexedColumnFilterForTesting(
    const Constraint& c,
    const column::DataLayerChain& col,
    RowMap* rm) {
  IndexSearch(c, col, rm);
}

}  // namespace perfetto::trace_processor
