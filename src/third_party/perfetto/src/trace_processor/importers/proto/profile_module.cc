/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/profile_module.h"
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/perf_sample_tracker.h"
#include "src/trace_processor/importers/proto/profile_packet_sequence_state.h"
#include "src/trace_processor/importers/proto/profile_packet_utils.h"
#include "src/trace_processor/importers/proto/stack_profile_sequence_state.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"
#include "protos/perfetto/trace/profiling/smaps.pbzero.h"

namespace perfetto {
namespace trace_processor {

using perfetto::protos::pbzero::TracePacket;
using protozero::ConstBytes;

ProfileModule::ProfileModule(TraceProcessorContext* context)
    : context_(context) {
  RegisterForField(TracePacket::kStreamingProfilePacketFieldNumber, context);
  RegisterForField(TracePacket::kPerfSampleFieldNumber, context);
  RegisterForField(TracePacket::kProfilePacketFieldNumber, context);
  RegisterForField(TracePacket::kModuleSymbolsFieldNumber, context);
  RegisterForField(TracePacket::kSmapsPacketFieldNumber, context);
}

ProfileModule::~ProfileModule() = default;

ModuleResult ProfileModule::TokenizePacket(
    const TracePacket::Decoder& decoder,
    TraceBlobView* packet,
    int64_t /*packet_timestamp*/,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kStreamingProfilePacketFieldNumber:
      return TokenizeStreamingProfilePacket(std::move(state), packet,
                                            decoder.streaming_profile_packet());
  }
  return ModuleResult::Ignored();
}

void ProfileModule::ParseTracePacketData(
    const protos::pbzero::TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData& data,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kStreamingProfilePacketFieldNumber:
      ParseStreamingProfilePacket(ts, data.sequence_state.get(),
                                  decoder.streaming_profile_packet());
      return;
    case TracePacket::kPerfSampleFieldNumber:
      ParsePerfSample(ts, data.sequence_state.get(), decoder);
      return;
    case TracePacket::kProfilePacketFieldNumber:
      ParseProfilePacket(ts, data.sequence_state.get(),
                         decoder.profile_packet());
      return;
    case TracePacket::kModuleSymbolsFieldNumber:
      ParseModuleSymbols(decoder.module_symbols());
      return;
    case TracePacket::kSmapsPacketFieldNumber:
      ParseSmapsPacket(ts, decoder.smaps_packet());
      return;
  }
}

ModuleResult ProfileModule::TokenizeStreamingProfilePacket(
    RefPtr<PacketSequenceStateGeneration> sequence_state,
    TraceBlobView* packet,
    ConstBytes streaming_profile_packet) {
  protos::pbzero::StreamingProfilePacket::Decoder decoder(
      streaming_profile_packet.data, streaming_profile_packet.size);

  // We have to resolve the reference timestamp of a StreamingProfilePacket
  // during tokenization. If we did this during parsing instead, the
  // tokenization of a subsequent ThreadDescriptor with a new reference
  // timestamp would cause us to later calculate timestamps based on the wrong
  // reference value during parsing. Since StreamingProfilePackets only need to
  // be sorted correctly with respect to process/thread metadata events (so that
  // pid/tid are resolved correctly during parsing), we forward the packet as a
  // whole through the sorter, using the "root" timestamp of the packet, i.e.
  // the current timestamp of the packet sequence.
  auto packet_ts =
      sequence_state->IncrementAndGetTrackEventTimeNs(/*delta_ns=*/0);
  base::StatusOr<int64_t> trace_ts = context_->clock_tracker->ToTraceTime(
      protos::pbzero::BUILTIN_CLOCK_MONOTONIC, packet_ts);
  if (trace_ts.ok())
    packet_ts = *trace_ts;

  // Increment the sequence's timestamp by all deltas.
  for (auto timestamp_it = decoder.timestamp_delta_us(); timestamp_it;
       ++timestamp_it) {
    sequence_state->IncrementAndGetTrackEventTimeNs(*timestamp_it * 1000);
  }

  context_->sorter->PushTracePacket(packet_ts, std::move(sequence_state),
                                    std::move(*packet), context_->machine_id());
  return ModuleResult::Handled();
}

