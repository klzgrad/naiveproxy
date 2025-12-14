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

#include "src/shared_lib/track_event/serialization.h"

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/thread_utils.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"

namespace perfetto::shlib {
namespace {

using perfetto::internal::TrackEventInternal;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
std::vector<std::string> GetCmdLine() {
  std::vector<std::string> cmdline_str;
  std::string cmdline;
  if (perfetto::base::ReadFile("/proc/self/cmdline", &cmdline)) {
    perfetto::base::StringSplitter splitter(std::move(cmdline), '\0');
    while (splitter.Next()) {
      cmdline_str.emplace_back(splitter.cur_token(), splitter.cur_token_size());
    }
  }
  return cmdline_str;
}
#endif

}  // namespace

protozero::MessageHandle<perfetto::protos::pbzero::TracePacket>
NewTracePacketInternal(perfetto::TraceWriterBase* trace_writer,
                       perfetto::shlib::TrackEventIncrementalState* incr_state,
                       const perfetto::shlib::TrackEventTlsState& tls_state,
                       perfetto::TraceTimestamp timestamp,
                       uint32_t seq_flags) {
  // PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL is the default timestamp returned
  // by TrackEventInternal::GetTraceTime(). If the configuration in `tls_state`
  // uses a different clock, we have to use that instead.
  if (PERFETTO_UNLIKELY(tls_state.default_clock_id !=
                            PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL &&
                        timestamp.clock_id ==
                            PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL)) {
    timestamp.clock_id = tls_state.default_clock_id;
  }
  auto packet = trace_writer->NewTracePacket();
  auto ts_unit_multiplier = tls_state.timestamp_unit_multiplier;
  if (PERFETTO_LIKELY(timestamp.clock_id ==
                      PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL)) {
    if (PERFETTO_LIKELY(incr_state->last_timestamp_ns <= timestamp.value)) {
      // No need to set the clock id here, since
      // PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL is the clock id assumed by
      // default.
      auto time_diff_ns = timestamp.value - incr_state->last_timestamp_ns;
      auto time_diff_units = time_diff_ns / ts_unit_multiplier;
      packet->set_timestamp(time_diff_units);
      incr_state->last_timestamp_ns += time_diff_units * ts_unit_multiplier;
    } else {
      packet->set_timestamp(timestamp.value / ts_unit_multiplier);
      packet->set_timestamp_clock_id(
          ts_unit_multiplier == 1
              ? static_cast<uint32_t>(PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH)
              : static_cast<uint32_t>(PERFETTO_TE_TIMESTAMP_TYPE_ABSOLUTE));
    }
  } else if (PERFETTO_LIKELY(timestamp.clock_id ==
                             tls_state.default_clock_id)) {
    packet->set_timestamp(timestamp.value / ts_unit_multiplier);
  } else {
    packet->set_timestamp(timestamp.value);
    packet->set_timestamp_clock_id(timestamp.clock_id);
  }
  packet->set_sequence_flags(seq_flags);
  return packet;
}

void ResetIncrementalStateIfRequired(
    perfetto::TraceWriterBase* trace_writer,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    const perfetto::shlib::TrackEventTlsState& tls_state,
    const perfetto::TraceTimestamp& timestamp) {
  if (!incr_state->was_cleared) {
    return;
  }
  incr_state->was_cleared = false;

  auto sequence_timestamp = timestamp;
  if (timestamp.clock_id != PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH &&
      timestamp.clock_id != PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL) {
    sequence_timestamp = TrackEventInternal::GetTraceTime();
  }

  incr_state->last_timestamp_ns = sequence_timestamp.value;
  auto tid = perfetto::base::GetThreadId();
  auto pid = perfetto::Platform::GetCurrentProcessId();
  uint64_t thread_track_uuid =
      perfetto_te_process_track_uuid ^ static_cast<uint64_t>(tid);
  auto ts_unit_multiplier = tls_state.timestamp_unit_multiplier;
  {
    // Mark any incremental state before this point invalid. Also set up
    // defaults so that we don't need to repeat constant data for each packet.
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, tls_state, timestamp,
        perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
    auto defaults = packet->set_trace_packet_defaults();
    defaults->set_timestamp_clock_id(tls_state.default_clock_id);
    // Establish the default track for this event sequence.
    auto track_defaults = defaults->set_track_event_defaults();
    track_defaults->set_track_uuid(thread_track_uuid);

    if (tls_state.default_clock_id != PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH) {
      perfetto::protos::pbzero::ClockSnapshot* clocks =
          packet->set_clock_snapshot();
      // Trace clock.
      perfetto::protos::pbzero::ClockSnapshot::Clock* trace_clock =
          clocks->add_clocks();
      trace_clock->set_clock_id(PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH);
      trace_clock->set_timestamp(sequence_timestamp.value);

      if (PERFETTO_LIKELY(tls_state.default_clock_id ==
                          PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL)) {
        // Delta-encoded incremental clock in nanoseconds by default but
        // configurable by |tls_state.timestamp_unit_multiplier|.
        perfetto::protos::pbzero::ClockSnapshot::Clock* clock_incremental =
            clocks->add_clocks();
        clock_incremental->set_clock_id(PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL);
        clock_incremental->set_timestamp(sequence_timestamp.value /
                                         ts_unit_multiplier);
        clock_incremental->set_is_incremental(true);
        clock_incremental->set_unit_multiplier_ns(ts_unit_multiplier);
      }
      if (ts_unit_multiplier > 1) {
        // absolute clock with custom timestamp_unit_multiplier.
        perfetto::protos::pbzero::ClockSnapshot::Clock* absolute_clock =
            clocks->add_clocks();
        absolute_clock->set_clock_id(PERFETTO_TE_TIMESTAMP_TYPE_ABSOLUTE);
        absolute_clock->set_timestamp(sequence_timestamp.value /
                                      ts_unit_multiplier);
        absolute_clock->set_is_incremental(false);
        absolute_clock->set_unit_multiplier_ns(ts_unit_multiplier);
      }
    }
  }

  // Every thread should write a descriptor for its default track, because most
  // trace points won't explicitly reference it. We also write the process
  // descriptor from every thread that writes trace events to ensure it gets
  // emitted at least once.
  {
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, tls_state, timestamp,
        perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    auto* track = packet->set_track_descriptor();
    track->set_uuid(thread_track_uuid);
    track->set_parent_uuid(perfetto_te_process_track_uuid);
    auto* td = track->set_thread();

    td->set_pid(static_cast<int32_t>(pid));
    td->set_tid(static_cast<int32_t>(tid));
    std::string thread_name;
    if (perfetto::base::GetThreadName(thread_name))
      td->set_thread_name(thread_name);
  }
  {
    auto packet = NewTracePacketInternal(
        trace_writer, incr_state, tls_state, timestamp,
        perfetto::protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    auto* track = packet->set_track_descriptor();
    track->set_uuid(perfetto_te_process_track_uuid);
    auto* pd = track->set_process();

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    static perfetto::base::NoDestructor<std::vector<std::string>> cmdline(
        GetCmdLine());
    if (!cmdline.ref().empty()) {
      // Since cmdline is a zero-terminated list of arguments, this ends up
      // writing just the first element, i.e., the process name, into the
      // process name field.
      pd->set_process_name(cmdline.ref()[0]);
      for (const std::string& arg : cmdline.ref()) {
        pd->add_cmdline(arg);
      }
    }
#endif
    pd->set_pid(static_cast<int32_t>(pid));
  }
}

}  // namespace perfetto::shlib
