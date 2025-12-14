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

#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_tokenizer.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_parser.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/third_party/simpleperf/cmd_report_sample.pbzero.h"

namespace perfetto::trace_processor::simpleperf_proto_importer {

namespace {
constexpr char kSimpleperfMagic[] = "SIMPLEPERF";
constexpr size_t kSimpleperfMagicSize = 10;
constexpr size_t kVersionSize = 2;
constexpr size_t kRecordSizeSize = 4;
}  // namespace

SimpleperfProtoTokenizer::SimpleperfProtoTokenizer(
    TraceProcessorContext* context)
    : context_(context),
      stream_(context->sorter->CreateStream(
          std::make_unique<SimpleperfProtoParser>(context, &tracker_))) {}

SimpleperfProtoTokenizer::~SimpleperfProtoTokenizer() = default;

base::Status SimpleperfProtoTokenizer::Parse(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));

  for (;;) {
    switch (state_) {
      case State::kExpectingMagic:
        RETURN_IF_ERROR(ParseMagic());
        break;

      case State::kExpectingVersion:
        RETURN_IF_ERROR(ParseVersion());
        break;

      case State::kExpectingRecordSize:
        RETURN_IF_ERROR(ParseRecordSize());
        break;

      case State::kExpectingRecord:
        RETURN_IF_ERROR(ParseRecord());
        break;

      case State::kFinished:
        return base::OkStatus();
    }
  }
}

base::Status SimpleperfProtoTokenizer::NotifyEndOfFile() {
  if (state_ != State::kFinished) {
    return base::ErrStatus("Unexpected end of simpleperf_proto file");
  }
  return base::OkStatus();
}

base::Status SimpleperfProtoTokenizer::ParseMagic() {
  auto iter = reader_.GetIterator();
  auto magic_data = iter.MaybeRead(kSimpleperfMagicSize);
  if (!magic_data) {
    return base::ErrStatus("Need more data");
  }

  if (std::memcmp(magic_data->data(), kSimpleperfMagic, kSimpleperfMagicSize) !=
      0) {
    return base::ErrStatus("Invalid simpleperf magic header");
  }

  reader_.PopFrontUntil(iter.file_offset());
  state_ = State::kExpectingVersion;
  return base::OkStatus();
}

base::Status SimpleperfProtoTokenizer::ParseVersion() {
  auto iter = reader_.GetIterator();
  auto version_data = iter.MaybeRead(kVersionSize);
  if (!version_data) {
    return base::ErrStatus("Need more data");
  }

  uint16_t version = *reinterpret_cast<const uint16_t*>(version_data->data());
  if (version != 1) {
    return base::ErrStatus("Unsupported simpleperf version: %d", version);
  }

  reader_.PopFrontUntil(iter.file_offset());
  state_ = State::kExpectingRecordSize;
  return base::OkStatus();
}

base::Status SimpleperfProtoTokenizer::ParseRecordSize() {
  auto iter = reader_.GetIterator();
  auto size_data = iter.MaybeRead(kRecordSizeSize);
  if (!size_data) {
    return base::ErrStatus("Need more data");
  }

  current_record_size_ = *reinterpret_cast<const uint32_t*>(size_data->data());

  reader_.PopFrontUntil(iter.file_offset());
  if (current_record_size_ == 0) {
    // End of records marker
    state_ = State::kFinished;
    return base::OkStatus();
  }

  state_ = State::kExpectingRecord;
  return base::OkStatus();
}

base::Status SimpleperfProtoTokenizer::ParseRecord() {
  auto iter = reader_.GetIterator();
  auto record_data = iter.MaybeRead(current_record_size_);
  if (!record_data) {
    return base::ErrStatus("Need more data");
  }

  using namespace perfetto::third_party::simpleperf::proto::pbzero;
  Record::Decoder record(record_data->data(), record_data->size());

  // Process metadata records directly in tokenizer (not sent to sorter)
  if (record.has_file()) {
    File::Decoder file(record.file());
    uint32_t file_id = file.id();

    // Create mapping for this file
    DummyMemoryMapping* mapping = nullptr;
    if (file.has_path()) {
      std::string path_str(file.path().data, file.path().size);
      mapping = &context_->mapping_tracker->CreateDummyMapping(path_str);
    } else {
      mapping = &context_->mapping_tracker->CreateDummyMapping("");
    }
    tracker_.AddFileMapping(file_id, mapping);

    // Store symbol table for this file
    std::vector<StringId> symbols;
    for (auto it = file.symbol(); it; ++it) {
      protozero::ConstChars symbol_chars = *it;
      base::StringView symbol_view(symbol_chars.data, symbol_chars.size);
      symbols.push_back(context_->storage->InternString(symbol_view));
    }
    tracker_.AddSymbolTable(file_id, std::move(symbols));

    reader_.PopFrontUntil(iter.file_offset());
    state_ = State::kExpectingRecordSize;
    return base::OkStatus();
  }

  if (record.has_meta_info()) {
    MetaInfo::Decoder meta(record.meta_info());

    // Store event types
    for (auto it = meta.event_type(); it; ++it) {
      protozero::ConstChars event_type_chars = *it;
      base::StringView event_type_view(event_type_chars.data,
                                       event_type_chars.size);
      tracker_.AddEventType(context_->storage->InternString(event_type_view));
    }

    reader_.PopFrontUntil(iter.file_offset());
    state_ = State::kExpectingRecordSize;
    return base::OkStatus();
  }

  if (record.has_lost()) {
    // TODO(lalitm): Process LostSituation record. This contains:
    // - sample_count: Total number of samples collected
    // - lost_count: Number of samples lost due to buffer overflow
    // Should emit a track event or stat to indicate data loss occurred.
    reader_.PopFrontUntil(iter.file_offset());
    state_ = State::kExpectingRecordSize;
    return base::OkStatus();
  }

  // Process timestamped records and Thread records (push to sorter)
  int64_t ts = 0;
  if (record.has_sample()) {
    Sample::Decoder sample(record.sample());
    if (sample.has_time()) {
      ts = static_cast<int64_t>(sample.time());
      last_seen_timestamp_ = ts;
    }
  } else if (record.has_context_switch()) {
    ContextSwitch::Decoder cs(record.context_switch());
    if (cs.has_time()) {
      ts = static_cast<int64_t>(cs.time());
      last_seen_timestamp_ = ts;
    }
  } else if (record.has_thread()) {
    // Thread records use the timestamp of the most recent Sample/ContextSwitch
    ts = last_seen_timestamp_;
  }

  // Create event with the record data and push to sorter
  SimpleperfProtoEvent event;
  event.ts = ts;
  event.record_data = std::move(*record_data);
  stream_->Push(ts, std::move(event));

  reader_.PopFrontUntil(iter.file_offset());
  state_ = State::kExpectingRecordSize;
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::simpleperf_proto_importer
