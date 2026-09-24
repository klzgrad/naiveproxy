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

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/profiler_sample_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/perf_sample_tracker.h"
#include "src/trace_processor/importers/proto/profile_packet_sequence_state.h"
#include "src/trace_processor/importers/proto/profile_packet_utils.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/stack_profile_sequence_state.h"
#include "src/trace_processor/importers/proto/track_event_sequence_state.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/build_id.h"
#include "src/trace_processor/util/clock_synchronizer.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"
#include "protos/perfetto/trace/profiling/smaps.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

namespace {

class StreamingProfileSink
    : public TraceSorter::Sink<StreamingProfileSampleEvent,
                               StreamingProfileSink> {
 public:
  explicit StreamingProfileSink(ProfileModule* module) : module_(module) {}
  void Parse(int64_t ts, StreamingProfileSampleEvent event) {
    module_->ParseStreamingProfileSample(ts, std::move(event));
  }

 private:
  ProfileModule* module_;
};

struct InternedSmapsPath {
  StringId path_id = kNullStringId;
  StringId trimmed_path_id = kNullStringId;
  bool is_deleted = false;
};

// Interns the mapping name, and a variant with any " (deleted)" suffix
// stripped as that's how the kernel reports file-backed mappings where the file
// has since been deleted.
InternedSmapsPath InternSmapsPath(TraceProcessorContext* context,
                                  base::StringView path) {
  static const base::StringView kDeletedSuffix(" (deleted)");
  InternedSmapsPath ret;
  ret.path_id = context->storage->InternString(path);
  base::StringView trimmed = path;
  if (path.EndsWith(kDeletedSuffix)) {
    trimmed = path.substr(0, path.size() - kDeletedSuffix.size());
    ret.is_deleted = true;
  }
  ret.trimmed_path_id = context->storage->InternString(trimmed);
  return ret;
}

}  // namespace

using perfetto::protos::pbzero::TracePacket;
using protozero::ConstBytes;

