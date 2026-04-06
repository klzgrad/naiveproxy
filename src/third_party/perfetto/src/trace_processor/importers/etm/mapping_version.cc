/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/etm/mapping_version.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "src/trace_processor/importers/common/address_range.h"

namespace perfetto::trace_processor::etm {

MappingVersion::MappingVersion(MappingId id,
                               int64_t create_ts,
                               AddressRange range,
                               std::optional<TraceBlob> content)
    : id_(id),
      create_ts_(create_ts),
      range_(range),
      content_(content ? std::move(*content) : TraceBlob::Allocate(0)) {
  PERFETTO_CHECK(content_.size() == range.length() || content_.size() == 0);
}

MappingVersion MappingVersion::SplitFront(uint64_t mid) {
  PERFETTO_CHECK(range_.start() < mid && mid < range_.end());
  AddressRange head(range_.start(), mid);
  AddressRange tail(mid, range_.end());
  uint64_t offset = mid - range_.start();

  std::optional<TraceBlob> head_content;
  std::optional<TraceBlob> tail_content;
  if (content_.size() != 0) {
    head_content = TraceBlob::CopyFrom(content_.data(), offset);
    tail_content =
        TraceBlob::CopyFrom(content_.data() + offset, content_.size() - offset);
  }

  range_ = tail;
  content_ = tail_content ? std::move(*tail_content) : TraceBlob::Allocate(0);

  return MappingVersion(id_, create_ts_, head, std::move(head_content));
}

}  // namespace perfetto::trace_processor::etm
