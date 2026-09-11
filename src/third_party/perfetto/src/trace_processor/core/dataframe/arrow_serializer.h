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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_SERIALIZER_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_SERIALIZER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/util/flex_vector.h"

namespace perfetto::trace_processor {
class StringPool;
}

namespace perfetto::trace_processor::core::dataframe {

// Serializes a Dataframe as a standard Arrow file containing one record batch.
// The output can be read by full Arrow implementations such as PyArrow.
//
// Sparse columns are expanded to Arrow's dense logical layout. String columns
// are dictionary encoded, with the values written as a dictionary batch ahead
// of the record batch. Input dataframes must be finalized so their column
// storage remains stable across the Prepare() and Write() passes.
class ArrowSerializer {
 public:
  enum class IdColumnMode {
    // Dataframe Id columns are implicit row numbers. Omit them when the reader
    // will reconstruct them from the record-batch row count.
    kOmit,
    // Materialize Id columns as Arrow uint32 arrays for external consumers.
    kInclude,
  };

  using WriteFn = std::function<base::Status(const uint8_t*, size_t)>;

  explicit ArrowSerializer(IdColumnMode = IdColumnMode::kOmit);
  ~ArrowSerializer();

  // Computes Arrow buffer metadata and the exact output size for a finalized
  // dataframe. Arrow places buffer lengths before buffer contents, so
  // variable-width strings must be measured here before Write() can stream the
  // file. This retains only metadata; it does not materialize column contents.
  base::StatusOr<size_t> Prepare(const Dataframe&, const StringPool&);

  // Streams the dataframe prepared by the preceding Prepare() call using
  // bounded scratch memory. Column contents are not materialized as a complete
  // Arrow body. The dataframe and string pool must not change between calls.
  base::Status Write(const Dataframe&, const StringPool&, const WriteFn&);

 private:
  struct BodyPlan;
  struct DictionaryPlan;
  struct PreparedColumn {
    uint32_t dataframe_column;
    std::string name;
    bool nullable;
    StorageType storage_type;
    uint32_t dictionary = 0;
  };

  void Reset();
  base::Status PlanBody(const Dataframe&, const StringPool&, BodyPlan*);
  base::Status PlanDictionary(uint32_t rows,
                              const Column&,
                              const StringPool&,
                              const BitVector*,
                              bool sparse);
  base::Status BuildFileFraming(const BodyPlan&);
  base::Status WriteDictionary(const DictionaryPlan&,
                               const StringPool&,
                               const WriteFn&);
  base::Status WriteDictionaryIndices(uint32_t rows,
                                      const Column&,
                                      bool sparse,
                                      const BitVector*,
                                      const DictionaryPlan&,
                                      const WriteFn&);
  base::Status WriteBody(const Dataframe&, const WriteFn&);

  std::vector<uint8_t> header_;
  std::vector<uint8_t> batch_header_;
  std::vector<uint8_t> trailer_;
  std::vector<PreparedColumn> prepared_columns_;
  std::vector<DictionaryPlan> dictionaries_;
  FlexVector<uint8_t> scratch_;

  const Dataframe* prepared_dataframe_ = nullptr;
  const StringPool* prepared_string_pool_ = nullptr;
  uint64_t prepared_mutations_ = 0;
  IdColumnMode id_column_mode_;
};

}  // namespace perfetto::trace_processor::core::dataframe

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_SERIALIZER_H_
