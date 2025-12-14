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

#include "src/trace_redaction/redactor_clock_converter.h"

namespace perfetto::trace_redaction {

using ClockId = RedactorClockSynchronizer::ClockId;

RedactorClockSynchronizerListenerImpl::RedactorClockSynchronizerListenerImpl()
    : trace_time_updates_(0) {}

base::Status RedactorClockSynchronizerListenerImpl::OnClockSyncCacheMiss() {
  return base::OkStatus();
}

base::Status RedactorClockSynchronizerListenerImpl::OnInvalidClockSnapshot() {
  return base::ErrStatus("Invalid clocks snapshot found during redaction");
}

base::Status RedactorClockSynchronizerListenerImpl::OnTraceTimeClockIdChanged(
    ClockId trace_time_clock_id [[maybe_unused]]) {
  ++trace_time_updates_;
  if (PERFETTO_UNLIKELY(trace_time_updates_ > 1)) {
    // We expect the trace time to remain constant for a trace.
    return base::ErrStatus(
        "Redactor clock conversion trace time unexpectedly changed %d times",
        trace_time_updates_);
  }
  return base::OkStatus();
}

base::Status RedactorClockSynchronizerListenerImpl::OnSetTraceTimeClock(
    ClockId trace_time_clock_id [[maybe_unused]]) {
  return base::OkStatus();
}

bool RedactorClockSynchronizerListenerImpl::IsLocalHost() {
  // Redactor does not support multi-machine clock conversion
  return true;
}

RedactorClockConverter::RedactorClockConverter()
    : clock_synchronizer_(
          std::make_unique<RedactorClockSynchronizerListenerImpl>()) {}

base::StatusOr<ClockId> RedactorClockConverter::GetTraceClock() {
  if (!primary_trace_clock_.has_value()) {
    // Set the default clocks if none has been provided.
    RETURN_IF_ERROR(
        SetTraceClock(protos::pbzero::BuiltinClock::BUILTIN_CLOCK_BOOTTIME));
  }
  PERFETTO_DCHECK(primary_trace_clock_.has_value());
  return primary_trace_clock_.value();
}

base::Status RedactorClockConverter::SetTraceClock(ClockId clock_id) {
  primary_trace_clock_ = clock_id;
  RETURN_IF_ERROR(clock_synchronizer_.SetTraceTimeClock(clock_id));
  return base::OkStatus();
}

void RedactorClockConverter::SetDefaultDataSourceClock(
    DataSourceType clock_type,
    ClockId clock_id,
    SequenceId trusted_seq_id) {
  seq_to_default_clocks_[trusted_seq_id].SetClock(clock_type, clock_id);
}

std::optional<ClockId>
RedactorClockConverter::GetSequenceDefaultDataSourceClock(
    SequenceId trusted_seq_id,
    DataSourceType source_type) const {
  SequenceClocks* sequence_clocks = seq_to_default_clocks_.Find(trusted_seq_id);
  if (sequence_clocks == nullptr) {
    return std::nullopt;
  }
  return sequence_clocks->GetClockId(source_type);
}

base::StatusOr<ClockId> RedactorClockConverter::GetGlobalDefaultDataSourceClock(
    const DataSourceType& clock_type) const {
  switch (clock_type) {
    case DataSourceType::kPerfDataSource:
      return protos::pbzero::BuiltinClock::BUILTIN_CLOCK_MONOTONIC_RAW;
    case DataSourceType::kUnknown:
      // A default needs to be set for the data source if you get here.
      return base::ErrStatus(
          "Failed to retrieve a global default clock for data source=%d",
          static_cast<int>(clock_type));
  }

  return base::ErrStatus("Data source %d global default has not been defined.",
                         static_cast<int>(clock_type));
}

base::StatusOr<ClockId> RedactorClockConverter::GetDataSourceClock(
    SequenceId trusted_seq_id,
    DataSourceType clock_type) const {
  auto default_seq_clock =
      GetSequenceDefaultDataSourceClock(trusted_seq_id, clock_type);
  if (PERFETTO_UNLIKELY(!default_seq_clock.has_value())) {
    return GetGlobalDefaultDataSourceClock(clock_type);
  }
  return default_seq_clock.value();
}

base::Status RedactorClockConverter::AddClockSnapshot(
    std::vector<RedactorClockSynchronizer::ClockTimestamp>& clock_snapshot) {
  base::StatusOr<uint32_t> snapshot_id =
      clock_synchronizer_.AddSnapshot(clock_snapshot);
  RETURN_IF_ERROR(snapshot_id.status());
  return base::OkStatus();
}

base::StatusOr<uint64_t> RedactorClockConverter::ConvertToTrace(
    ClockId source_clock_id,
    uint64_t source_ts) const {
  ASSIGN_OR_RETURN(int64_t trace_ts,
                   clock_synchronizer_.ToTraceTime(
                       source_clock_id, static_cast<int64_t>(source_ts)));
  return static_cast<uint64_t>(trace_ts);
}

}  // namespace perfetto::trace_redaction
