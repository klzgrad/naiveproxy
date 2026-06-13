/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/importers/perf/aux_stream_manager.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/perf/aux_data_tokenizer.h"
#include "src/trace_processor/importers/perf/aux_record.h"
#include "src/trace_processor/importers/perf/auxtrace_info_record.h"
#include "src/trace_processor/importers/perf/auxtrace_record.h"
#include "src/trace_processor/importers/perf/itrace_start_record.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::perf_importer {

base::StatusOr<std::reference_wrapper<AuxStream>>
AuxStreamManager::GetOrCreateStreamForSampleId(
    const std::optional<SampleId>& sample_id) {
  if (!sample_id.has_value() || !sample_id->cpu().has_value()) {
    return base::ErrStatus(
        "Aux data handling only implemented for per cpu data.");
  }
  return GetOrCreateStreamForCpu(*sample_id->cpu());
}

base::Status AuxStreamManager::OnAuxtraceInfoRecord(AuxtraceInfoRecord info) {
  if (tokenizer_) {
    return base::ErrStatus("Multiple PERF_RECORD_AUXTRACE_INFO not supported.");
  }
  ASSIGN_OR_RETURN(tokenizer_,
                   perf_tracker_->CreateAuxDataTokenizer(std::move(info)));
  return base::OkStatus();
}

base::Status AuxStreamManager::OnAuxRecord(AuxRecord aux) {
  if (!tokenizer_) {
    return base::ErrStatus(
        "PERF_RECORD_AUX without previous PERF_RECORD_AUXTRACE_INFO.");
  }
  ASSIGN_OR_RETURN(AuxStream & stream,
                   GetOrCreateStreamForSampleId(aux.sample_id));
  return stream.OnAuxRecord(aux);
}

base::Status AuxStreamManager::OnAuxtraceRecord(AuxtraceRecord auxtrace,
                                                TraceBlobView data) {
  if (!tokenizer_) {
    return base::ErrStatus(
        "PERF_RECORD_AUXTRACE without previous PERF_RECORD_AUXTRACE_INFO.");
  }
  if (auxtrace.cpu == std::numeric_limits<uint32_t>::max()) {
    // Aux data can be written by cpu or by tid. An unset cpu will have a value
    // of UINT32_MAX. Be aware for an unset tid simpleperf uses 0 and perf uses
    // UINT32_MAX. ¯\_(ツ)_/¯
    // Deal just with per cpu data for now.
    return base::ErrStatus(
        "Aux data handling only implemented for per cpu data.");
  }
  ASSIGN_OR_RETURN(AuxStream & stream, GetOrCreateStreamForCpu(auxtrace.cpu));
  return stream.OnAuxtraceRecord(std::move(auxtrace), std::move(data));
}

base::Status AuxStreamManager::OnItraceStartRecord(ItraceStartRecord start) {
  ASSIGN_OR_RETURN(AuxStream & stream,
                   GetOrCreateStreamForSampleId(start.sample_id));
  return stream.OnItraceStartRecord(std::move(start));
}

base::Status AuxStreamManager::FinalizeStreams() {
  for (auto it = auxdata_streams_by_cpu_.GetIterator(); it; ++it) {
    RETURN_IF_ERROR(it.value()->NotifyEndOfStream());
  }

  return base::OkStatus();
}

base::StatusOr<std::reference_wrapper<AuxStream>>
AuxStreamManager::GetOrCreateStreamForCpu(uint32_t cpu) {
  PERFETTO_CHECK(tokenizer_);
  std::unique_ptr<AuxStream>* stream_ptr = auxdata_streams_by_cpu_.Find(cpu);
  if (!stream_ptr) {
    std::unique_ptr<AuxStream> stream(
        new AuxStream(this, AuxStream::Type::kCpuBound, cpu));
    ASSIGN_OR_RETURN(stream->data_stream_,
                     tokenizer_->InitializeAuxDataStream(stream.get()));
    stream_ptr = auxdata_streams_by_cpu_.Insert(cpu, std::move(stream)).first;
  }

  return std::ref(**stream_ptr);
}

AuxStream::AuxStream(AuxStreamManager* manager, Type type, uint32_t tid_or_cpu)
    : manager_(*manager), type_(type), tid_or_cpu_(tid_or_cpu) {}
AuxStream::~AuxStream() = default;

base::Status AuxStream::OnAuxRecord(AuxRecord aux) {
  if (aux.offset < aux_end_) {
    return base::ErrStatus("Overlapping AuxRecord. Got %" PRIu64
                           ", expected at least %" PRIu64,
                           aux.offset, aux_end_);
  }
  if (aux.offset > aux_end_) {
    manager_.context()->storage->IncrementStats(
        stats::perf_aux_missing, static_cast<int64_t>(aux.offset - aux_end_));
  }
  if (aux.flags & PERF_AUX_FLAG_TRUNCATED) {
    manager_.context()->storage->IncrementStats(stats::perf_aux_truncated);
  }
  if (aux.flags & PERF_AUX_FLAG_PARTIAL) {
    manager_.context()->storage->IncrementStats(stats::perf_aux_partial);
  }
  if (aux.flags & PERF_AUX_FLAG_COLLISION) {
    manager_.context()->storage->IncrementStats(stats::perf_aux_collision);
  }
  outstanding_records_.emplace_back(std::move(aux));
  aux_end_ = aux.end();
  return MaybeParse();
}