ProfileModule::ProfileModule(ProtoImporterModuleContext* module_context,
                             TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      chrome_source_id_(context->storage->InternString("chrome")),
      linux_perf_source_id_(context->storage->InternString("linux.perf")),
      perf_sample_tracker_(context),
      streaming_profile_stream_(context->sorter->CreateStream(
          std::make_unique<StreamingProfileSink>(this))) {
  RegisterForField(TracePacket::kStreamingProfilePacketFieldNumber);
  RegisterForField(TracePacket::kPerfSampleFieldNumber);
  RegisterForField(TracePacket::kProfilePacketFieldNumber);
  RegisterForField(TracePacket::kModuleSymbolsFieldNumber);
  RegisterForField(TracePacket::kSmapsPacketFieldNumber);
}

ProfileModule::~ProfileModule() = default;

ModuleResult ProfileModule::TokenizePacket(const TokenizePacketArgs& args) {
  switch (args.field.id()) {
    case TracePacket::kStreamingProfilePacketFieldNumber:
      return TokenizeStreamingProfilePacket(
          std::move(args.state), args.packet,
          args.field.Cast<TracePacket::kStreamingProfilePacket>());
  }
  return ModuleResult::Ignored();
}

void ProfileModule::ParseField(const ParseFieldArgs& args) {
  switch (args.field.id()) {
    case TracePacket::kPerfSampleFieldNumber:
      ParsePerfSample(args.ts, args.data.sequence_state.get(), args.decoder,
                      args.field);
      return;
    case TracePacket::kProfilePacketFieldNumber:
      ParseProfilePacket(args.ts, args.data.sequence_state.get(),
                         args.field.Cast<TracePacket::kProfilePacket>());
      return;
    case TracePacket::kModuleSymbolsFieldNumber:
      ParseModuleSymbols(args.field.Cast<TracePacket::kModuleSymbols>());
      return;
    case TracePacket::kSmapsPacketFieldNumber:
      ParseSmapsPacket(args.ts, args.field.Cast<TracePacket::kSmapsPacket>());
      return;
  }
}

ModuleResult ProfileModule::TokenizeStreamingProfilePacket(
    RefPtr<PacketSequenceStateGeneration> sequence_state,
    TraceBlobView* packet,
    ConstBytes streaming_profile_packet) {
  protos::pbzero::StreamingProfilePacket::Decoder decoder(
      streaming_profile_packet.data, streaming_profile_packet.size);

  // We have to resolve the timestamps of a StreamingProfilePacket during
  // tokenization. If we did this during parsing instead, the tokenization of a
  // subsequent ThreadDescriptor with a new reference timestamp would cause us
  // to later calculate timestamps based on the wrong reference value during
  // parsing. Each sample is pushed through the sorter individually at its
  // resolved timestamp so that samples sort correctly with respect to all
  // other events; pid/tid resolution and callstack interning still happen at
  // parse time, via the sequence state carried by the event.
  auto* track_event = sequence_state->GetCustomState<TrackEventSequenceState>();
  int64_t packet_ts =
      track_event->IncrementAndGetTrackEventTimeNs(/*delta_ns=*/0);
  if (PERFETTO_UNLIKELY(packet_ts < 0)) {
    context_->import_logs_tracker->RecordTokenizationLog(
        stats::streaming_profile_invalid_timestamp, packet->offset());
    return ModuleResult::Handled();
  }

  std::optional<int64_t> trace_ts = context_->clock_tracker->ToTraceTime(
      ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_MONOTONIC), packet_ts);
  if (trace_ts)
    packet_ts = *trace_ts;

  int64_t sample_ts = packet_ts;
  auto timestamp_it = decoder.timestamp_delta_us();
  for (auto callstack_it = decoder.callstack_iid(); callstack_it;
       ++callstack_it, ++timestamp_it) {
    if (!timestamp_it) {
      context_->import_logs_tracker->RecordTokenizationLog(
          stats::stackprofile_parser_error, packet->offset());
      break;
    }
    int64_t delta_ns = base::SaturatingMultiply(*timestamp_it, 1000);
    int64_t timestamp = track_event->IncrementAndGetTrackEventTimeNs(delta_ns);
    if (PERFETTO_UNLIKELY(timestamp < 0)) {
      context_->import_logs_tracker->RecordTokenizationLog(
          stats::streaming_profile_invalid_timestamp, packet->offset());
      return ModuleResult::Handled();
    }
    sample_ts = base::SaturatingAdd(sample_ts, delta_ns);
    streaming_profile_stream_->Push(
        sample_ts, StreamingProfileSampleEvent{sequence_state, *callstack_it,
                                               decoder.process_priority()});
  }
  // Keep advancing the sequence clock over any trailing deltas so subsequent
  // packets on this sequence resolve their reference timestamp correctly.
  for (; timestamp_it; ++timestamp_it) {
    int64_t timestamp = track_event->IncrementAndGetTrackEventTimeNs(
        base::SaturatingMultiply(*timestamp_it, 1000));
    if (PERFETTO_UNLIKELY(timestamp < 0)) {
      context_->import_logs_tracker->RecordTokenizationLog(
          stats::streaming_profile_invalid_timestamp, packet->offset());
      return ModuleResult::Handled();
    }
  }
  return ModuleResult::Handled();
}

void ProfileModule::ParseStreamingProfileSample(
    int64_t ts,
    StreamingProfileSampleEvent event) {
  PacketSequenceStateGeneration* sequence_state = event.sequence_state.get();
  ProcessTracker* procs = context_->process_tracker.get();
  StackProfileSequenceState& stack_profile_sequence_state =
      *sequence_state->GetCustomState<StackProfileSequenceState>();

  const auto& thread = sequence_state->thread_descriptor();
  uint32_t pid = static_cast<uint32_t>(thread.pid());
  uint32_t tid = static_cast<uint32_t>(thread.tid());
  const UniqueTid utid = procs->UpdateThread(tid, pid);
  const UniquePid upid = procs->GetOrCreateProcess(pid);

  auto opt_cs_id = stack_profile_sequence_state.FindOrInsertCallstack(
      sequence_state, upid, event.callstack_iid);
  if (!opt_cs_id) {
    context_->import_logs_tracker->RecordParserLog(
        stats::stackprofile_parser_error, ts,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(context_->storage->InternString("callstack_iid"),
                          Variadic::UnsignedInteger(event.callstack_iid));
        });
    return;
  }

  tables::ProfilerSampleTable::Row row;
  row.ts = ts;
  row.source = chrome_source_id_;
  tables::ProfilerTaskContextTable::Row task_context;
  task_context.utid = utid;
  task_context.upid = upid;
  row.task_context_id =
      context_->profiler_sample_tracker->InternTaskContext(task_context);
  row.callsite_id = *opt_cs_id;
  auto sample_id = context_->profiler_sample_tracker->AddSample(row);
  if (event.process_priority != 0) {
    context_->storage->mutable_chrome_stack_sample_extras_table()->Insert(
        {sample_id, event.process_priority});
  }
}

