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

#include "src/trace_processor/importers/perf/sample.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/importers/perf/record.h"

#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"

namespace perfetto::trace_processor::perf_importer {
namespace {

bool ParseSampleReadGroup(Reader& reader,
                          uint64_t read_format,
                          uint64_t num_records,
                          std::vector<Sample::ReadGroup>& out) {
  out.resize(num_records);
  for (auto& read : out) {
    if (PERFETTO_UNLIKELY(!reader.Read(read.value))) {
      return false;
    }

    if (read_format & PERF_FORMAT_ID) {
      if (PERFETTO_UNLIKELY(!reader.ReadOptional(read.event_id))) {
        return false;
      }
    }

    if (read_format & PERF_FORMAT_LOST) {
      uint64_t lost;
      if (PERFETTO_UNLIKELY(!reader.Read(lost))) {
        return false;
      }
    }
  }

  return true;
}

bool ParseSampleRead(Reader& reader,
                     uint64_t read_format,
                     std::vector<Sample::ReadGroup>& out) {
  uint64_t value_or_nr;

  if (PERFETTO_UNLIKELY(!reader.Read(value_or_nr))) {
    return false;
  }

  if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
    uint64_t total_time_enabled;
    if (PERFETTO_UNLIKELY(!reader.Read(total_time_enabled))) {
      return false;
    }
  }

  if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
    uint64_t total_time_running;
    if (PERFETTO_UNLIKELY(!reader.Read(total_time_running))) {
      return false;
    }
  }

  if (read_format & PERF_FORMAT_GROUP) {
    return ParseSampleReadGroup(reader, read_format, value_or_nr, out);
  }

  std::optional<uint64_t> event_id;
  if (read_format & PERF_FORMAT_ID) {
    event_id.emplace(0);
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(event_id))) {
      return false;
    }
  }

  if (read_format & PERF_FORMAT_LOST) {
    uint64_t lost;
    if (PERFETTO_UNLIKELY(!reader.Read(lost))) {
      return false;
    }
  }

  out.push_back({event_id, value_or_nr});

  return true;
}

protos::pbzero::Profiling::CpuMode PerfCallchainContextToCpuMode(uint64_t ip) {
  switch (ip) {
    case PERF_CONTEXT_HV:
      return protos::pbzero::Profiling::MODE_HYPERVISOR;
    case PERF_CONTEXT_KERNEL:
      return protos::pbzero::Profiling::MODE_KERNEL;
    case PERF_CONTEXT_USER:
      return protos::pbzero::Profiling::MODE_USER;
    case PERF_CONTEXT_GUEST_KERNEL:
      return protos::pbzero::Profiling::MODE_GUEST_KERNEL;
    case PERF_CONTEXT_GUEST_USER:
      return protos::pbzero::Profiling::MODE_GUEST_USER;
    case PERF_CONTEXT_GUEST:
    default:
      return protos::pbzero::Profiling::MODE_UNKNOWN;
  }
  PERFETTO_FATAL("For GCC");
}

bool IsPerfContextMark(uint64_t ip) {
  return ip >= PERF_CONTEXT_MAX;
}

bool ParseSampleCallchain(Reader& reader,
                          protos::pbzero::Profiling::CpuMode cpu_mode,
                          std::vector<Sample::Frame>& out) {
  uint64_t nr;
  if (PERFETTO_UNLIKELY(!reader.Read(nr))) {
    return false;
  }

  std::vector<Sample::Frame> frames;
  frames.reserve(nr);
  for (; nr != 0; --nr) {
    uint64_t ip;
    if (PERFETTO_UNLIKELY(!reader.Read(ip))) {
      return false;
    }
    if (PERFETTO_UNLIKELY(IsPerfContextMark(ip))) {
      cpu_mode = PerfCallchainContextToCpuMode(ip);
      continue;
    }
    frames.push_back({cpu_mode, ip});
  }

  out = std::move(frames);
  return true;
}
}  // namespace

base::Status Sample::Parse(int64_t in_trace_ts, const Record& record) {
  PERFETTO_CHECK(record.attr);
  const uint64_t sample_type = record.attr->sample_type();

  trace_ts = in_trace_ts;
  cpu_mode = record.GetCpuMode();
  perf_session = record.session;
  attr = record.attr;

  Reader reader(record.payload.copy());

  std::optional<uint64_t> identifier;
  if (sample_type & PERF_SAMPLE_IDENTIFIER) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(identifier))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_IDENTIFIER");
    }
  }

  if (sample_type & PERF_SAMPLE_IP) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(ip))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_IP");
    }
  }

  if (sample_type & PERF_SAMPLE_TID) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(pid_tid))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_TID");
    }
  }

  if (sample_type & PERF_SAMPLE_TIME) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(time))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_TIME");
    }
  }

  if (sample_type & PERF_SAMPLE_ADDR) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(addr))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_ADDR");
    }
  }

  if (sample_type & PERF_SAMPLE_ID) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(id))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_ID");
    }
  }

  if (identifier.has_value()) {
    if (!id.has_value()) {
      id = identifier;
    } else if (PERFETTO_UNLIKELY(*identifier != *id)) {
      return base::ErrStatus("ID and IDENTIFIER mismatch");
    }
  }

  if (sample_type & PERF_SAMPLE_STREAM_ID) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(stream_id))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_STREAM_ID");
    }
  }

  if (sample_type & PERF_SAMPLE_CPU) {
    struct {
      int32_t cpu;
      int32_t unused;
    } tmp;
    if (PERFETTO_UNLIKELY(!reader.Read(tmp))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_CPU");
    }
    cpu = tmp.cpu;
  }

  if (sample_type & PERF_SAMPLE_PERIOD) {
    if (PERFETTO_UNLIKELY(!reader.ReadOptional(period))) {
      return base ::ErrStatus("Not enough data to read PERF_SAMPLE_PERIOD");
    }
  }

  if (sample_type & PERF_SAMPLE_READ) {
    if (PERFETTO_UNLIKELY(
            !ParseSampleRead(reader, attr->read_format(), read_groups))) {
      return base::ErrStatus("Failed to read PERF_SAMPLE_READ field");
    }
    if (read_groups.empty()) {
      return base::ErrStatus("No data in PERF_SAMPLE_READ field");
    }
  }

  if (sample_type & PERF_SAMPLE_CALLCHAIN) {
    if (PERFETTO_UNLIKELY(!ParseSampleCallchain(reader, cpu_mode, callchain))) {
      return base::ErrStatus("Failed to read PERF_SAMPLE_CALLCHAIN field");
    }
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::perf_importer