void ProfileModule::ParseStreamingProfilePacket(
    int64_t timestamp,
    PacketSequenceStateGeneration* sequence_state,
    ConstBytes streaming_profile_packet) {
  protos::pbzero::StreamingProfilePacket::Decoder packet(
      streaming_profile_packet.data, streaming_profile_packet.size);

  ProcessTracker* procs = context_->process_tracker.get();
  TraceStorage* storage = context_->storage.get();
  StackProfileSequenceState& stack_profile_sequence_state =
      *sequence_state->GetCustomState<StackProfileSequenceState>();

  uint32_t pid = static_cast<uint32_t>(sequence_state->pid());
  uint32_t tid = static_cast<uint32_t>(sequence_state->tid());
  const UniqueTid utid = procs->UpdateThread(tid, pid);
  const UniquePid upid = procs->GetOrCreateProcess(pid);

  // Iterate through timestamps and callstacks simultaneously.
  auto timestamp_it = packet.timestamp_delta_us();
  for (auto callstack_it = packet.callstack_iid(); callstack_it;
       ++callstack_it, ++timestamp_it) {
    if (!timestamp_it) {
      context_->storage->IncrementStats(stats::stackprofile_parser_error);
      PERFETTO_ELOG(
          "StreamingProfilePacket has less callstack IDs than timestamps!");
      break;
    }

    auto opt_cs_id =
        stack_profile_sequence_state.FindOrInsertCallstack(upid, *callstack_it);
    if (!opt_cs_id) {
      context_->storage->IncrementStats(stats::stackprofile_parser_error);
      continue;
    }

    // Resolve the delta timestamps based on the packet's root timestamp.
    timestamp += *timestamp_it * 1000;

    tables::CpuProfileStackSampleTable::Row sample_row{
        timestamp, *opt_cs_id, utid, packet.process_priority()};
    storage->mutable_cpu_profile_stack_sample_table()->Insert(sample_row);
  }
}