void ProfileModule::ParsePerfSample(
    int64_t ts,
    PacketSequenceStateGeneration* sequence_state,
    const SelectiveTracePacketDecoder& decoder,
    const TracePacketField& field) {
  using PerfSample = protos::pbzero::PerfSample;
  PerfSample::Decoder sample(field.Cast<TracePacket::kPerfSample>());

  uint32_t seq_id = decoder.trusted_packet_sequence_id();
  PerfSampleTracker::SamplingStreamInfo sampling_stream =
      perf_sample_tracker_.GetSamplingStreamInfo(
          seq_id, sample.cpu(), sequence_state->GetTracePacketDefaults());

  // Not a sample, but an indication of data loss in the ring buffer shared with
  // the kernel.
  if (sample.kernel_records_lost() > 0) {
    PERFETTO_DCHECK(sample.pid() == 0);

    context_->stats_tracker->IncrementIndexedStats(
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
      context_->stats_tracker->SetIndexedStats(
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
        context_->stats_tracker->IncrementStats(stats::perf_samples_skipped);
        break;
      case (PerfSample::PROFILER_SKIP_UNWIND_ENQUEUE):
        context_->stats_tracker->IncrementStats(
            stats::perf_samples_skipped_dataloss);
        break;
      default:
        break;
    }
  }

  // Populate the |perf_sample| table with everything except the recorded
  // counter values, which go to |counter|.
  // Collect counter IDs for counter set association
  std::vector<CounterId> counter_ids;

  std::optional<CounterId> timebase_counter_id;
  if (sample.has_timebase_count()) {
    timebase_counter_id = context_->event_tracker->PushCounter(
        ts, static_cast<double>(sample.timebase_count()),
        sampling_stream.timebase_track_id);
  }
  if (timebase_counter_id) {
    counter_ids.push_back(*timebase_counter_id);
  }

  if (sample.has_follower_counts()) {
    auto track_it = sampling_stream.follower_track_ids.begin();
    auto track_end = sampling_stream.follower_track_ids.end();
    for (auto it = sample.follower_counts(); it && track_it != track_end;
         ++it, ++track_it) {
      auto follower_counter_id = context_->event_tracker->PushCounter(
          ts, static_cast<double>(*it), *track_it);
      if (follower_counter_id) {
        counter_ids.push_back(*follower_counter_id);
      }
    }
  }

  std::optional<uint32_t> counter_set_id =
      context_->profiler_sample_tracker->AddCounterSet(counter_ids);

  const UniqueTid utid =
      context_->process_tracker->UpdateThread(sample.tid(), sample.pid());
  const UniquePid upid =
      context_->process_tracker->GetOrCreateProcess(sample.pid());

  std::optional<CallsiteId> cs_id;
  StackProfileSequenceState& stack_profile_sequence_state =
      *sequence_state->GetCustomState<StackProfileSequenceState>();
  if (sample.has_callstack_iid()) {
    uint64_t callstack_iid = sample.callstack_iid();
    cs_id = stack_profile_sequence_state.FindOrInsertCallstack(
        sequence_state, upid, callstack_iid);
  }

  using protos::pbzero::Profiling;
  TraceStorage* storage = context_->storage.get();

  auto cpu_mode = static_cast<Profiling::CpuMode>(sample.cpu_mode());
  std::optional<StringPool::Id> cpu_mode_id;
  if (cpu_mode != Profiling::MODE_UNKNOWN) {
    cpu_mode_id =
        storage->InternString(ProfilePacketUtils::StringifyCpuMode(cpu_mode));
  }

  std::optional<StringPool::Id> unwind_error_id;
  if (sample.has_unwind_error()) {
    auto unwind_error =
        static_cast<Profiling::StackUnwindError>(sample.unwind_error());
    unwind_error_id = storage->InternString(
        ProfilePacketUtils::StringifyStackUnwindError(unwind_error));
  }

  tables::ProfilerSampleTable::Row row;
  row.ts = ts;
  row.source = linux_perf_source_id_;
  tables::ProfilerTaskContextTable::Row task_context;
  task_context.utid = utid;
  task_context.upid = upid;
  row.task_context_id =
      context_->profiler_sample_tracker->InternTaskContext(task_context);
  tables::ProfilerExecutionContextTable::Row execution_context;
  if (sample.has_cpu()) {
    execution_context.ucpu =
        context_->cpu_tracker->GetOrCreateCpu(sample.cpu()).value;
  }
  execution_context.cpu_mode = cpu_mode_id;
  if (execution_context.ucpu || execution_context.cpu_mode) {
    row.execution_context_id =
        context_->profiler_sample_tracker->InternExecutionContext(
            execution_context);
  }
  row.callsite_id = cs_id;
  row.unwind_error = unwind_error_id;
  row.session_id = sampling_stream.perf_session_id;
  row.counter_set_id = counter_set_id;
  context_->profiler_sample_tracker->AddSample(row);
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

    // End of the window: the state this dump represents.
    std::optional<int64_t> maybe_window_end =
        context_->clock_tracker->ToTraceTime(
            ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_MONOTONIC_COARSE),
            static_cast<int64_t>(entry.timestamp()));
    if (!maybe_window_end)
      continue;

    int64_t window_end = *maybe_window_end;

    // Start of the window. Older producers don't emit it, so the window
    // collapses to a point (start == end), preserving the previous behaviour.
    int64_t window_start = window_end;
    if (entry.has_start_timestamp()) {
      std::optional<int64_t> maybe_window_start =
          context_->clock_tracker->ToTraceTime(
              ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_MONOTONIC_COARSE),
              static_cast<int64_t>(entry.start_timestamp()));
      if (maybe_window_start)
        window_start = *maybe_window_start;
    }

    int pid = static_cast<int>(entry.pid());
    context_->stats_tracker->SetIndexedStats(
        stats::heapprofd_last_profile_timestamp, pid, ts);

    // The heap this dump is for. Older producers (pre aosp/1348782) don't emit
    // a name; the heap_profile row leaves it null and the allocations below
    // fall back to "unknown" (for those older traces this was always the native
    // heap profiler (libc.malloc)).
    std::optional<StringId> heap_name;
    if (entry.heap_name().size != 0)
      heap_name = context_->storage->InternString(entry.heap_name());

    // One heap_profile row per (dump, heap), deduped across the continued
    // packets that repeat the dump header. ts_end is the dump (allocation)
    // timestamp, so heap_profile_allocation joins via
    // (upid, ts == heap_profile.ts_end).
    UniquePid upid = context_->process_tracker->GetOrCreateProcess(
        static_cast<uint32_t>(entry.pid()));
    if (seen_heap_profiles_.Insert({upid, window_end, heap_name}, nullptr)
            .second) {
      tables::HeapProfileTable::Row row;
      row.ts = window_start;
      row.ts_end = window_end;
      row.dur = window_end - window_start;
      row.upid = upid;
      row.heap_name = heap_name;
      context_->storage->mutable_heap_profile_table()->Insert(row);
    }

    if (entry.disconnected())
      context_->stats_tracker->IncrementIndexedStats(
          stats::heapprofd_client_disconnected, pid);
    if (entry.buffer_corrupted())
      context_->stats_tracker->IncrementIndexedStats(
          stats::heapprofd_buffer_corrupted, pid);
    if (entry.buffer_overran() ||
        entry.client_error() ==
            protos::pbzero::ProfilePacket::ProcessHeapSamples::
                CLIENT_ERROR_HIT_TIMEOUT) {
      context_->stats_tracker->IncrementIndexedStats(
          stats::heapprofd_buffer_overran, pid);
    }
    if (entry.client_error()) {
      context_->stats_tracker->SetIndexedStats(stats::heapprofd_client_error,
                                               pid, entry.client_error());
    }
    if (entry.rejected_concurrent())
      context_->stats_tracker->IncrementIndexedStats(
          stats::heapprofd_rejected_concurrent, pid);
    if (entry.hit_guardrail())
      context_->stats_tracker->IncrementIndexedStats(
          stats::heapprofd_hit_guardrail, pid);
    if (entry.orig_sampling_interval_bytes()) {
      context_->stats_tracker->SetIndexedStats(
          stats::heapprofd_sampling_interval_adjusted, pid,
          static_cast<int64_t>(entry.sampling_interval_bytes()) -
              static_cast<int64_t>(entry.orig_sampling_interval_bytes()));
    }

    protos::pbzero::ProfilePacket::ProcessStats::Decoder stats(entry.stats());
    context_->stats_tracker->IncrementIndexedStats(
        stats::heapprofd_unwind_time_us, static_cast<int>(entry.pid()),
        static_cast<int64_t>(stats.total_unwinding_time_us()));
    context_->stats_tracker->IncrementIndexedStats(
        stats::heapprofd_unwind_samples, static_cast<int>(entry.pid()),
        static_cast<int64_t>(stats.heap_samples()));
    context_->stats_tracker->IncrementIndexedStats(
        stats::heapprofd_client_spinlock_blocked, static_cast<int>(entry.pid()),
        static_cast<int64_t>(stats.client_spinlock_blocked_us()));

    // orig_sampling_interval_bytes was introduced slightly after a bug with
    // self_max_count was fixed in the producer. We use this as a proxy
    // whether or not we are getting this data from a fixed producer or not.
    bool trustworthy_max_count = entry.orig_sampling_interval_bytes() > 0;

    // Allocations require a concrete heap name; fall back to "unknown" for the
    // older producers described above.
    StringId allocation_heap_name =
        heap_name ? *heap_name : context_->storage->InternString("unknown");

    for (auto sample_it = entry.samples(); sample_it; ++sample_it) {
      protos::pbzero::ProfilePacket::HeapSample::Decoder sample(*sample_it);

      ProfilePacketSequenceState::SourceAllocation src_allocation;
      src_allocation.pid = entry.pid();
      src_allocation.heap_name = allocation_heap_name;
      src_allocation.timestamp = window_end;
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
    profile_packet_sequence_state.FinalizeProfile(sequence_state);
  }
}

