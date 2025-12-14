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

#ifndef SRC_TRACE_REDACTION_REDACTOR_CLOCK_CONVERTER_H_
#define SRC_TRACE_REDACTION_REDACTOR_CLOCK_CONVERTER_H_

#include <unordered_map>
#include "perfetto/ext/base/status_macros.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "src/trace_processor/util/clock_synchronizer.h"

namespace perfetto::trace_redaction {

class RedactorClockSynchronizerListenerImpl {
 public:
  RedactorClockSynchronizerListenerImpl();

  base::Status OnClockSyncCacheMiss();

  base::Status OnInvalidClockSnapshot();

  base::Status OnTraceTimeClockIdChanged(
      perfetto::trace_processor::ClockSynchronizer<
          RedactorClockSynchronizerListenerImpl>::ClockId trace_time_clock_id);

  base::Status OnSetTraceTimeClock(
      perfetto::trace_processor::ClockSynchronizer<
          RedactorClockSynchronizerListenerImpl>::ClockId trace_time_clock_id);

  // Always returns true as redactor only supports local host clock conversion.
  bool IsLocalHost();

 private:
  // Number of time that trace time has been updated.
  uint32_t trace_time_updates_;
};

using RedactorClockSynchronizer = perfetto::trace_processor::ClockSynchronizer<
    RedactorClockSynchronizerListenerImpl>;

using SequenceId = uint32_t;
using ClockId = RedactorClockSynchronizer::ClockId;
using ClockTimestamp = RedactorClockSynchronizer::ClockTimestamp;

// This class handles conversions between different clocks for trace redactor.
//
// This class is a wrapper for trace_processor::ClockSynchronizer with the
// addition that it caches clocks required for conversion for different data
// sources and it is designed to be used by the trace redactor.
//
// Any trace packet intends to use the redactor ProcessThreadTimeline and whose
// clock won't be the default trace time should use this class to convert
// it to the default trace time which is used by ProcessThreadTimeline.
class RedactorClockConverter {
 public:
  enum class DataSourceType { kPerfDataSource, kUnknown };

  RedactorClockConverter();

  // Sets the global trace clock which will be the target clock used for
  // conversions.
  base::Status SetTraceClock(ClockId clock_id);

  base::StatusOr<ClockId> GetTraceClock();

  // Sets the clock_id to be used as the default for the provided
  // (trusted_sequence_id, clock_type) pair
  void SetDefaultDataSourceClock(DataSourceType clock_type,
                                 ClockId clock_id,
                                 SequenceId trusted_sequence_id);

  // Returns the clock that should be used for the current data source when no
  // timestamp_clock_id override is specified for the packet.
  base::StatusOr<ClockId> GetDataSourceClock(SequenceId trusted_seq_id,
                                             DataSourceType clock_type) const;

  // Add a new clock snapshot which will be used for clock synchronization.
  base::Status AddClockSnapshot(std::vector<ClockTimestamp>& clock_snapshot);

  // Converts timestamp from a source clock to trace time.
  // Returns the timestamp converted to the trace domain.
  base::StatusOr<uint64_t> ConvertToTrace(ClockId source_clock_id,
                                          uint64_t source_ts) const;

 private:
  // This class is used to keep track of default clock ids for each sequence.
  // It is an abstraction of the TracePacketDefaults parsed from the trace.
  class SequenceClocks {
   public:
    // Get the Clock Id for the provided clock_type if exists.
    std::optional<ClockId> GetClockId(DataSourceType clock_type) const {
      auto clock_id = clock_type_to_id_.Find(clock_type);
      if (clock_id == nullptr) {
        return std::nullopt;
      }
      return *clock_id;
    }

    void SetClock(DataSourceType clock_type, ClockId clock_id) {
      clock_type_to_id_[clock_type] = clock_id;
    }

   private:
    base::FlatHashMap<DataSourceType, ClockId> clock_type_to_id_;
  };

  // Returns the default clock id to be used for the specified data source when
  // packet does not specify a TracePacket::timestamp_clock_id
  std::optional<ClockId> GetSequenceDefaultDataSourceClock(
      SequenceId trusted_seq_id,
      DataSourceType clock_type) const;

  // Returns the default clocks to be used when neither a packet clock nor a
  // sequence clock have been specified.
  base::StatusOr<ClockId> GetGlobalDefaultDataSourceClock(
      const DataSourceType& clock_type) const;

  mutable RedactorClockSynchronizer clock_synchronizer_;
  std::optional<ClockId> primary_trace_clock_;
  base::FlatHashMap<SequenceId, SequenceClocks> seq_to_default_clocks_;
};

}  // namespace perfetto::trace_redaction

#endif  // SRC_TRACE_REDACTION_REDACTOR_CLOCK_CONVERTER_H_