void ProfileModule::ParsePerfSample(
    int64_t ts,
    PacketSequenceStateGeneration* sequence_state,
    const TracePacket::Decoder& decoder) {
  using PerfSample = protos::pbzero::PerfSample;
  const auto& sample_raw = decoder.perf_sample();
  PerfSample::Decoder sample(sample_raw.data, sample_raw.size);

  uint32_t seq_id = decoder.trusted_packet_sequence_id();
  PerfSampleTracker::SamplingStreamInfo sampling_stream =
      context_->perf_sample_tracker->GetSamplingStreamInfo(
          seq_id, sample.cpu(), sequence_state->GetTracePacketDefaults());

  // Not a sample, but an indication of data loss in the ring buffer shared with
  // the kernel.
  if (sample.kernel_records_lost() > 0) {
    PERFETTO_DCHECK(sample.pid() == 0);

    context_->storage->IncrementIndexedStats(
        stats::perf_cpu_lost_records, static_cast<int>(sample.cpu()),
        static_cast<int64_t>(sample.kernel_records_lost()));
    return;
  }

  // Not a sample, but an event from the producer.
  // TODO(rsavitski): this stat is indexed by the session id, but the older
  // stats (see above) aren't. The indexing is relevant if a trace contains more
  // than one profiling data source. So the older stats should be changed to
  // being indexed as well.
  if (sample.has_producer_event()) {
    PerfSample::ProducerEvent::Decoder producer_event(sample.producer_event());
    if (producer_event.source_stop_reason() ==
        PerfSample::ProducerEvent::PROFILER_STOP_GUARDRAIL) {
      context_->storage->SetIndexedStats(
          stats::perf_guardrail_stop_ts,
          static_cast<int>(sampling_stream.perf_session_id.value), ts);
    }
    return;
  }

  // Sample has incomplete stack sampling payload (not necessarily an error).
  if (sample.has_sample_skipped_reason()) {
    switch (sample.sample_skipped_reason()) {
      case (PerfSample::PROFILER_SKIP_NOT_IN_SCOPE):
        // WAI, we're recording per-cpu but the sampled process was not in
        // config's scope. The counter part of the sample is still relevant.
        break;
      case (PerfSample::PROFILER_SKIP_READ_STAGE):
      case (PerfSample::PROFILER_SKIP_UNWIND_STAGE):
        context_->storage->IncrementStats(stats::perf_samples_skipped);
        break;
      case (PerfSample::PROFILER_SKIP_UNWIND_ENQUEUE):
        context_->storage->IncrementStats(stats::perf_samples_skipped_dataloss);
        break;
      default:
        break;
    }
  }

  // Populate the |perf_sample| table with everything except the recorded
  // counter values, which go to |counter|.
  context_->event_tracker->PushCounter(
      ts, static_cast<double>(sample.timebase_count()),
      sampling_stream.timebase_track_id);

  if (sample.has_follower_counts()) {
    auto track_it = sampling_stream.follower_track_ids.begin();
    auto track_end = sampling_stream.follower_track_ids.end();
    for (auto it = sample.follower_counts(); it && track_it != track_end;
         ++it, ++track_it) {
      context_->event_tracker->PushCounter(ts, static_cast<double>(*it),
                                           *track_it);
    }
  }

  const UniqueTid utid =
      context_->process_tracker->UpdateThread(sample.tid(), sample.pid());
  const UniquePid upid =
      context_->process_tracker->GetOrCreateProcess(sample.pid());

  std::optional<CallsiteId> cs_id;
  StackProfileSequenceState& stack_profile_sequence_state =
      *sequence_state->GetCustomState<StackProfileSequenceState>();
  if (sample.has_callstack_iid()) {
    uint64_t callstack_iid = sample.callstack_iid();
    cs_id =
        stack_profile_sequence_state.FindOrInsertCallstack(upid, callstack_iid);
  }

  using protos::pbzero::Profiling;
  TraceStorage* storage = context_->storage.get();

  auto cpu_mode = static_cast<Profiling::CpuMode>(sample.cpu_mode());
  StringPool::Id cpu_mode_id =
      storage->InternString(ProfilePacketUtils::StringifyCpuMode(cpu_mode));

  std::optional<StringPool::Id> unwind_error_id;
  if (sample.has_unwind_error()) {
    auto unwind_error =
        static_cast<Profiling::StackUnwindError>(sample.unwind_error());
    unwind_error_id = storage->InternString(
        ProfilePacketUtils::StringifyStackUnwindError(unwind_error));
  }
  tables::PerfSampleTable::Row sample_row(ts, utid, sample.cpu(), cpu_mode_id,
                                          cs_id, unwind_error_id,
                                          sampling_stream.perf_session_id);
  context_->storage->mutable_perf_sample_table()->Insert(sample_row);
}