void ProfileModule::ParseModuleSymbols(ConstBytes blob) {
  protos::pbzero::ModuleSymbols::Decoder module_symbols(blob.data, blob.size);
  std::optional<BuildId> build_id;
  if (module_symbols.build_id().size > 0) {
    build_id = BuildId::FromRaw(module_symbols.build_id());
  }

  auto mappings =
      context_->mapping_tracker->FindMappings(module_symbols.path(), build_id);
  if (mappings.empty()) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_mapping_id);
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
        auto rr = (*frames)[frame_id];
        rr.set_symbol_set_id(symbol_set_id);
        frame_found = true;
      }
    }

    if (!frame_found) {
      context_->stats_tracker->IncrementStats(
          stats::stackprofile_invalid_frame_id);
      continue;
    }
  }
}

void ProfileModule::ParseSmapsPacket(int64_t ts, ConstBytes blob) {
  protos::pbzero::SmapsPacket::Decoder sp(blob.data, blob.size);
  auto upid = context_->process_tracker->GetOrCreateProcess(sp.pid());

  // Newer structure-of-arrays encoding of smaps.
  if (sp.has_packed_entries()) {
    ParsePackedSmaps(ts, upid, sp.packed_entries());
    return;
  }

  for (auto it = sp.entries(); it; ++it) {
    protos::pbzero::SmapsEntry::Decoder e(*it);
    InternedSmapsPath interned =
        InternSmapsPath(context_, base::StringView(e.path()));
    context_->storage->mutable_profiler_smaps_table()->Insert(
        {upid,
         ts,
         interned.path_id,
         /*path_trimmed=*/interned.trimmed_path_id,
         /*aggregate_count=*/1u,
         /*is_deleted=*/interned.is_deleted ? 1u : 0u,
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
         static_cast<int64_t>(e.proportional_resident_kb()),
         /*rss_kb=*/int64_t{0},
         /*anonymous_kb=*/int64_t{0},
         /*pss_dirty_kb=*/int64_t{0},
         /*swap_pss_kb=*/int64_t{0}});
  }
}

