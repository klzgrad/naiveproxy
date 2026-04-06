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

#include "src/trace_processor/importers/perf/spe_tokenizer.h"
#include "src/trace_processor/importers/perf/spe_record_parser.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/perf/aux_data_tokenizer.h"
#include "src/trace_processor/importers/perf/aux_record.h"
#include "src/trace_processor/importers/perf/itrace_start_record.h"
#include "src/trace_processor/importers/perf/spe.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor::perf_importer {
namespace {

class SpeStream : public AuxDataStream {
 public:
  explicit SpeStream(TraceProcessorContext* context, AuxStream* stream)
      : context_(context),
        stream_(*stream),
        record_stream_(context->sorter->CreateStream(
            std::make_unique<SpeRecordParserImpl>(context))) {}

  void OnDataLoss(uint64_t) override {
    // Clear any inflight parsing.
    buffer_.PopFrontUntil(buffer_.end_offset());
  }

  base::Status OnItraceStartRecord(ItraceStartRecord) override {
    // Clear any inflight parsing.
    buffer_.PopFrontUntil(buffer_.end_offset());
    return base::OkStatus();
  }

  base::Status Parse(AuxRecord aux, TraceBlobView data) override {
    last_aux_record_ = std::move(aux);
    buffer_.PushBack(std::move(data));
    while (ProcessRecord()) {
    }
    return base::OkStatus();
  }

  base::Status NotifyEndOfStream() override { return base::OkStatus(); }

 private:
  // A SPE trace is just a stream of SPE records which in turn are a collection
  // of packets. An End or Timestamp packet signals the end of the current
  // record. This method will read the stream until an end of record condition,
  // emit the record to the sorter, consume the bytes from the buffer, and
  // finally return true. If not enough data is available to parse a full record
  // it returns false and the internal buffer is not modified.
  bool ProcessRecord() {
    for (auto it = buffer_.GetIterator(); it;) {
      uint8_t byte_0 = *it;
      // Must be true (we passed the for loop condition).
      it.MaybeAdvance(1);

      if (spe::IsExtendedHeader(byte_0)) {
        if (!it) {
          return false;
        }
        uint8_t byte_1 = *it;
        uint8_t payload_size =
            spe::ExtendedHeader(byte_0, byte_1).GetPayloadSize();
        if (!it.MaybeAdvance(payload_size + 1)) {
          return false;
        }
        continue;
      }

      spe::ShortHeader short_header(byte_0);
      uint8_t payload_size = short_header.GetPayloadSize();
      if (!it.MaybeAdvance(payload_size)) {
        return false;
      }

      if (short_header.IsEndPacket()) {
        size_t record_len = it.file_offset() - buffer_.start_offset();
        TraceBlobView record =
            *buffer_.SliceOff(buffer_.start_offset(), record_len);
        buffer_.PopFrontUntil(it.file_offset());
        Emit(std::move(record), std::nullopt);
        return true;
      }

      if (short_header.IsTimestampPacket()) {
        size_t record_len = it.file_offset() - buffer_.start_offset();
        TraceBlobView record =
            *buffer_.SliceOff(buffer_.start_offset(), record_len);
        buffer_.PopFrontUntil(it.file_offset());
        Emit(std::move(record), ReadTimestamp(record));
        return true;
      }
    }
    return false;
  }

  uint64_t ReadTimestamp(const TraceBlobView& record) {
    PERFETTO_CHECK(record.size() >= 8);
    uint64_t timestamp;
    memcpy(&timestamp, record.data() + record.size() - 8, 8);
    return timestamp;
  }

  // Emits a record to the sorter. You can optionally pass the cycles value
  // contained in the timestamp packet which will be used to determine the trace
  // timestamp.
  void Emit(TraceBlobView record, std::optional<uint64_t> cycles) {
    PERFETTO_CHECK(last_aux_record_);

    std::optional<uint64_t> perf_time;

    if (cycles.has_value()) {
      perf_time = stream_.ConvertTscToPerfTime(*cycles);
    } else {
      context_->storage->IncrementStats(stats::spe_no_timestamp);
    }

    if (!perf_time && last_aux_record_->sample_id.has_value()) {
      perf_time = last_aux_record_->sample_id->time();
    }

    if (!perf_time) {
      record_stream_->Push(context_->sorter->max_timestamp(),
                           std::move(record));
      return;
    }

    std::optional<int64_t> trace_time = context_->clock_tracker->ToTraceTime(
        last_aux_record_->attr->clock_id(), static_cast<int64_t>(*perf_time));
    if (trace_time) {
      record_stream_->Push(*trace_time, std::move(record));
    }
  }

  TraceProcessorContext* const context_;
  AuxStream& stream_;
  util::TraceBlobViewReader buffer_;
  std::optional<AuxRecord> last_aux_record_;
  std::unique_ptr<TraceSorter::Stream<TraceBlobView>> record_stream_;
};

}  // namespace

SpeTokenizer::~SpeTokenizer() = default;
base::StatusOr<AuxDataStream*> SpeTokenizer::InitializeAuxDataStream(
    AuxStream* stream) {
  streams_.push_back(std::make_unique<SpeStream>(context_, stream));
  return streams_.back().get();
}

}  // namespace perfetto::trace_processor::perf_importer
