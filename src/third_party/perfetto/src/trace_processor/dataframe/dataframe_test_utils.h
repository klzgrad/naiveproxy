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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_DATAFRAME_TEST_UTILS_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_DATAFRAME_TEST_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/dataframe/cursor.h"
#include "src/trace_processor/dataframe/cursor_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/dataframe/value_fetcher.h"

namespace perfetto::trace_processor::dataframe {

struct TestRowFetcher : ValueFetcher {
  using Value = std::variant<std::nullopt_t, int64_t, double, const char*>;
  enum Type : uint8_t {
    kNull,
    kInt64,
    kDouble,
    kString,
  };

  void SetRow(const std::vector<Value>& row_data) { current_row_ = &row_data; }

  Type GetValueType(uint32_t index) {
    PERFETTO_CHECK(current_row_ && index < current_row_->size());
    const auto& var = (*current_row_)[index];
    if (std::holds_alternative<std::nullopt_t>(var))
      return Type::kNull;
    if (std::holds_alternative<int64_t>(var))
      return Type::kInt64;
    if (std::holds_alternative<double>(var))
      return Type::kDouble;
    if (std::holds_alternative<const char*>(var))
      return Type::kString;
    PERFETTO_FATAL("Invalid variant state");
  }

  int64_t GetInt64Value(uint32_t index) {
    PERFETTO_CHECK(current_row_ && index < current_row_->size());
    return std::get<int64_t>((*current_row_)[index]);
  }

  double GetDoubleValue(uint32_t index) {
    PERFETTO_CHECK(current_row_ && index < current_row_->size());
    return std::get<double>((*current_row_)[index]);
  }

  const char* GetStringValue(uint32_t index) {
    PERFETTO_CHECK(current_row_ && index < current_row_->size());
    return std::get<const char*>((*current_row_)[index]);
  }
  static bool IteratorInit(uint32_t) { PERFETTO_FATAL("Unsupported"); }
  static bool IteratorNext(uint32_t) { PERFETTO_FATAL("Unsupported"); }

 private:
  const std::vector<Value>* current_row_ = nullptr;
};

// Callback for verifying cell values from the cursor.
// Stores visited values in variants for later checking.
struct ValueVerifier : CellCallback {
  using ValueVariant = std::variant<uint32_t,
                                    int32_t,
                                    int64_t,
                                    double,
                                    NullTermStringView,
                                    std::nullptr_t>;

  template <typename Fetcher>
  void Fetch(Cursor<Fetcher>* cursor, uint32_t col_count) {
    for (uint32_t i = 0; i < col_count; ++i) {
      cursor->Cell(i, *this);
    }
  }
  void OnCell(int64_t value) { values.emplace_back(value); }
  void OnCell(double value) { values.emplace_back(value); }
  void OnCell(NullTermStringView value) { values.emplace_back(value); }
  void OnCell(std::nullptr_t) { values.emplace_back(nullptr); }
  void OnCell(int32_t value) { values.emplace_back(value); }
  void OnCell(uint32_t value) { values.emplace_back(value); }

  std::vector<ValueVariant> values;
};

inline void VerifyData(
    Dataframe& df,
    uint64_t cols_bitmap,
    const std::vector<std::vector<ValueVerifier::ValueVariant>>& expected) {
  std::vector<FilterSpec> filter_specs;
  auto num_cols_selected =
      static_cast<uint32_t>(PERFETTO_POPCOUNT(cols_bitmap));
  ASSERT_OK_AND_ASSIGN(auto plan,
                       df.PlanQuery(filter_specs, {}, {}, {}, cols_bitmap));

  // Heap allocate to avoid potential stack overflows due to large cursor
  // object.
  auto cursor = std::make_unique<Cursor<TestRowFetcher>>();
  df.PrepareCursor(std::move(plan), *cursor);

  TestRowFetcher fetcher;
  cursor->Execute(fetcher);

  size_t row_index = 0;
  for (const auto& row : expected) {
    ValueVerifier verifier;
    ASSERT_FALSE(cursor->Eof()) << "Cursor finished early at row " << row_index;
    verifier.Fetch(&*cursor, num_cols_selected);
    EXPECT_THAT(verifier.values, testing::ElementsAreArray(row))
        << "Mismatch in data for row " << row_index;
    cursor->Next();
    row_index++;
  }
  ASSERT_TRUE(cursor->Eof()) << "Cursor has more rows than expected. Expected "
                             << expected.size() << " rows.";
  ASSERT_EQ(row_index, expected.size())
      << "Mismatch in number of rows processed.";
}

template <typename... Args>
std::vector<ValueVerifier::ValueVariant> Row(Args&&... args) {
  return std::vector<ValueVerifier::ValueVariant>{
      ValueVerifier::ValueVariant(std::forward<Args>(args))...};
}

template <typename... Args>
std::vector<std::vector<ValueVerifier::ValueVariant>> Rows(Args&&... rows) {
  return std::vector<std::vector<ValueVerifier::ValueVariant>>{
      std::forward<Args>(rows)...};
}

}  // namespace perfetto::trace_processor::dataframe

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_DATAFRAME_TEST_UTILS_H_
