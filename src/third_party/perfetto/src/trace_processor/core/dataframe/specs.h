#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_SPECS_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_SPECS_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/duplicate_types.h"
#include "src/trace_processor/core/common/null_types.h"
#include "src/trace_processor/core/common/op_types.h"
#include "src/trace_processor/core/common/sort_types.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/common/value_fetcher.h"
#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::core::dataframe {

// -----------------------------------------------------------------------------
// Export types from core namespace for ergomics.
// -----------------------------------------------------------------------------

// Storage types
using core::Double;
using core::Id;
using core::Int32;
using core::Int64;
using core::StorageType;
using core::String;
using core::Uint32;

// Operation types
using core::Eq;
using core::Ge;
using core::Glob;
using core::Gt;
using core::In;
using core::IsNotNull;
using core::IsNull;
using core::Le;
using core::Lt;
using core::Ne;
using core::Op;
using core::Regex;

// Nullability types
using core::DenseNull;
using core::NonNull;
using core::Nullability;
using core::SparseNull;
using core::SparseNullWithPopcountAlways;
using core::SparseNullWithPopcountUntilFinalization;

// Sort types
using core::IdSorted;
using core::SetIdSorted;
using core::SortDirection;
using core::Sorted;
using core::SortState;
using core::Unsorted;

// Duplicate types
using core::DuplicateState;
using core::HasDuplicates;
using core::NoDuplicates;

// Value fetcher.
using core::ErrorValueFetcher;
using core::ValueFetcher;

// -----------------------------------------------------------------------------
// Filter Specifications
// -----------------------------------------------------------------------------

// Specifies a filter operation to be applied to column data.
// This is used to generate query plans for filtering rows.
struct FilterSpec {
  // Index of the column in the dataframe to filter.
  uint32_t col;

  // Original index from the client query (used for tracking).
  uint32_t source_index;

  // Operation to apply (e.g., equality).
  Op op;

  // Output parameter: index for the filter value in query execution.
  // This is populated during query planning.
  std::optional<uint32_t> value_index;
};

// -----------------------------------------------------------------------------
// Distinct Specifications
// -----------------------------------------------------------------------------

// Specifies a distinct operation to be applied to the dataframe rows.
struct DistinctSpec {
  // Index of the column in the dataframe to perform a distinct on.
  uint32_t col;
};

// -----------------------------------------------------------------------------
// Sort Specifications
// -----------------------------------------------------------------------------

// Specifies a sort operation to be applied to the dataframe rows.
struct SortSpec {
  // Index of the column in the dataframe to sort by.
  uint32_t col;

  // Direction of the sort (ascending or descending).
  SortDirection direction;
};

// -----------------------------------------------------------------------------
// Limit Specification
// -----------------------------------------------------------------------------

// Specifies limit and offset parameters for a query.
struct LimitSpec {
  std::optional<uint32_t> limit;
  std::optional<uint32_t> offset;
};

// -----------------------------------------------------------------------------
// Dataframe and Column Specifications
// -----------------------------------------------------------------------------

// Defines the properties of a column in the dataframe.
struct ColumnSpec {
  StorageType type;
  Nullability nullability;
  SortState sort_state;
  DuplicateState duplicate_state;
};

// Defines the properties of the dataframe.
struct DataframeSpec {
  std::vector<std::string> column_names;
  std::vector<ColumnSpec> column_specs;
};

// Same as ColumnSpec but for cases where the spec is known at compile time.
template <typename T, typename N, typename S, typename D>
struct TypedColumnSpec {
 public:
  using type = T;
  using null_storage_type = N;
  using sort_state = S;
  using duplicate_state = D;
  ColumnSpec spec;

  // Inferred properties from the above.
  using mutate_variant = std::variant<std::monostate,
                                      uint32_t,
                                      int32_t,
                                      int64_t,
                                      double,
                                      StringPool::Id>;
  using non_null_mutate_type =
      StorageType::VariantTypeAtIndex<T, mutate_variant>;
  using mutate_type = std::conditional_t<std::is_same_v<N, NonNull>,
                                         non_null_mutate_type,
                                         std::optional<non_null_mutate_type>>;
};

// Same as Spec but for cases where the spec is known at compile time.
template <typename... C>
struct TypedDataframeSpec {
  static constexpr uint32_t kColumnCount = sizeof...(C);
  using columns = std::tuple<C...>;
  using mutate_types = std::tuple<typename C::mutate_type...>;

  template <size_t I>
  using column_spec = typename std::tuple_element_t<I, columns>;

  static_assert(kColumnCount > 0,
                "TypedSpec must have at least one column type");

  // Converts the typed spec to a untyped DataframeSpec.
  DataframeSpec ToUntypedDataframeSpec() const {
    DataframeSpec spec;
    spec.column_names.reserve(kColumnCount);
    spec.column_specs.reserve(kColumnCount);
    for (size_t i = 0; i < kColumnCount; ++i) {
      spec.column_names.push_back(column_names[i]);
      spec.column_specs.push_back(column_specs[i]);
    }
    return spec;
  }

  std::array<const char*, kColumnCount> column_names;
  std::array<ColumnSpec, kColumnCount> column_specs;
};

template <typename... C>
static constexpr TypedDataframeSpec<C...> CreateTypedDataframeSpec(
    std::array<const char*, sizeof...(C)> _column_names,
    C... _columns) {
  return TypedDataframeSpec<C...>{_column_names, {_columns.spec...}};
}

template <typename T, typename N, typename S, typename D = HasDuplicates>
static constexpr TypedColumnSpec<T, N, S, D> CreateTypedColumnSpec(T,
                                                                   N,
                                                                   S,
                                                                   D = D{}) {
  return TypedColumnSpec<T, N, S, D>{ColumnSpec{T{}, N{}, S{}, D{}}};
}

}  // namespace perfetto::trace_processor::core::dataframe

namespace perfetto::trace_processor {

// Namespace alias for ergonomics.
namespace dataframe = core::dataframe;

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_SPECS_H_