void ProfileModule::ParseProfilePacket(
    int64_t ts,
    PacketSequenceStateGeneration* sequence_state,
    ConstBytes blob) {
  ProfilePacketSequenceState& profile_packet_sequence_state =
      *sequence_state->GetCustomState<ProfilePacketSequenceState>();
  protos::pbzero::ProfilePacket::Decoder packet(blob.data, blob.size);
  profile_packet_sequence_state.SetProfilePacketIndex(packet.index());

  for (auto it = packet.strings(); it; ++it) {
    protos::pbzero::InternedString::Decoder entry(*it);
    const char* str = reinterpret_cast<const char*>(entry.str().data);
    auto str_view = base::StringView(str, entry.str().size);
    profile_packet_sequence_state.AddString(entry.iid(), str_view);
  }

  for (auto it = packet.mappings(); it; ++it) {
    protos::pbzero::Mapping::Decoder entry(*it);
    profile_packet_sequence_state.AddMapping(
        entry.iid(), ProfilePacketUtils::MakeSourceMapping(entry));
  }

  for (auto it = packet.frames(); it; ++it) {
    protos::pbzero::Frame::Decoder entry(*it);
    profile_packet_sequence_state.AddFrame(
        entry.iid(), ProfilePacketUtils::MakeSourceFrame(entry));
  }

  for (auto it = packet.callstacks(); it; ++it) {
    protos::pbzero::Callstack::Decoder entry(*it);
    profile_packet_sequence_state.AddCallstack(
        entry.iid(), ProfilePacketUtils::MakeSourceCallstack(entry));
  }

  for (auto it = packet.process_dumps(); it; ++it) {
    protos::pbzero::ProfilePacket::ProcessHeapSamples::Decoder entry(*it);

    base::StatusOr<int64_t> maybe_timestamp =
        context_->clock_tracker->ToTraceTime(
            protos::pbzero::BUILTIN_CLOCK_MONOTONIC_COARSE,
            static_cast<int64_t>(entry.timestamp()));

    // ToTraceTime() increments the clock_sync_failure error stat in this case.
    if (!maybe_timestamp.ok())
      continue;

    int64_t timestamp = *maybe_timestamp;

    int pid = static_cast<int>(entry.pid());
    context_->storage->SetIndexedStats(stats::heapprofd_last_profile_timestamp,
                                       pid, ts);

    if (entry.disconnected())
      context_->storage->IncrementIndexedStats(
          stats::heapprofd_client_disconnected, pid);
    if (entry.buffer_corrupted())
      context_->storage->IncrementIndexedStats(
          stats::heapprofd_buffer_corrupted, pid);
    if (entry.buffer_overran() ||
        entry.client_error() ==
            protos::pbzero::ProfilePacket::ProcessHeapSamples::
                CLIENT_ERROR_HIT_TIMEOUT) {
      context_->storage->IncrementIndexedStats(stats::heapprofd_buffer_overran,
                                               pid);
    }
    if (entry.client_error()) {
      context_->storage->SetIndexedStats(stats::heapprofd_client_error, pid,
                                         entry.client_error());
    }
    if (entry.rejected_concurrent())
      context_->storage->IncrementIndexedStats(
          stats::heapprofd_rejected_concurrent, pid);
    if (entry.hit_guardrail())
      context_->storage->IncrementIndexedStats(stats::heapprofd_hit_guardrail,
                                               pid);
    if (entry.orig_sampling_interval_bytes()) {
      context_->storage->SetIndexedStats(
          stats::heapprofd_sampling_interval_adjusted, pid,
          static_cast<int64_t>(entry.sampling_interval_bytes()) -
              static_cast<int64_t>(entry.orig_sampling_interval_bytes()));
    }

    protos::pbzero::ProfilePacket::ProcessStats::Decoder stats(entry.stats());
    context_->storage->IncrementIndexedStats(
        stats::heapprofd_unwind_time_us, static_cast<int>(entry.pid()),
        static_cast<int64_t>(stats.total_unwinding_time_us()));
    context_->storage->IncrementIndexedStats(
        stats::heapprofd_unwind_samples, static_cast<int>(entry.pid()),
        static_cast<int64_t>(stats.heap_samples()));
    context_->storage->IncrementIndexedStats(
        stats::heapprofd_client_spinlock_blocked, static_cast<int>(entry.pid()),
        static_cast<int64_t>(stats.client_spinlock_blocked_us()));

    // orig_sampling_interval_bytes was introduced slightly after a bug with
    // self_max_count was fixed in the producer. We use this as a proxy
    // whether or not we are getting this data from a fixed producer or not.
    bool trustworthy_max_count = entry.orig_sampling_interval_bytes() > 0;

    for (auto sample_it = entry.samples(); sample_it; ++sample_it) {
      protos::pbzero::ProfilePacket::HeapSample::Decoder sample(*sample_it);

      ProfilePacketSequenceState::SourceAllocation src_allocation;
      src_allocation.pid = entry.pid();
      if (entry.heap_name().size != 0) {
        src_allocation.heap_name =
            context_->storage->InternString(entry.heap_name());
      } else {
        // After aosp/1348782 there should be a heap name associated with all
        // allocations - absence of one is likely a bug (for traces captured
        // in older builds, this was the native heap profiler (libc.malloc)).
        src_allocation.heap_name = context_->storage->InternString("unknown");
      }
      src_allocation.timestamp = timestamp;
      src_allocation.callstack_id = sample.callstack_id();
      if (sample.has_self_max()) {
        src_allocation.self_allocated = sample.self_max();
        if (trustworthy_max_count)
          src_allocation.alloc_count = sample.self_max_count();
      } else {
        src_allocation.self_allocated = sample.self_allocated();
        src_allocation.self_freed = sample.self_freed();
        src_allocation.alloc_count = sample.alloc_count();
        src_allocation.free_count = sample.free_count();
      }

      profile_packet_sequence_state.StoreAllocation(src_allocation);
    }
  }
  if (!packet.continued()) {
    profile_packet_sequence_state.FinalizeProfile();
  }
}