base::Status AuxStream::OnAuxtraceRecord(AuxtraceRecord auxtrace,
                                         TraceBlobView data) {
  PERFETTO_CHECK(auxtrace.size == data.size());
  if (auxtrace.offset < auxtrace_end_) {
    return base::ErrStatus("Overlapping AuxtraceData");
  }
  if (auxtrace.offset > auxtrace_end_) {
    manager_.context()->storage->IncrementStats(
        stats::perf_auxtrace_missing,
        static_cast<int64_t>(auxtrace.offset - auxtrace_end_));
  }
  outstanding_auxtrace_data_.emplace_back(auxtrace, std::move(data));
  auxtrace_end_ = outstanding_auxtrace_data_.back().end();
  return MaybeParse();
}

base::Status AuxStream::MaybeParse() {
  while (!outstanding_records_.empty()) {
    auto& record = outstanding_records_.front();
    if (auto* r = std::get_if<ItraceStartRecord>(&record); r) {
      RETURN_IF_ERROR(data_stream_->OnItraceStartRecord(std::move(*r)));
      outstanding_records_.pop_front();
      continue;
    }

    const AuxRecord* aux_record = std::get_if<AuxRecord>(&record);
    PERFETTO_CHECK(aux_record);

    if (aux_record->size == 0) {
      outstanding_records_.pop_front();
      continue;
    }

    if (outstanding_auxtrace_data_.empty()) {
      break;
    }

    AuxtraceDataReader& auxtrace_data = outstanding_auxtrace_data_.front();

    // We need both auxtrace and aux, so we start at the biggest offset and end
    // at the smallest.
    const uint64_t start_offset =
        std::max(aux_record->offset, auxtrace_data.offset());
    const uint64_t end_offset =
        std::min(aux_record->end(), auxtrace_data.end());

    if (start_offset >= auxtrace_data.end()) {
      // No usefull data at the front of the queue
      outstanding_auxtrace_data_.pop_front();
      continue;
    }
    if (start_offset >= aux_record->end()) {
      // No usefull data in this record
      outstanding_records_.pop_front();
      continue;
    }
    // We can not get this after the two check above.
    PERFETTO_CHECK(start_offset < end_offset);

    if (tokenizer_offset_ < start_offset) {
      data_stream_->OnDataLoss(start_offset - tokenizer_offset_);
      tokenizer_offset_ = start_offset;
    }
    PERFETTO_CHECK(tokenizer_offset_ == start_offset);

    const uint64_t size = end_offset - start_offset;
    auxtrace_data.DropUntil(start_offset);
    TraceBlobView data = auxtrace_data.ConsumeFront(size);

    AuxRecord adjusted_aux_record = *aux_record;
    adjusted_aux_record.offset = tokenizer_offset_;
    adjusted_aux_record.size = size;
    tokenizer_offset_ += size;
    RETURN_IF_ERROR(
        data_stream_->Parse(std::move(adjusted_aux_record), std::move(data)));
  }

  return base::OkStatus();
}

base::Status AuxStream::OnItraceStartRecord(ItraceStartRecord start) {
  if (outstanding_records_.empty()) {
    return data_stream_->OnItraceStartRecord(std::move(start));
  }
  outstanding_records_.emplace_back(std::move(start));
  return base::OkStatus();
}

base::Status AuxStream::NotifyEndOfStream() {
  for (; !outstanding_records_.empty(); outstanding_records_.pop_front()) {
    auto& record = outstanding_records_.front();
    if (auto* r = std::get_if<ItraceStartRecord>(&record); r) {
      RETURN_IF_ERROR(data_stream_->OnItraceStartRecord(std::move(*r)));
    }
  }

  if (aux_end_ < auxtrace_end_) {
    manager_.context()->storage->IncrementStats(
        stats::perf_aux_missing,
        static_cast<int64_t>(auxtrace_end_ - aux_end_));
  } else if (auxtrace_end_ < aux_end_) {
    manager_.context()->storage->IncrementStats(
        stats::perf_auxtrace_missing,
        static_cast<int64_t>(aux_end_ - auxtrace_end_));
  }

  uint64_t end = std::max(aux_end_, auxtrace_end_);
  if (tokenizer_offset_ < end) {
    uint64_t loss = end - tokenizer_offset_;
    data_stream_->OnDataLoss(loss);
    tokenizer_offset_ += loss;
  }
  return data_stream_->NotifyEndOfStream();
}

AuxStream::AuxtraceDataReader::AuxtraceDataReader(AuxtraceRecord auxtrace,
                                                  TraceBlobView data)
    : offset_(auxtrace.offset), data_(std::move(data)) {
  PERFETTO_CHECK(auxtrace.size == data_.size());
}

void AuxStream::AuxtraceDataReader::DropUntil(uint64_t offset) {
  PERFETTO_CHECK(offset >= offset_ && offset <= end());
  const uint64_t size = offset - offset_;

  data_ = data_.slice_off(size, data_.size() - size);
  offset_ += size;
}

TraceBlobView AuxStream::AuxtraceDataReader::ConsumeFront(uint64_t size) {
  PERFETTO_CHECK(size <= data_.size());
  TraceBlobView res = data_.slice_off(0, size);
  data_ = data_.slice_off(size, data_.size() - size);
  offset_ += size;
  return res;
}

}  // namespace perfetto::trace_processor::perf_importer
