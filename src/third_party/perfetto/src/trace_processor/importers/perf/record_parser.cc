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

#include "src/trace_processor/importers/perf/record_parser.h"

#include <cinttypes>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/create_mapping_params.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/perf/itrace_start_record.h"
#include "src/trace_processor/importers/perf/mmap_record.h"
#include "src/trace_processor/importers/perf/perf_counter.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/importers/perf/perf_event_attr.h"
#include "src/trace_processor/importers/perf/perf_tracker.h"
#include "src/trace_processor/importers/perf/reader.h"
#include "src/trace_processor/importers/perf/record.h"
#include "src/trace_processor/importers/perf/sample.h"
#include "src/trace_processor/importers/proto/profile_packet_utils.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/util/build_id.h"

#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"

namespace perfetto::trace_processor::perf_importer {
namespace {

CreateMappingParams BuildCreateMappingParams(
    const CommonMmapRecordFields& fields,
    std::string filename,
    std::optional<BuildId> build_id) {
  return {AddressRange::FromStartAndSize(fields.addr, fields.len), fields.pgoff,
          // start_offset: This is the offset into the file where the ELF header
          // starts. We assume all file mappings are ELF files an thus this
          // offset is 0.
          0,
          // load_bias: This can only be read out of the actual ELF file, which
          // we do not have here, so we set it to 0. When symbolizing we will
          // hopefully have the real load bias and we can compensate there for a
          // possible mismatch.
          0, std::move(filename), std::move(build_id)};
}

bool IsInKernel(protos::pbzero::Profiling::CpuMode cpu_mode) {
  switch (cpu_mode) {
    case protos::pbzero::Profiling::MODE_GUEST_KERNEL:
    case protos::pbzero::Profiling::MODE_KERNEL:
      return true;
    case protos::pbzero::Profiling::MODE_USER:
    case protos::pbzero::Profiling::MODE_HYPERVISOR:
    case protos::pbzero::Profiling::MODE_GUEST_USER:
    case protos::pbzero::Profiling::MODE_UNKNOWN:
      return false;
  }
  PERFETTO_FATAL("For GCC.");
}

}  // namespace

using FramesTable = tables::StackProfileFrameTable;
using CallsitesTable = tables::StackProfileCallsiteTable;

RecordParser::RecordParser(TraceProcessorContext* context,
                           PerfTracker* perf_tracker)
    : context_(context),
      perf_tracker_(perf_tracker),
      mapping_tracker_(context->mapping_tracker.get()) {}

RecordParser::~RecordParser() = default;

void RecordParser::Parse(int64_t ts, Record record) {
  if (base::Status status = ParseRecord(ts, std::move(record)); !status.ok()) {
    context_->storage->IncrementIndexedStats(
        stats::perf_record_skipped, static_cast<int>(record.header.type));
  }
}

base::Status RecordParser::ParseRecord(int64_t ts, Record record) {
  switch (record.header.type) {
    case PERF_RECORD_COMM:
      return ParseComm(std::move(record));

    case PERF_RECORD_SAMPLE:
      return ParseSample(ts, std::move(record));

    case PERF_RECORD_MMAP:
      return ParseMmap(ts, std::move(record));

    case PERF_RECORD_MMAP2:
      return ParseMmap2(ts, std::move(record));

    case PERF_RECORD_ITRACE_START:
      return ParseItraceStart(std::move(record));

    case PERF_RECORD_AUX:
    case PERF_RECORD_AUXTRACE:
    case PERF_RECORD_AUXTRACE_INFO:
      // These should be dealt with at tokenization time
      PERFETTO_FATAL("Unexpected record type at parsing time: %" PRIu32,
                     record.header.type);

    default:
      context_->storage->IncrementIndexedStats(
          stats::perf_unknown_record_type,
          static_cast<int>(record.header.type));
      return base::ErrStatus("Unknown PERF_RECORD with type %" PRIu32,
                             record.header.type);
  }
}

base::Status RecordParser::ParseSample(int64_t ts, Record record) {
  Sample sample;
  RETURN_IF_ERROR(sample.Parse(ts, record));

  if (!sample.period.has_value() && record.attr != nullptr) {
    sample.period = record.attr->sample_period();
  }

  return InternSample(std::move(sample));
}

base::Status RecordParser::InternSample(Sample sample) {
  if (!sample.time.has_value()) {
    // We do not really use this TS as this is using the perf clock, but we need
    // it to be present so that we can compute the trace_ts done during
    // tokenization. (Actually at tokenization time we do estimate a trace_ts if
    // no perf ts is present, but for samples we want this to be as accurate as
    // possible)
    return base::ErrStatus(
        "Can not parse samples with no PERF_SAMPLE_TIME field");
  }

  if (!sample.pid_tid.has_value()) {
    return base::ErrStatus(
        "Can not parse samples with no PERF_SAMPLE_TID field");
  }

  if (sample.cpu_mode ==
      protos::pbzero::perfetto_pbzero_enum_Profiling::MODE_UNKNOWN) {
    context_->storage->IncrementStats(stats::perf_samples_cpu_mode_unknown);
  }

  UniqueTid utid = context_->process_tracker->UpdateThread(sample.pid_tid->tid,
                                                           sample.pid_tid->pid);
  const auto upid = *context_->storage->thread_table()
                         .FindById(tables::ThreadTable::Id(utid))
                         ->upid();

  if (sample.callchain.empty() && sample.ip.has_value()) {
    sample.callchain.push_back(Sample::Frame{sample.cpu_mode, *sample.ip});
  }
  std::optional<CallsiteId> callsite_id = InternCallchain(
      upid, sample.callchain, sample.perf_invocation->needs_pc_adjustment());

  auto session_id = sample.attr->perf_session_id();
  context_->storage->mutable_perf_sample_table()->Insert(
      {sample.trace_ts, utid, sample.cpu,
       context_->storage->InternString(
           ProfilePacketUtils::StringifyCpuMode(sample.cpu_mode)),
       callsite_id, std::nullopt, session_id});

  return UpdateCounters(sample);
}

std::optional<CallsiteId> RecordParser::InternCallchain(
    UniquePid upid,
    const std::vector<Sample::Frame>& callchain,
    bool adjust_pc) {
  if (callchain.empty()) {
    return std::nullopt;
  }

  auto& stack_profile_tracker = *context_->stack_profile_tracker;

  std::optional<CallsiteId> parent;
  uint32_t depth = 0;
  // Note callchain is not empty so this is always valid.
  const auto leaf = --callchain.rend();
  for (auto it = callchain.rbegin(); it != callchain.rend(); ++it) {
    uint64_t ip = it->ip;

    // For non leaf frames the ip stored in the chain is the return address, but
    // what we really need is the address of the call instruction. For that we
    // just need to move the ip one instruction back. Instructions can be of
    // different sizes depending on the CPU arch (ARM, AARCH64, etc..). For
    // symbolization to work we don't really need to point at the first byte of
    // the instruction, any byte of the instruction seems to be enough, so use
    // -1.
    if (ip != 0 && it != leaf && adjust_pc) {
      --ip;
    }

    VirtualMemoryMapping* mapping;
    if (IsInKernel(it->cpu_mode)) {
      mapping = mapping_tracker_->FindKernelMappingForAddress(ip);
    } else {
      mapping = mapping_tracker_->FindUserMappingForAddress(upid, ip);
    }

    if (!mapping) {
      context_->storage->IncrementStats(stats::perf_dummy_mapping_used);
      // Simpleperf will not create mappings for anonymous executable mappings
      // which are used by JITted code (e.g. V8 JavaScript).
      mapping = GetDummyMapping(upid);
    }

    const FrameId frame_id =
        mapping->InternFrame(mapping->ToRelativePc(ip), "");

    parent = stack_profile_tracker.InternCallsite(parent, frame_id, depth);
    depth++;
  }
  return parent;
}

base::Status RecordParser::ParseComm(Record record) {
  Reader reader(record.payload.copy());
  uint32_t pid;
  uint32_t tid;
  std::string comm;
  if (!reader.Read(pid) || !reader.Read(tid) || !reader.ReadCString(comm)) {
    return base::ErrStatus("Failed to parse PERF_RECORD_COMM");
  }

  context_->process_tracker->UpdateThread(tid, pid);
  auto utid = context_->process_tracker->GetOrCreateThread(tid);
  context_->process_tracker->UpdateThreadNameAndMaybeProcessName(
      utid, context_->storage->InternString(base::StringView(comm)),
      ThreadNamePriority::kFtrace);

  return base::OkStatus();
}

base::Status RecordParser::ParseMmap(int64_t trace_ts, Record record) {
  MmapRecord mmap;
  RETURN_IF_ERROR(mmap.Parse(record));
  std::optional<BuildId> build_id =
      record.session->LookupBuildId(mmap.pid, mmap.filename);

  auto params =
      BuildCreateMappingParams(mmap, mmap.filename, std::move(build_id));

  if (IsInKernel(record.GetCpuMode())) {
    perf_tracker_->CreateKernelMemoryMapping(trace_ts, std::move(params));
  } else {
    perf_tracker_->CreateUserMemoryMapping(trace_ts, GetUpid(mmap),
                                           std::move(params));
  }
  return base::OkStatus();
}

base::Status RecordParser::ParseMmap2(int64_t trace_ts, Record record) {
  Mmap2Record mmap2;
  RETURN_IF_ERROR(mmap2.Parse(record));
  std::optional<BuildId> build_id = mmap2.GetBuildId();
  if (!build_id.has_value()) {
    build_id = record.session->LookupBuildId(mmap2.pid, mmap2.filename);
  }

  auto params =
      BuildCreateMappingParams(mmap2, mmap2.filename, std::move(build_id));

  if (IsInKernel(record.GetCpuMode())) {
    perf_tracker_->CreateKernelMemoryMapping(trace_ts, std::move(params));
  } else {
    perf_tracker_->CreateUserMemoryMapping(trace_ts, GetUpid(mmap2),
                                           std::move(params));
  }

  return base::OkStatus();
}

base::Status RecordParser::ParseItraceStart(Record record) {
  ItraceStartRecord start;
  RETURN_IF_ERROR(start.Parse(record));
  context_->process_tracker->UpdateThread(start.tid, start.pid);
  return base::OkStatus();
}

UniquePid RecordParser::GetUpid(const CommonMmapRecordFields& fields) const {
  UniqueTid utid =
      context_->process_tracker->UpdateThread(fields.tid, fields.pid);
  auto upid = context_->storage->thread_table()
                  .FindById(tables::ThreadTable::Id(utid))
                  ->upid();
  PERFETTO_CHECK(upid.has_value());
  return *upid;
}

base::Status RecordParser::UpdateCounters(const Sample& sample) {
  if (!sample.read_groups.empty()) {
    return UpdateCountersInReadGroups(sample);
  }

  if (!sample.period.has_value() && !sample.attr->sample_period().has_value()) {
    return base::ErrStatus("No period for sample");
  }

  uint64_t period = sample.period.has_value() ? *sample.period
                                              : *sample.attr->sample_period();
  sample.attr->GetOrCreateCounter(sample.cpu)
      .AddDelta(sample.trace_ts, static_cast<double>(period));
  return base::OkStatus();
}

base::Status RecordParser::UpdateCountersInReadGroups(const Sample& sample) {
  for (const auto& entry : sample.read_groups) {
    RefPtr<PerfEventAttr> attr =
        sample.perf_invocation->FindAttrForEventId(*entry.event_id);
    if (PERFETTO_UNLIKELY(!attr)) {
      return base::ErrStatus("No perf_event_attr for id %" PRIu64,
                             *entry.event_id);
    }
    attr->GetOrCreateCounter(sample.cpu)
        .AddCount(sample.trace_ts, static_cast<double>(entry.value));
  }
  return base::OkStatus();
}

DummyMemoryMapping* RecordParser::GetDummyMapping(UniquePid upid) {
  if (auto* it = dummy_mappings_.Find(upid); it) {
    return *it;
  }

  DummyMemoryMapping* mapping = &mapping_tracker_->CreateDummyMapping("");
  dummy_mappings_.Insert(upid, mapping);
  return mapping;
}

}  // namespace perfetto::trace_processor::perf_importer
