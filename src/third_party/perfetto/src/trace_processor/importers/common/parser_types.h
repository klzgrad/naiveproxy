/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PARSER_TYPES_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PARSER_TYPES_H_

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"

namespace perfetto::trace_processor {

struct alignas(8) InlineSchedSwitch {
  int64_t prev_state;
  int32_t next_pid;
  int32_t next_prio;
  StringPool::Id next_comm;
};
static_assert(sizeof(InlineSchedSwitch) == 24);

// We enforce the exact size as it's critical for peak-memory use when sorting
// data in trace processor that this struct is as small as possible.
static_assert(sizeof(InlineSchedSwitch) == 24);

struct alignas(8) InlineSchedWaking {
  int32_t pid;
  uint16_t target_cpu;
  uint16_t prio;
  StringPool::Id comm;
  uint16_t common_flags;
};

// We enforce the exact size as it's critical for peak-memory use when sorting
// data in trace processor that this struct is as small as possible.
static_assert(sizeof(InlineSchedWaking) == 16);

struct alignas(8) JsonEvent {
  int64_t dur = std::numeric_limits<int64_t>::max();

  uint32_t pid = 0;
  uint32_t tid = 0;

  StringPool::Id name = StringPool::Id::Null();
  StringPool::Id cat = StringPool::Id::Null();

  union IdStrOrUint64 {
    StringPool::Id id_str;
    uint64_t id_uint64;
  };
  IdStrOrUint64 id;
  IdStrOrUint64 bind_id;

  int64_t tts = std::numeric_limits<int64_t>::max();
  int64_t tdur = std::numeric_limits<int64_t>::max();
  int64_t async_cookie = std::numeric_limits<int64_t>::max();

  std::unique_ptr<char[]> args;

  char phase = '\0';

  uint64_t flow_in : 1;
  uint64_t flow_out : 1;

  uint64_t pid_exists : 1;
  uint64_t pid_is_string_id : 1;
  uint64_t tid_exists : 1;
  uint64_t tid_is_string_id : 1;

  uint64_t bind_enclosing_slice : 1;

  enum class IdType : uint64_t {
    kNone = 0,
    kString = 1,
    kUint64 = 2,
  };
  IdType id_type : 2;
  IdType bind_id_type : 2;

  enum class Scope : uint64_t {
    kNone = 0,
    kGlobal = 1,
    kProcess = 2,
    kThread = 3,
  };
  Scope scope : 2;

  enum class AsyncCookieType : uint64_t {
    kNone,
    kId,
    kId2Local,
    kId2Global,
  };
  AsyncCookieType async_cookie_type : 2;

  uint64_t args_size : 41;

  JsonEvent()
      : flow_in(false),
        flow_out(false),
        pid_exists(false),
        pid_is_string_id(false),
        tid_exists(false),
        tid_is_string_id(false),
        bind_enclosing_slice(false),
        id_type(IdType::kNone),
        bind_id_type(IdType::kNone),
        scope(Scope::kNone),
        async_cookie_type(AsyncCookieType::kNone),
        args_size(0) {}
};
static_assert(sizeof(JsonEvent) % 8 == 0);

struct alignas(8) TracePacketData {
  TraceBlobView packet;
  RefPtr<PacketSequenceStateGeneration> sequence_state;
};
static_assert(sizeof(TracePacketData) % 8 == 0);

struct alignas(8) TrackEventData {
  TrackEventData(TraceBlobView pv,
                 RefPtr<PacketSequenceStateGeneration> generation)
      : trace_packet_data{std::move(pv), std::move(generation)} {}

  explicit TrackEventData(TracePacketData tpd)
      : trace_packet_data(std::move(tpd)) {}

  static constexpr uint8_t kMaxNumExtraCounters = 8;

  uint8_t CountExtraCounterValues() const {
    for (uint8_t i = 0; i < TrackEventData::kMaxNumExtraCounters; ++i) {
      if (std::equal_to<double>()(extra_counter_values[i], 0))
        return i;
    }
    return TrackEventData::kMaxNumExtraCounters;
  }

  TracePacketData trace_packet_data;
  std::optional<int64_t> thread_timestamp;
  std::optional<int64_t> thread_instruction_count;
  double counter_value = 0;
  std::array<double, kMaxNumExtraCounters> extra_counter_values = {};
};
static_assert(sizeof(TracePacketData) % 8 == 0);

struct alignas(8) LegacyV8CpuProfileEvent {
  uint64_t session_id;
  uint32_t pid;
  uint32_t tid;
  uint32_t callsite_id;
};
static_assert(sizeof(LegacyV8CpuProfileEvent) % 8 == 0);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_PARSER_TYPES_H_