void ProfileModule::ParsePackedSmaps(int64_t ts,
                                     UniquePid upid,
                                     ConstBytes blob) {
  protos::pbzero::PackedSmaps::Decoder packed(blob.data, blob.size);

  // Intern path names while keeping the list ordered.
  std::vector<InternedSmapsPath> string_table;
  for (auto it = packed.string_table(); it; ++it) {
    string_table.push_back(
        InternSmapsPath(context_, base::StringView((*it).data, (*it).size)));
  }

  // Two possible encodings, based on the config:
  // * aggregated: aggregate_count written, name_id is not (implicitly the
  //   string_table order).
  // * unaggregated: name_id indexes into string_table, aggregate_count not
  //   written.
  bool aggregated = packed.has_aggregate_count();

  bool parse_error = false;
  auto name_id_it = packed.name_id(&parse_error);
  auto agg_count_it = packed.aggregate_count(&parse_error);
  auto size_kb_it = packed.size_kb(&parse_error);
  auto rss_kb_it = packed.rss_kb(&parse_error);
  auto anonymous_kb_it = packed.anonymous_kb(&parse_error);
  auto swap_kb_it = packed.swap_kb(&parse_error);
  auto shared_clean_kb_it = packed.shared_clean_kb(&parse_error);
  auto shared_dirty_kb_it = packed.shared_dirty_kb(&parse_error);
  auto private_clean_kb_it = packed.private_clean_kb(&parse_error);
  auto private_dirty_kb_it = packed.private_dirty_kb(&parse_error);
  auto locked_kb_it = packed.locked_kb(&parse_error);
  auto pss_kb_it = packed.pss_kb(&parse_error);
  auto pss_dirty_kb_it = packed.pss_dirty_kb(&parse_error);
  auto swap_pss_kb_it = packed.swap_pss_kb(&parse_error);
  const bool has_size_kb = static_cast<bool>(size_kb_it);
  const bool has_rss_kb = static_cast<bool>(rss_kb_it);
  const bool has_anonymous_kb = static_cast<bool>(anonymous_kb_it);
  const bool has_swap_kb = static_cast<bool>(swap_kb_it);
  const bool has_shared_clean_kb = static_cast<bool>(shared_clean_kb_it);
  const bool has_shared_dirty_kb = static_cast<bool>(shared_dirty_kb_it);
  const bool has_private_clean_kb = static_cast<bool>(private_clean_kb_it);
  const bool has_private_dirty_kb = static_cast<bool>(private_dirty_kb_it);
  const bool has_locked_kb = static_cast<bool>(locked_kb_it);
  const bool has_pss_kb = static_cast<bool>(pss_kb_it);
  const bool has_pss_dirty_kb = static_cast<bool>(pss_dirty_kb_it);
  const bool has_swap_pss_kb = static_cast<bool>(swap_pss_kb_it);

  auto* table = context_->storage->mutable_profiler_smaps_table();

  // Walk the N packed repeated fields, ensuring that all iterators are valid on
  // every iteration, but accounting for the fact that not all value fields
  // might be set at all.
  size_t i = 0;
  for (;
       (aggregated || name_id_it) && (!aggregated || agg_count_it) &&
       (!has_size_kb || size_kb_it) && (!has_rss_kb || rss_kb_it) &&
       (!has_anonymous_kb || anonymous_kb_it) && (!has_swap_kb || swap_kb_it) &&
       (!has_shared_clean_kb || shared_clean_kb_it) &&
       (!has_shared_dirty_kb || shared_dirty_kb_it) &&
       (!has_private_clean_kb || private_clean_kb_it) &&
       (!has_private_dirty_kb || private_dirty_kb_it) &&
       (!has_locked_kb || locked_kb_it) && (!has_pss_kb || pss_kb_it) &&
       (!has_pss_dirty_kb || pss_dirty_kb_it) &&
       (!has_swap_pss_kb || swap_pss_kb_it);
       ++i) {
    size_t name_idx = 0;
    if (aggregated) {
      name_idx = i;
    } else {
      name_idx = *name_id_it;
      ++name_id_it;
    }
    if (PERFETTO_UNLIKELY(name_idx >= string_table.size())) {
      parse_error = true;
      break;
    }

    tables::ProfilerSmapsTable::Row row;
    row.upid = upid;
    row.ts = ts;
    row.path = string_table[name_idx].path_id;
    row.path_trimmed = string_table[name_idx].trimmed_path_id;
    row.is_deleted = string_table[name_idx].is_deleted ? 1u : 0u;
    row.aggregate_count = 1u;
    if (aggregated) {
      row.aggregate_count = *agg_count_it;
      ++agg_count_it;
    }
    if (has_size_kb) {
      row.size_kb = static_cast<int64_t>(*size_kb_it);
      ++size_kb_it;
    }
    if (has_rss_kb) {
      row.rss_kb = static_cast<int64_t>(*rss_kb_it);
      ++rss_kb_it;
    }
    if (has_anonymous_kb) {
      row.anonymous_kb = static_cast<int64_t>(*anonymous_kb_it);
      ++anonymous_kb_it;
    }
    if (has_swap_kb) {
      row.swap_kb = static_cast<int64_t>(*swap_kb_it);
      ++swap_kb_it;
    }
    if (has_shared_clean_kb) {
      row.shared_clean_resident_kb = static_cast<int64_t>(*shared_clean_kb_it);
      ++shared_clean_kb_it;
    }
    if (has_shared_dirty_kb) {
      row.shared_dirty_resident_kb = static_cast<int64_t>(*shared_dirty_kb_it);
      ++shared_dirty_kb_it;
    }
    if (has_private_clean_kb) {
      row.private_clean_resident_kb =
          static_cast<int64_t>(*private_clean_kb_it);
      ++private_clean_kb_it;
    }
    if (has_private_dirty_kb) {
      row.private_dirty_kb = static_cast<int64_t>(*private_dirty_kb_it);
      ++private_dirty_kb_it;
    }
    if (has_locked_kb) {
      row.locked_kb = static_cast<int64_t>(*locked_kb_it);
      ++locked_kb_it;
    }
    if (has_pss_kb) {
      row.proportional_resident_kb = static_cast<int64_t>(*pss_kb_it);
      ++pss_kb_it;
    }
    if (has_pss_dirty_kb) {
      row.pss_dirty_kb = static_cast<int64_t>(*pss_dirty_kb_it);
      ++pss_dirty_kb_it;
    }
    if (has_swap_pss_kb) {
      row.swap_pss_kb = static_cast<int64_t>(*swap_pss_kb_it);
      ++swap_pss_kb_it;
    }
    table->Insert(row);
  }

  // Validate that all packed fields that were set had the same number of
  // elements.
  const bool sizes_match =
      !name_id_it && !agg_count_it && !size_kb_it && !rss_kb_it &&
      !anonymous_kb_it && !swap_kb_it && !shared_clean_kb_it &&
      !shared_dirty_kb_it && !private_clean_kb_it && !private_dirty_kb_it &&
      !locked_kb_it && !pss_kb_it && !pss_dirty_kb_it && !swap_pss_kb_it &&
      (!aggregated || i == string_table.size());
  if (parse_error || !sizes_match) {
    context_->stats_tracker->IncrementStats(stats::smaps_parser_errors);
  }
}

void ProfileModule::OnEventsFullyExtracted() {
  for (auto it = context_->storage->stack_profile_mapping_table().IterateRows();
       it; ++it) {
    NullTermStringView path = context_->storage->GetString(it.name());
    NullTermStringView build_id = context_->storage->GetString(it.build_id());

    if (path.StartsWith("/data/local/tmp/") && build_id.empty()) {
      context_->stats_tracker->IncrementStats(
          stats::symbolization_tmp_build_id_not_found);
    }
  }
}

}  // namespace perfetto::trace_processor
