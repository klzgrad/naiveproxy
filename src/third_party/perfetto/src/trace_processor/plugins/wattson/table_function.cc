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

#include "src/trace_processor/plugins/wattson/table_function.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/util/gzip_utils.h"

namespace perfetto::trace_processor::wattson {
namespace {

using core::dataframe::AdhocColumnType;
using core::dataframe::AdhocDataframeBuilder;
using core::dataframe::ColumnSpec;
using core::dataframe::Dataframe;
using core::dataframe::DataframeSpec;

static_assert(PERFETTO_IS_LITTLE_ENDIAN(),
              "wattson curve blobs are little-endian and require a "
              "little-endian host");

class WattsonCurvesBlobReader {
 public:
  explicit WattsonCurvesBlobReader(const std::vector<uint8_t>& bytes)
      : data_(bytes.data()), size_(bytes.size()) {}

  template <typename T>
  T Read() {
    static_assert(std::is_trivially_copyable_v<T>);
    T value;
    ReadInto(&value, sizeof(T));
    return value;
  }

  base::StringView ReadStringView(uint16_t size) {
    const uint8_t* data = ReadBytes(size);
    return base::StringView(reinterpret_cast<const char*>(data), size);
  }

  void CheckAvailable(uint64_t size) const {
    PERFETTO_CHECK(size <= size_ - offset_);
  }

  void CheckFullyRead() const { PERFETTO_CHECK(offset_ == size_); }

 private:
  const uint8_t* ReadBytes(size_t size) {
    CheckAvailable(size);
    const uint8_t* data = data_ + offset_;
    offset_ += size;
    return data;
  }

  void ReadInto(void* dest, size_t size) {
    const uint8_t* data = ReadBytes(size);
    memcpy(dest, data, size);
  }

  const uint8_t* data_;
  size_t size_;
  size_t offset_ = 0;
};

// Maps the column's declared StorageType to the AdhocDataframeBuilder type.
// Throws (CHECK) on unsupported types.
AdhocColumnType BuilderTypeFor(const ColumnSpec& cs) {
  if (cs.type.index() == core::StorageType::GetTypeIndex<core::Int64>()) {
    return AdhocColumnType::kInt64;
  }
  if (cs.type.index() == core::StorageType::GetTypeIndex<core::Double>()) {
    return AdhocColumnType::kDouble;
  }
  if (cs.type.index() == core::StorageType::GetTypeIndex<core::String>()) {
    return AdhocColumnType::kString;
  }
  PERFETTO_FATAL("wattson curves: unsupported column type at index %zu",
                 static_cast<size_t>(cs.type.index()));
}

}  // namespace

class WattsonCurvesTableFunction::Cursor : public StaticTableFunction::Cursor {
 public:
  explicit Cursor(WattsonCurvesTableFunction* fn) : fn_(fn) {}
  ~Cursor() override;

  bool Run(const std::vector<SqlValue>& arguments) override {
    PERFETTO_DCHECK(arguments.empty());
    auto df_or = fn_->BuildDataframe();
    if (!df_or.ok()) {
      return OnFailure(df_or.status());
    }
    owned_dataframe_ = std::move(df_or.value());
    return OnSuccess(&*owned_dataframe_);
  }

 private:
  WattsonCurvesTableFunction* fn_;
  // The framework holds the pointer returned by OnSuccess until the next
  // Run(), so the Cursor must own the Dataframe; the optional is overwritten
  // on every call (no caching).
  std::optional<Dataframe> owned_dataframe_;
};

WattsonCurvesTableFunction::Cursor::~Cursor() = default;

WattsonCurvesTableFunction::WattsonCurvesTableFunction(
    StringPool* pool,
    std::string table_name,
    DataframeSpec spec,
    const uint8_t* compressed_blob,
    size_t compressed_blob_size)
    : pool_(pool),
      table_name_(std::move(table_name)),
      spec_(std::move(spec)),
      compressed_blob_(compressed_blob),
      compressed_blob_size_(compressed_blob_size) {}

WattsonCurvesTableFunction::~WattsonCurvesTableFunction() = default;

std::unique_ptr<StaticTableFunction::Cursor>
WattsonCurvesTableFunction::MakeCursor() {
  return std::make_unique<Cursor>(this);
}

DataframeSpec WattsonCurvesTableFunction::CreateSpec() {
  return spec_;
}

std::string WattsonCurvesTableFunction::TableName() {
  return table_name_;
}

uint32_t WattsonCurvesTableFunction::GetArgumentCount() const {
  return 0;
}

base::StatusOr<Dataframe> WattsonCurvesTableFunction::BuildDataframe() const {
  std::vector<uint8_t> bytes = util::GzipDecompressor::DecompressFully(
      compressed_blob_, compressed_blob_size_);
  WattsonCurvesBlobReader reader(bytes);
  uint32_t row_count = reader.Read<uint32_t>();
  uint32_t string_count = reader.Read<uint32_t>();

  // String table: uint16 length + bytes for each entry. Intern into the pool
  // so the dataframe can hold StringPool::Ids.
  std::vector<StringPool::Id> string_ids;
  string_ids.reserve(string_count);
  for (uint32_t i = 0; i < string_count; ++i) {
    uint16_t len = reader.Read<uint16_t>();
    string_ids.push_back(pool_->InternString(reader.ReadStringView(len)));
  }

  std::vector<AdhocColumnType> types;
  types.reserve(spec_.column_specs.size());
  for (const ColumnSpec& cs : spec_.column_specs) {
    types.push_back(BuilderTypeFor(cs));
  }

  AdhocDataframeBuilder::Options opts;
  opts.types = types;
  // The StaticTableFunctionModule synthesizes its own HIDDEN _auto_id PRIMARY
  // KEY when declaring the SQLite vtab, so the builder must not add one too.
  opts.emit_auto_id = false;
  AdhocDataframeBuilder builder(spec_.column_names, pool_, opts);

  // Decode each column in schema order, in the same column-oriented layout
  // produced by `gen_wattson_curves.py`.
  for (uint32_t col_idx = 0; col_idx < types.size(); ++col_idx) {
    switch (types[col_idx]) {
      case AdhocColumnType::kString: {
        reader.CheckAvailable(uint64_t{4} * row_count);
        for (uint32_t r = 0; r < row_count; ++r) {
          uint32_t idx = reader.Read<uint32_t>();
          PERFETTO_CHECK(idx < string_ids.size());
          builder.PushNonNullUnchecked(col_idx, string_ids[idx]);
        }
        break;
      }
      case AdhocColumnType::kInt64: {
        reader.CheckAvailable(uint64_t{8} * row_count);
        for (uint32_t r = 0; r < row_count; ++r) {
          builder.PushNonNullUnchecked(col_idx, reader.Read<int64_t>());
        }
        break;
      }
      case AdhocColumnType::kDouble: {
        reader.CheckAvailable(uint64_t{8} * row_count);
        for (uint32_t r = 0; r < row_count; ++r) {
          builder.PushNonNullUnchecked(col_idx, reader.Read<double>());
        }
        break;
      }
    }
  }

  reader.CheckFullyRead();

  return std::move(builder).Build();
}

}  // namespace perfetto::trace_processor::wattson
