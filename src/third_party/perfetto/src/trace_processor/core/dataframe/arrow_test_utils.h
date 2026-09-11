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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_TEST_UTILS_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_TEST_UTILS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/arrow_deserializer.h"
#include "src/trace_processor/core/dataframe/arrow_serializer.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_processor::core::dataframe::arrow_test {

inline std::vector<uint8_t> Serialize(
    Dataframe& dataframe,
    const StringPool& pool,
    ArrowSerializer::IdColumnMode id_column_mode =
        ArrowSerializer::IdColumnMode::kOmit) {
  dataframe.Finalize();
  ArrowSerializer serializer(id_column_mode);
  auto prepared_size = serializer.Prepare(dataframe, pool);
  EXPECT_OK(prepared_size);
  if (!prepared_size.ok()) {
    return {};
  }
  size_t expected_size = *prepared_size;
  std::vector<uint8_t> output;
  base::Status status =
      serializer.Write(dataframe, pool, [&](const uint8_t* data, size_t size) {
        output.insert(output.end(), data, data + size);
        return base::OkStatus();
      });
  EXPECT_OK(status);
  EXPECT_EQ(output.size(), expected_size);
  return output;
}

inline util::TraceBlobViewReader MakeReader(const std::vector<uint8_t>& bytes,
                                            size_t chunk_size = 0) {
  util::TraceBlobViewReader reader;
  if (chunk_size == 0) {
    chunk_size = bytes.empty() ? 1 : bytes.size();
  }
  for (size_t i = 0; i < bytes.size(); i += chunk_size) {
    size_t length = std::min(chunk_size, bytes.size() - i);
    reader.PushBack(
        TraceBlobView(TraceBlob::CopyFrom(bytes.data() + i, length)));
  }
  return reader;
}

inline base::StatusOr<Dataframe> Deserialize(const std::vector<uint8_t>& bytes,
                                             StringPool* pool,
                                             const DataframeSpec& spec,
                                             size_t chunk_size = 0) {
  util::TraceBlobViewReader reader = MakeReader(bytes, chunk_size);
  return DeserializeFromArrow(reader, pool, spec);
}

template <typename Spec, typename... Values>
Dataframe MakeDataframe(const Spec& spec,
                        StringPool* pool,
                        Values&&... values) {
  Dataframe dataframe = Dataframe::CreateFromTypedSpec(spec, pool);
  (dataframe.InsertUnchecked(spec, std::monostate{},
                             std::forward<Values>(values)),
   ...);
  return dataframe;
}

template <typename Spec>
Dataframe RoundTrip(const Spec& spec, Dataframe& source, StringPool* pool) {
  std::vector<uint8_t> bytes = Serialize(source, *pool);
  auto result = Deserialize(bytes, pool, source.CreateSpec());
  EXPECT_OK(result);
  return result.ok() ? std::move(*result)
                     : Dataframe::CreateFromTypedSpec(spec, pool);
}

}  // namespace perfetto::trace_processor::core::dataframe::arrow_test

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_TEST_UTILS_H_
