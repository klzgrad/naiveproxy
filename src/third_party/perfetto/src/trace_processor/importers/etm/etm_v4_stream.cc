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

#include "src/trace_processor/importers/etm/etm_v4_stream.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/etm/etm_v4_stream_demultiplexer.h"
#include "src/trace_processor/importers/etm/frame_decoder.h"
#include "src/trace_processor/importers/etm/opencsd.h"
#include "src/trace_processor/importers/etm/storage_handle.h"
#include "src/trace_processor/importers/perf/aux_record.h"
#include "src/trace_processor/importers/perf/itrace_start_record.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/util.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/etm_tables_py.h"

namespace perfetto::trace_processor::etm {
namespace {
bool is_raw_format(const perf_importer::AuxRecord& aux) {
  return (aux.flags & PERF_AUX_FLAG_CORESIGHT_FORMAT_RAW);
}
}  // namespace

EtmV4Stream::EtmV4Stream(TraceProcessorContext* context,
                         FrameDecoder* frame_decoder,
                         tables::EtmV4ConfigurationTable::Id config_id)
    : context_(context), frame_decoder_(frame_decoder), config_id_(config_id) {}

EtmV4Stream::~EtmV4Stream() = default;

base::Status EtmV4Stream::Parse(perf_importer::AuxRecord aux,
                                TraceBlobView data) {
  if (!is_raw_format(aux)) {
    return ParseFramedData(aux.offset, std::move(data));
  }
  AddChunk(std::move(data));
  return base::OkStatus();
}

base::Status EtmV4Stream::ParseFramedData(uint64_t offset, TraceBlobView data) {
  PERFETTO_CHECK(offset == index_);
  uint32_t data_block_size;
  PERFETTO_CHECK(perf_importer::SafeCast(data.size(), &data_block_size));

  ASSIGN_OR_RETURN(
      bool keep_going,
      frame_decoder_->TraceDataIn(OCSD_OP_RESET, index_, 0, nullptr, nullptr));
  PERFETTO_CHECK(keep_going);

  uint32_t num_bytes_processed;
  ASSIGN_OR_RETURN(keep_going, frame_decoder_->TraceDataIn(
                                   OCSD_OP_DATA, index_, data_block_size,
                                   data.data(), &num_bytes_processed));
  PERFETTO_CHECK(keep_going);
  PERFETTO_CHECK(num_bytes_processed == data_block_size);
  PERFETTO_CHECK(index_ <= std::numeric_limits<decltype(index_)>::max() -
                               data_block_size);
  index_ += data_block_size;

  ASSIGN_OR_RETURN(keep_going, frame_decoder_->TraceDataIn(
                                   OCSD_OP_EOT, index_, 0, nullptr, nullptr));
  PERFETTO_CHECK(keep_going);
  return base::OkStatus();
}

ocsd_datapath_resp_t EtmV4Stream::TraceDataIn(const ocsd_datapath_op_t op,
                                              const ocsd_trc_index_t,
                                              const uint32_t size,
                                              const uint8_t* data,
                                              uint32_t* num_bytes_processed) {
  switch (op) {
    case OCSD_OP_RESET:
      StartChunkedTrace();
      break;

    case OCSD_OP_DATA:
      WriteChunkedTrace(data, size);
      *num_bytes_processed = size;
      break;

    case OCSD_OP_FLUSH:
      PERFETTO_FATAL("Unreachable");
      break;

    case OCSD_OP_EOT:
      EndChunkedTrace();
  }
  return OCSD_RESP_CONT;
}

void EtmV4Stream::OnDataLoss(uint64_t num_bytes) {
  index_ += num_bytes;
  // No need to do anything else as we treat every AuxData as a new chunk, or
  // in the case of non raw data, the decoder is reset for each AuxData
}

base::Status EtmV4Stream::NotifyEndOfStream() {
  PERFETTO_CHECK(stream_active_);
  if (session_.has_value()) {
    EndSession();
  }
  stream_active_ = false;
  return base::OkStatus();
}

base::Status EtmV4Stream::OnItraceStartRecord(
    perf_importer::ItraceStartRecord start) {
  std::optional<int64_t> start_ts;
  if (start.time().has_value()) {
    start_ts = context_->clock_tracker->ToTraceTime(
        start.attr->clock_id(), static_cast<int64_t>(*start.time()));
  }
  if (session_.has_value()) {
    EndSession();
  }
  StartSession(start_ts);
  return base::OkStatus();
}

void EtmV4Stream::StartSession(std::optional<int64_t> start_ts) {
  PERFETTO_CHECK(stream_active_);
  PERFETTO_CHECK(!session_.has_value());
  auto session_id = context_->storage->mutable_etm_v4_session_table()
                        ->Insert({config_id_, start_ts})
                        .id;
  session_.emplace(session_id);

  if (start_ts) {
    static constexpr auto kETMSessionBlueprint =
        tracks::CounterBlueprint("etm_session", tracks::UnknownUnitBlueprint(),
                                 tracks::DimensionBlueprints(),
                                 tracks::StaticNameBlueprint("ETMSession"));
    TrackId track_id =
        context_->track_tracker->InternTrack(kETMSessionBlueprint);
    context_->event_tracker->PushCounter(
        start_ts.value(), static_cast<double>(session_id.value), track_id);
  }
}

void EtmV4Stream::AddChunk(TraceBlobView chunk) {
  PERFETTO_CHECK(session_.has_value());
  session_->chunks_.push_back(std::move(chunk));
}

void EtmV4Stream::EndSession() {
  PERFETTO_CHECK(session_.has_value());
  // There should be no inflight framed data.
  PERFETTO_CHECK(buffer_.empty());
  uint32_t chunk_set_id = context_->storage->etm_v4_chunk_table().row_count();
  for (auto& chunk : session_->chunks_) {
    if (chunk.size() == 0) {
      continue;
    }
    auto id = context_->storage->mutable_etm_v4_chunk_table()
                  ->Insert({session_->session_id, chunk_set_id,
                            static_cast<int64_t>(chunk.size())})
                  .id;
    StorageHandle(context_).StoreChunk(id, std::move(chunk));
  }
  session_.reset();
}

void EtmV4Stream::StartChunkedTrace() {
  PERFETTO_CHECK(buffer_.empty());
}

void EtmV4Stream::WriteChunkedTrace(const uint8_t* src, uint32_t size) {
  buffer_.insert(buffer_.end(), src, src + size);
}

void EtmV4Stream::EndChunkedTrace() {
  if (buffer_.empty()) {
    return;
  }
  AddChunk(TraceBlobView(TraceBlob::CopyFrom(buffer_.data(), buffer_.size())));
  buffer_.clear();
}

}  // namespace perfetto::trace_processor::etm
