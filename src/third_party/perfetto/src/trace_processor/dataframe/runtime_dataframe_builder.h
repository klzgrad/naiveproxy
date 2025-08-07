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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_RUNTIME_DATAFRAME_BUILDER_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_RUNTIME_DATAFRAME_BUILDER_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/value_fetcher.h"

namespace perfetto::trace_processor::dataframe {

// Builds a Dataframe instance row by row at runtime.
//
// This class allows constructing a `Dataframe` incrementally. It infers
// column types (`int64_t`, `double`, `StringPool::Id`) based on the first
// non-null value encountered in each column. Null values are tracked
// efficiently using a `BitVector` (created only if nulls exist), and the
// underlying data storage only stores non-null values (SparseNull
// representation).
//
// Upon calling `Build()`, the builder analyzes the collected data to:
// - Determine the final optimal storage type for integer columns (downcasting
//   `int64_t` to `uint32_t` or `int32_t` if possible, or using `Id` type).
// - Determine the final sort state (`IdSorted`, `SetIdSorted`, `Sorted`,
//   `Unsorted`) by analyzing the collected values. Nullable columns are always
//   `Unsorted`.
// - Construct the final `Dataframe` object.
//
// Usage Example:
// ```cpp
// // Assume MyFetcher inherits from ValueFetcher and provides data for rows.
// struct MyFetcher : ValueFetcher {
//   // ... implementation to fetch data for current row ...
// };
//
// std::vector<std::string> col_names = {"ts", "value", "name"};
// StringPool pool;
// RuntimeDataframeBuilder builder(col_names, &pool);
// for (MyFetcher fetcher; fetcher.Next();) {
//   if (!builder.AddRow(&fetcher)) {
//     // Handle error (e.g., type mismatch)
//     PERFETTO_ELOG("Failed to add row: %s", builder.status().message());
//     break;
//   }
// }
//
// base::StatusOr<Dataframe> df = std::move(builder).Build();
// if (!df.ok()) {
//   // Handle build error
//   PERFETTO_ELOG("Failed to build dataframe: %s", df.status().message());
// } else {
//   // Use the dataframe *df...
// }
// ```
class RuntimeDataframeBuilder {
 public:
  // Constructs a RuntimeDataframeBuilder.
  //
  // Args:
  //   names: A vector of strings representing the names of the columns
  //          to be built. The order determines the column order as well.
  //   pool: A pointer to a `StringPool` instance used for interning
  //         string values encountered during row addition. Must remain
  //         valid for the lifetime of the builder and the resulting
  //         Dataframe.
  //  types: An optional vector of `ColumnType` specifying the types
  //         of the columns. If empty, types are inferred from the first
  //         non-null value added to each column. If provided, must match
  //         the size of `names`.
  RuntimeDataframeBuilder(
      std::vector<std::string> names,
      StringPool* pool,
      const std::vector<AdhocDataframeBuilder::ColumnType>& types = {})
      : coulumn_count_(static_cast<uint32_t>(names.size())),
        builder_(std::move(names), pool, types),
        pool_(pool) {}
  ~RuntimeDataframeBuilder() = default;

  // Movable but not copyable
  RuntimeDataframeBuilder(RuntimeDataframeBuilder&&) noexcept;
  RuntimeDataframeBuilder& operator=(RuntimeDataframeBuilder&&) noexcept;
  RuntimeDataframeBuilder(const RuntimeDataframeBuilder&) = delete;
  RuntimeDataframeBuilder& operator=(const RuntimeDataframeBuilder&) = delete;

  // Adds a row to the dataframe using data provided by the Fetcher.
  //
  // Template Args:
  //   ValueFetcherImpl: A concrete class derived from `ValueFetcher` that
  //                     provides methods like `GetValueType(col_idx)` and
  //                     `GetInt64Value(col_idx)`, `GetDoubleValue(col_idx)`,
  //                     `GetStringValue(col_idx)` for the current row.
  // Args:
  //   fetcher: A pointer to an instance of `ValueFetcherImpl`, configured
  //            to provide data for the row being added. The fetcher only
  //            needs to be valid for the duration of this call.
  // Returns:
  //   true: If the row was added successfully.
  //   false: If an error occurred (e.g., type mismatch). Check `status()` for
  //          details. The builder should not be used further if false is
  //          returned.
  //
  // Implementation Notes:
  // 1) Infers column types (int64_t, double, StringPool::Id) based on the first
  //    non-null value encountered. Stores integer types smaller than int64_t
  //    (i.e. Id, uint32_t, int32_t) initially as int64_t, with potential
  //    downcasting occurring during Build().
  // 2) Tracks null values sparsely: only non-null values are appended to the
  //    internal data storage vectors. A BitVector is created and maintained
  //    only if null values are encountered for a column.
  // 3) Performs strict type checking against the inferred type for subsequent
  //    rows. If a type mismatch occurs, sets an error status (retrievable via
  //    status()) and returns false.
  template <typename ValueFetcherImpl>
  bool AddRow(ValueFetcherImpl* fetcher) {
    static_assert(std::is_base_of_v<ValueFetcher, ValueFetcherImpl>,
                  "ValueFetcherImpl must inherit from ValueFetcher");
    PERFETTO_CHECK(status().ok());

    for (uint32_t i = 0; i < coulumn_count_; ++i) {
      typename ValueFetcherImpl::Type fetched_type = fetcher->GetValueType(i);
      switch (fetched_type) {
        case ValueFetcherImpl::kInt64:
          if (!builder_.PushNonNull(i, fetcher->GetInt64Value(i))) {
            return false;
          }
          break;
        case ValueFetcherImpl::kDouble:
          if (!builder_.PushNonNull(i, fetcher->GetDoubleValue(i))) {
            return false;
          }
          break;
        case ValueFetcherImpl::kString:
          if (!builder_.PushNonNull(
                  i, pool_->InternString(fetcher->GetStringValue(i)))) {
            return false;
          }
          break;
        case ValueFetcherImpl::kNull:
          builder_.PushNull(i);
          break;
      }
    }
    return true;
  }

  // Finalizes the builder and attempts to construct the Dataframe.
  // This method consumes the builder (note the && qualifier).
  //
  // Returns:
  //   StatusOr<Dataframe>: On success, contains the built `Dataframe`.
  //                        On failure (e.g., if `AddRow` previously failed),
  //                        contains an error status retrieved from `status()`.
  //
  // Implementation wise, the collected data for each column is analyzed to:
  // - Determine the final optimal storage type (e.g., downcasting int64_t to
  //   uint32_t/int32_t if possible, using Id type if applicable).
  // - Determine the final nullability overlay (NonNull or SparseNull).
  // - Determine the final sort state (IdSorted, SetIdSorted, Sorted, Unsorted)
  //   by analyzing the collected non-null values.
  // - Construct and return the final `Dataframe` instance.
  base::StatusOr<Dataframe> Build() && { return std::move(builder_).Build(); }

  // Returns the current status of the builder.
  //
  // If `AddRow` returned `false`, this method can be used to retrieve the
  // `base::Status` object containing the error details (e.g., type mismatch).
  //
  // Returns:
  //   const base::Status&: The current status. `ok()` will be true unless
  //                        an error occurred during a previous `AddRow` call.
  const base::Status& status() const { return builder_.status(); }

 private:
  uint32_t coulumn_count_ = 0;
  AdhocDataframeBuilder builder_;
  StringPool* pool_ = nullptr;
};

}  // namespace perfetto::trace_processor::dataframe

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_RUNTIME_DATAFRAME_BUILDER_H_
