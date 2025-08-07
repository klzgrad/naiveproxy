#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_SPECS_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_SPECS_H_

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
#include "src/trace_processor/dataframe/type_set.h"

namespace perfetto::trace_processor::dataframe {

// -----------------------------------------------------------------------------
// Column value Types
// -----------------------------------------------------------------------------

// Represents values where the index of the value in the table is the same as
// the value. This allows for zero memory overhead as values don't need to be
// explicitly stored. Operations on column with this type can be highly
// optimized.
struct Id {};

// Represents values where the value is a 32-bit unsigned integer.
struct Uint32 {};

// Represents values where the value is a 32-bit signed integer.
struct Int32 {};

// Represents values where the value is a 64-bit signed integer.
struct Int64 {};

// Represents values where the value is a double.
struct Double {};

// Represents values where the value is a string.
struct String {};

// TypeSet of all possible storage value types.
using StorageType = TypeSet<Id, Uint32, Int32, Int64, Double, String>;

// -----------------------------------------------------------------------------
// Operation Types
// -----------------------------------------------------------------------------

// Filters only cells which compare equal to the given value.
struct Eq {};

// Filters only cells which do not compare equal to the given value.
struct Ne {};

// Filters only cells which are less than the given value.
struct Lt {};

// Filters only cells which are less than or equal to the given value.
struct Le {};

// Filters only cells which are greater than the given value.
struct Gt {};

// Filters only cells which are greater than or equal to the given value.
struct Ge {};

// Filters only cells which match the given glob pattern.
struct Glob {};

// Filters only cells which match the given regex pattern.
struct Regex {};

// Filters only cells which are not NULL.
struct IsNotNull {};

// Filters only cells which are NULL.
struct IsNull {};

// Filters only cells which are part of the provided list of values.
struct In {};

// TypeSet of all possible operations for filter conditions.
using Op = TypeSet<Eq, Ne, Lt, Le, Gt, Ge, Glob, Regex, IsNotNull, IsNull, In>;

// -----------------------------------------------------------------------------
// Sort State Types
// -----------------------------------------------------------------------------

// Represents a column sorted by its id property.
// This is a special state that should only be applied to Id columns, indicating
// the natural ordering where indices equal values.
struct IdSorted {};

// Represents a column which has two properties:
// 1) is sorted in ascending order
// 2) for each unique value `v` in the column, the first occurrence of `v` is
//    at index `v` in the column.
//
// In essence, this means that the columns end up looking like:
// [0, 0, 0, 3, 3, 5, 5, 7, 7, 7, 10]
//
// This state can only be applied to Uint32 columns.
struct SetIdSorted {};

// Represents a column which is sorted in ascending order by its value.
struct Sorted {};

// Represents a column which is not sorted.
struct Unsorted {};

// TypeSet of all possible column sort states.
using SortState = TypeSet<IdSorted, SetIdSorted, Sorted, Unsorted>;

// -----------------------------------------------------------------------------
// Nullability Types
// -----------------------------------------------------------------------------

// Represents a column that doesn't contain NULL values.
struct NonNull {};

// Represents a column that contains NULL values with the storage only
// containing data for non-NULL values.
struct SparseNull {};

// Represents a column that contains NULL values with the storage only
// containing data for non-NULL values while still needing to access the
// non-null values in O(1) time at any time.
struct SparseNullWithPopcountAlways {};

// Represents a column that contains NULL values with the storage only
// containing data for non-NULL values while still needing to access the
// non-null values in O(1) time only until the dataframe is finalized.
struct SparseNullWithPopcountUntilFinalization {};

// Represents a column that contains NULL values with the storage containing
// data for all values (with undefined values at positions that would be NULL).
struct DenseNull {};

// TypeSet of all possible column nullability states.
using Nullability = TypeSet<NonNull,
                            SparseNull,
                            SparseNullWithPopcountAlways,
                            SparseNullWithPopcountUntilFinalization,
                            DenseNull>;

// -----------------------------------------------------------------------------
// Duplicate State Types
// -----------------------------------------------------------------------------

// Represents a column that is known to have no duplicate values.
struct NoDuplicates {};

// Represents a column that may or does contain duplicate values.
// This should be the default/conservative assumption.
struct HasDuplicates {};

// TypeSet of all possible column duplicate states.
using DuplicateState = TypeSet<NoDuplicates, HasDuplicates>;

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

// Defines the direction for sorting.
enum class SortDirection : uint32_t {
  kAscending,
  kDescending,
};

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

}  // namespace perfetto::trace_processor::dataframe

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_SPECS_H_