void ProfileModule::ParseModuleSymbols(ConstBytes blob) {
  protos::pbzero::ModuleSymbols::Decoder module_symbols(blob.data, blob.size);
  BuildId build_id = BuildId::FromRaw(module_symbols.build_id());

  auto mappings =
      context_->mapping_tracker->FindMappings(module_symbols.path(), build_id);
  if (mappings.empty()) {
    context_->storage->IncrementStats(stats::stackprofile_invalid_mapping_id);
    return;
  }
  for (auto addr_it = module_symbols.address_symbols(); addr_it; ++addr_it) {
    protos::pbzero::AddressSymbols::Decoder address_symbols(*addr_it);

    uint32_t symbol_set_id = context_->storage->symbol_table().row_count();

    bool has_lines = false;
    // Taking the last (i.e. the least interned) location if there're several.
    ArgsTranslationTable::SourceLocation last_location;
    for (auto line_it = address_symbols.lines(); line_it; ++line_it) {
      protos::pbzero::Line::Decoder line(*line_it);
      auto file_name = line.source_file_name();
      context_->storage->mutable_symbol_table()->Insert(
          {symbol_set_id, context_->storage->InternString(line.function_name()),
           file_name.size == 0 ? kNullStringId
                               : context_->storage->InternString(file_name),
           line.has_line_number() && file_name.size != 0
               ? std::make_optional(line.line_number())
               : std::nullopt});
      last_location = ArgsTranslationTable::SourceLocation{
          file_name.ToStdString(), line.function_name().ToStdString(),
          line.line_number()};
      has_lines = true;
    }
    if (!has_lines) {
      continue;
    }
    bool frame_found = false;
    for (VirtualMemoryMapping* mapping : mappings) {
      context_->args_translation_table->AddNativeSymbolTranslationRule(
          mapping->mapping_id(), address_symbols.address(), last_location);
      std::vector<FrameId> frame_ids =
          mapping->FindFrameIds(address_symbols.address());

      for (const FrameId frame_id : frame_ids) {
        auto* frames = context_->storage->mutable_stack_profile_frame_table();
        auto rr = *frames->FindById(frame_id);
        rr.set_symbol_set_id(symbol_set_id);
        frame_found = true;
      }
    }

    if (!frame_found) {
      context_->storage->IncrementStats(stats::stackprofile_invalid_frame_id);
      continue;
    }
  }
}

void ProfileModule::ParseSmapsPacket(int64_t ts, ConstBytes blob) {
  protos::pbzero::SmapsPacket::Decoder sp(blob.data, blob.size);
  auto upid = context_->process_tracker->GetOrCreateProcess(sp.pid());

  for (auto it = sp.entries(); it; ++it) {
    protos::pbzero::SmapsEntry::Decoder e(*it);
    context_->storage->mutable_profiler_smaps_table()->Insert(
        {upid, ts, context_->storage->InternString(e.path()),
         static_cast<int64_t>(e.size_kb()),
         static_cast<int64_t>(e.private_dirty_kb()),
         static_cast<int64_t>(e.swap_kb()),
         context_->storage->InternString(e.file_name()),
         static_cast<int64_t>(e.start_address()),
         static_cast<int64_t>(e.module_timestamp()),
         context_->storage->InternString(e.module_debugid()),
         context_->storage->InternString(e.module_debug_path()),
         static_cast<int32_t>(e.protection_flags()),
         static_cast<int64_t>(e.private_clean_resident_kb()),
         static_cast<int64_t>(e.shared_dirty_resident_kb()),
         static_cast<int64_t>(e.shared_clean_resident_kb()),
         static_cast<int64_t>(e.locked_kb()),
         static_cast<int64_t>(e.proportional_resident_kb())});
  }
}

void ProfileModule::NotifyEndOfFile() {
  for (auto it = context_->storage->stack_profile_mapping_table().IterateRows();
       it; ++it) {
    NullTermStringView path = context_->storage->GetString(it.name());
    NullTermStringView build_id = context_->storage->GetString(it.build_id());

    if (path.StartsWith("/data/local/tmp/") && build_id.empty()) {
      context_->storage->IncrementStats(
          stats::symbolization_tmp_build_id_not_found);
    }
  }
}

}  // namespace trace_processor
}  // namespace perfetto
