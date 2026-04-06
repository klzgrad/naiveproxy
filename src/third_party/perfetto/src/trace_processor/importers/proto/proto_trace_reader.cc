/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/proto_trace_reader.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/proto/default_modules.h"
#include "src/trace_processor/importers/proto/packet_analyzer.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/proto_trace_parser_impl.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/gzip_utils.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/common/trace_stats.pbzero.h"
#include "protos/perfetto/config/trace_config.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/extension_descriptor.pbzero.h"
#include "protos/perfetto/trace/perfetto/tracing_service_event.pbzero.h"
#include "protos/perfetto/trace/remote_clock_sync.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {
namespace {

using ClockId = ClockTracker::ClockId;

class TracePacketSink
    : public TraceSorter::Sink<TracePacketData, TracePacketSink> {
 public:
  explicit TracePacketSink(ProtoTraceParserImpl* parser) : parser_(parser) {}
  void Parse(int64_t ts, TracePacketData data) {
    parser_->ParseTracePacket(ts, std::move(data));
  }

 private:
  ProtoTraceParserImpl* parser_;
};
class TrackEventSink
    : public TraceSorter::Sink<TrackEventData, TrackEventSink> {
 public:
  explicit TrackEventSink(ProtoTraceParserImpl* parser) : parser_(parser) {}
  void Parse(int64_t ts, TrackEventData data) {
    parser_->ParseTrackEvent(ts, std::move(data));
  }

 private:
  ProtoTraceParserImpl* parser_;
};
class FtraceEventSink
    : public TraceSorter::Sink<TracePacketData, FtraceEventSink> {
 public:
  FtraceEventSink(ProtoTraceParserImpl* parser, uint32_t cpu)
      : parser_(parser), cpu_(cpu) {}
  void Parse(int64_t ts, TracePacketData data) {
    parser_->ParseFtraceEvent(cpu_, ts, std::move(data));
  }

 private:
  ProtoTraceParserImpl* parser_;
  uint32_t cpu_;
};
class EtwEventSink : public TraceSorter::Sink<TracePacketData, EtwEventSink> {
 public:
  EtwEventSink(ProtoTraceParserImpl* parser, uint32_t cpu)
      : parser_(parser), cpu_(cpu) {}
  void Parse(int64_t ts, TracePacketData data) {
    parser_->ParseEtwEvent(cpu_, ts, std::move(data));
  }

 private:
  ProtoTraceParserImpl* parser_;
  uint32_t cpu_;
};
class InlineSchedSwitchSink
    : public TraceSorter::Sink<InlineSchedSwitch, InlineSchedSwitchSink> {
 public:
  explicit InlineSchedSwitchSink(ProtoTraceParserImpl* impl, uint32_t cpu)
      : impl_(impl), cpu_(cpu) {}
  void Parse(int64_t ts, InlineSchedSwitch data) {
    impl_->ParseInlineSchedSwitch(cpu_, ts, data);
  }

 private:
  ProtoTraceParserImpl* impl_;
  uint32_t cpu_;
};
class InlineSchedWakingSink
    : public TraceSorter::Sink<InlineSchedWaking, InlineSchedWakingSink> {
 public:
  explicit InlineSchedWakingSink(ProtoTraceParserImpl* impl, uint32_t cpu)
      : impl_(impl), cpu_(cpu) {}
  void Parse(int64_t ts, InlineSchedWaking data) {
    impl_->ParseInlineSchedWaking(cpu_, ts, data);
  }

 private:
  ProtoTraceParserImpl* impl_;
  uint32_t cpu_;
};

}  // namespace

ProtoTraceReader::ProtoTraceReader(TraceProcessorContext* ctx)
    : context_(ctx),
      parser_(std::make_unique<ProtoTraceParserImpl>(ctx, &module_context_)),
      protovm_(ctx),
      skipped_packet_key_id_(ctx->storage->InternString("skipped_packet")),
      invalid_incremental_state_key_id_(
          ctx->storage->InternString("invalid_incremental_state")),
      packet_sequence_id_key_id_(
          ctx->storage->InternString("packet_sequence_id")) {
  module_context_.trace_packet_stream = context_->sorter->CreateStream(
      std::make_unique<TracePacketSink>(parser_.get()));
  module_context_.track_event_stream = context_->sorter->CreateStream(
      std::make_unique<TrackEventSink>(parser_.get()));
  module_context_.ftrace_stream_factory = [this](uint32_t cpu) {
    return context_->sorter->CreateStream(
        std::make_unique<FtraceEventSink>(parser_.get(), cpu));
  };
  module_context_.etw_stream_factory = [this](uint32_t cpu) {
    return context_->sorter->CreateStream(
        std::make_unique<EtwEventSink>(parser_.get(), cpu));
  };
  module_context_.inline_sched_switch_stream_factory = [this](uint32_t cpu) {
    return context_->sorter->CreateStream(
        std::make_unique<InlineSchedSwitchSink>(parser_.get(), cpu));
  };
  module_context_.inline_sched_waking_stream_factory = [this](uint32_t cpu) {
    return context_->sorter->CreateStream(
        std::make_unique<InlineSchedWakingSink>(parser_.get(), cpu));
  };
  RegisterDefaultModules(&module_context_, context_);
  if (context_->register_additional_proto_modules) {
    context_->register_additional_proto_modules(&module_context_, context_);
  }
}

ProtoTraceReader::~ProtoTraceReader() = default;

base::Status ProtoTraceReader::Parse(TraceBlobView blob) {
  return tokenizer_.Tokenize(std::move(blob), [this](TraceBlobView packet) {
    return ParsePacket(std::move(packet));
  });
}

base::Status ProtoTraceReader::ParseExtensionDescriptor(ConstBytes descriptor) {
  protos::pbzero::ExtensionDescriptor::Decoder decoder(descriptor.data,
                                                       descriptor.size);

  const uint8_t* data = nullptr;
  size_t size = 0;
  std::vector<uint8_t> decompressed;
  if (decoder.has_extension_set()) {
    auto extension = decoder.extension_set();
    data = extension.data;
    size = extension.size;
  } else if (decoder.has_extension_set_gzip()) {
    auto gzipped = decoder.extension_set_gzip();
    decompressed =
        util::GzipDecompressor::DecompressFully(gzipped.data, gzipped.size);
    if (decompressed.empty()) {
      return base::ErrStatus(
          "Failed to decompress gzipped extension descriptor");
    }
    data = decompressed.data();
    size = decompressed.size();
  } else {
    return base::OkStatus();
  }

  return context_->descriptor_pool_->AddFromFileDescriptorSet(
      data, size,
      /*skip_prefixes*/ {},
      /*merge_existing_messages=*/true);
}

base::Status ProtoTraceReader::ParsePacket(TraceBlobView packet) {
  protos::pbzero::TracePacket::Decoder decoder(packet.data(), packet.length());
  if (PERFETTO_UNLIKELY(decoder.bytes_left())) {
    return base::ErrStatus(
        "Failed to parse proto packet fully; the trace is probably corrupt.");
  }

  // Any compressed packets should have been handled by the tokenizer.
  PERFETTO_CHECK(!decoder.has_compressed_packets());

  // When the trace packet is emitted from a remote machine: parse the packet
  // using a different ProtoTraceReader instance. The packet will be parsed
  // in the context of the remote machine.
  if (PERFETTO_UNLIKELY(decoder.machine_id())) {
    if (context_->machine_id() == MachineId(kDefaultMachineId)) {
      auto [it, inserted] =
          machine_to_proto_readers_.Insert(decoder.machine_id(), nullptr);
      if (PERFETTO_UNLIKELY(inserted)) {
        auto* machine_context =
            context_->ForkContextForMachineInCurrentTrace(decoder.machine_id());
        *it = std::make_unique<ProtoTraceReader>(machine_context);
        auto parent_default = context_->clock_tracker->trace_default_clock();
        if (parent_default) {
          machine_context->clock_tracker->SetTraceDefaultClock(*parent_default);
        }
        machine_context->clock_tracker->AddDeferredIdentitySync(
            ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_BOOTTIME));
        // TODO(lalitm): this doesn't seem the right place for this but I cannot
        // think of a much better place either.
        machine_context->process_tracker->SetPidZeroIsUpidZeroIdleProcess();
      }
      return it->get()->ParsePacket(std::move(packet));
    }
  }
  // Assert that the packet is parsed using the right instance of reader.
  // machine_id is set only for remote machines.
  PERFETTO_DCHECK(decoder.has_machine_id() ==
                  (context_->machine_id() != MachineId(kDefaultMachineId)));

  // Execute ProtoVM logic right after the "machine ID fork" as detailed in
  // go/perfetto-proto-vm.
  if (decoder.has_trace_provenance()) {
    protovm_.ProcessTraceProvenancePacket(decoder.trace_provenance());
  } else if (decoder.has_protovms()) {
    protovm_.ProcessProtoVmsPacket(decoder.protovms(), packet);
  } else if (auto new_packet = protovm_.TryProcessPatch(decoder, packet);
             new_packet) {
    packet = std::move(*new_packet);
    decoder =
        protos::pbzero::TracePacket::Decoder(packet.data(), packet.length());
  }

  uint32_t seq_id = decoder.trusted_packet_sequence_id();
  auto [scoped_state, inserted] = sequence_state_.Insert(seq_id, {});
  if (decoder.has_trusted_packet_sequence_id()) {
    if (!inserted && decoder.previous_packet_dropped()) {
      ++scoped_state->previous_packet_dropped_count;
    }
  }

  if (decoder.first_packet_on_sequence()) {
    HandleFirstPacketOnSequence(seq_id);
  }

  uint32_t sequence_flags = decoder.sequence_flags();
  if (decoder.incremental_state_cleared() ||
      sequence_flags &
          protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED) {
    HandleIncrementalStateCleared(decoder, packet);
  } else if (decoder.previous_packet_dropped()) {
    HandlePreviousPacketDropped(decoder, packet);
  }

  // It is important that we parse defaults before parsing other fields such as
  // the timestamp, since the defaults could affect them.
  if (decoder.has_trace_packet_defaults()) {
    auto field = decoder.trace_packet_defaults();
    ParseTracePacketDefaults(decoder, packet.slice(field.data, field.size));
  }

  if (decoder.has_interned_data()) {
    auto field = decoder.interned_data();
    ParseInternedData(decoder, packet.slice(field.data, field.size));
  }

  if (decoder.has_clock_snapshot()) {
    return ParseClockSnapshot(decoder.clock_snapshot(), seq_id);
  }

  if (decoder.has_trace_stats()) {
    ParseTraceStats(decoder.trace_stats());
  }

  if (decoder.has_remote_clock_sync()) {
    PERFETTO_DCHECK(context_->machine_id() != MachineId(kDefaultMachineId));
    return ParseRemoteClockSync(decoder.remote_clock_sync());
  }

  if (decoder.has_service_event()) {
    PERFETTO_DCHECK(decoder.has_timestamp());
    int64_t ts = static_cast<int64_t>(decoder.timestamp());
    return ParseServiceEvent(ts, decoder.service_event());
  }

  if (decoder.has_extension_descriptor()) {
    return ParseExtensionDescriptor(decoder.extension_descriptor());
  }

  auto* state = GetIncrementalStateForPacketSequence(seq_id);
  if (decoder.sequence_flags() &
      protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE) {
    if (!seq_id) {
      return base::ErrStatus(
          "TracePacket specified SEQ_NEEDS_INCREMENTAL_STATE but the "
          "TraceWriter's sequence_id is zero (the service is "
          "probably too old)");
    }
    scoped_state->needs_incremental_state_total++;

    if (!state->IsIncrementalStateValid()) {
      if (context_->content_analyzer) {
        // Account for the skipped packet for trace proto content analysis,
        // with a special annotation.
        PacketAnalyzer::SampleAnnotation annotation;
        annotation.emplace_back(skipped_packet_key_id_,
                                invalid_incremental_state_key_id_);
        PacketAnalyzer::Get(context_)->ProcessPacket(packet, annotation);
      }
      scoped_state->needs_incremental_state_skipped++;
      context_->import_logs_tracker->RecordTokenizationError(
          stats::packet_skipped_seq_needs_incremental_state_invalid,
          packet.offset(),
          [this, seq_id](ArgsTracker::BoundInserter& inserter) {
            inserter.AddArg(packet_sequence_id_key_id_,
                            Variadic::UnsignedInteger(seq_id));
          });
      return base::OkStatus();
    }
  }

  if (context_->content_analyzer && !decoder.has_track_event()) {
    PacketAnalyzer::Get(context_)->ProcessPacket(packet, {});
  }

  if (decoder.has_trace_config()) {
    ParseTraceConfig(decoder.trace_config());
  }

  return TimestampTokenizeAndPushToSorter(std::move(packet));
}

base::Status ProtoTraceReader::TimestampTokenizeAndPushToSorter(
    TraceBlobView packet) {
  protos::pbzero::TracePacket::Decoder decoder(packet.data(), packet.length());

  uint32_t seq_id = decoder.trusted_packet_sequence_id();
  auto* state = GetIncrementalStateForPacketSequence(seq_id);

  protos::pbzero::TracePacketDefaults::Decoder* defaults =
      state->current_generation()->GetTracePacketDefaults();

  int64_t timestamp;
  if (decoder.has_timestamp()) {
    timestamp = static_cast<int64_t>(decoder.timestamp());

    // Use the packet's clock_id, falling back to sequence defaults, then
    // to the trace's default clock (e.g. BOOTTIME for proto traces).
    // If none is set, timestamp_clock_id stays 0 and no conversion
    // happens — the timestamp is assumed to be in trace time already.
    uint32_t timestamp_clock_id = decoder.timestamp_clock_id();
    if (PERFETTO_UNLIKELY(!timestamp_clock_id && defaults)) {
      timestamp_clock_id = defaults->timestamp_clock_id();
    }
    if (PERFETTO_UNLIKELY(!timestamp_clock_id)) {
      auto default_clock = context_->clock_tracker->trace_default_clock();
      if (default_clock) {
        timestamp_clock_id = default_clock->clock_id;
      }
    }

    if ((decoder.has_chrome_events() || decoder.has_chrome_metadata()) &&
        (!timestamp_clock_id ||
         timestamp_clock_id == protos::pbzero::BUILTIN_CLOCK_MONOTONIC)) {
      // Chrome event timestamps are in MONOTONIC domain, but may occur in
      // traces where (a) no clock snapshots exist or (b) no clock_id is
      // specified for their timestamps. Adjust to trace time if we have a clock
      // snapshot.
      // TODO(eseckler): Set timestamp_clock_id and emit ClockSnapshots in
      // chrome and then remove this.
      auto trace_ts = context_->clock_tracker->ToTraceTime(
          ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_MONOTONIC), timestamp);
      if (trace_ts)
        timestamp = *trace_ts;
    } else if (timestamp_clock_id) {
      // If the TracePacket specifies a non-zero clock-id, translate the
      // timestamp into the trace-time clock domain.
      ClockTracker::ClockId converted_clock_id;
      if (ClockId::IsSequenceClock(timestamp_clock_id)) {
        if (!seq_id) {
          return base::ErrStatus(
              "TracePacket specified a sequence-local clock id (%" PRIu32
              ") but the TraceWriter's sequence_id is zero (the service is "
              "probably too old)",
              timestamp_clock_id);
        }
        converted_clock_id = ClockId::Sequence(context_->trace_id().value,
                                               seq_id, timestamp_clock_id);
      } else {
        converted_clock_id = ClockId::Machine(timestamp_clock_id);
      }
      auto trace_ts = context_->clock_tracker->ToTraceTime(
          converted_clock_id, timestamp, packet.offset());
      if (!trace_ts) {
        // We need to switch to full sorting mode to ensure that packets with
        // missing timestamp are handled correctly. Don't save the packet unless
        // switching to full sorting mode succeeded.
        if (!received_eof_ && context_->sorter->SetSortingMode(
                                  TraceSorter::SortingMode::kFullSort)) {
          eof_deferred_packets_.push_back(std::move(packet));
          return base::OkStatus();
        }
        // We don't return an error here as it will cause the trace to stop
        // parsing. Instead, we rely on the stat increment (which happened
        // automatically in ToTraceTime) to inform the user about the error.
        return base::OkStatus();
      }
      timestamp = *trace_ts;
    }
  } else {
    timestamp = std::max(latest_timestamp_, context_->sorter->max_timestamp());
  }
  latest_timestamp_ = std::max(timestamp, latest_timestamp_);

  auto& modules = module_context_.modules_by_field;
  for (uint32_t field_id = 1; field_id < modules.size(); ++field_id) {
    if (!modules[field_id].empty() && decoder.Get(field_id).valid()) {
      for (ProtoImporterModule* module : modules[field_id]) {
        ModuleResult res = module->TokenizePacket(
            decoder, &packet, timestamp, state->current_generation(), field_id);
        if (!res.ignored())
          return res.ToStatus();
      }
    }
  }

  // Use parent data and length because we want to parse this again
  // later to get the exact type of the packet.
  module_context_.trace_packet_stream->Push(
      timestamp,
      TracePacketData{std::move(packet), state->current_generation()});

  return base::OkStatus();
}

void ProtoTraceReader::ParseTraceConfig(protozero::ConstBytes blob) {
  using Config = protos::pbzero::TraceConfig;
  Config::Decoder trace_config(blob);
  if (trace_config.write_into_file()) {
    if (!trace_config.flush_period_ms()) {
      context_->storage->IncrementStats(stats::config_write_into_file_no_flush);
    }
    int i = 0;
    for (auto it = trace_config.buffers(); it; ++it, ++i) {
      Config::BufferConfig::Decoder buf(*it);
      if (buf.fill_policy() == Config::BufferConfig::FillPolicy::DISCARD) {
        context_->storage->IncrementIndexedStats(
            stats::config_write_into_file_discard, i);
      }
    }
  }
}

void ProtoTraceReader::HandleIncrementalStateCleared(
    const protos::pbzero::TracePacket::Decoder& packet_decoder,
    const TraceBlobView& packet) {
  if (PERFETTO_UNLIKELY(!packet_decoder.has_trusted_packet_sequence_id())) {
    context_->import_logs_tracker->RecordTokenizationError(
        stats::incremental_state_cleared_missing_sequence_id, packet.offset());
    return;
  }
  GetIncrementalStateForPacketSequence(
      packet_decoder.trusted_packet_sequence_id())
      ->OnIncrementalStateCleared();
  for (auto& module : module_context_.modules) {
    module->OnIncrementalStateCleared(
        packet_decoder.trusted_packet_sequence_id());
  }
}

void ProtoTraceReader::HandleFirstPacketOnSequence(
    uint32_t packet_sequence_id) {
  for (auto& module : module_context_.modules) {
    module->OnFirstPacketOnSequence(packet_sequence_id);
  }
}

void ProtoTraceReader::HandlePreviousPacketDropped(
    const protos::pbzero::TracePacket::Decoder& packet_decoder,
    const TraceBlobView& packet) {
  if (PERFETTO_UNLIKELY(!packet_decoder.has_trusted_packet_sequence_id())) {
    context_->import_logs_tracker->RecordTokenizationError(
        stats::previous_packet_dropped_missing_sequence_id, packet.offset());
    return;
  }
  GetIncrementalStateForPacketSequence(
      packet_decoder.trusted_packet_sequence_id())
      ->OnPacketLoss();
}

void ProtoTraceReader::ParseTracePacketDefaults(
    const protos::pbzero::TracePacket_Decoder& packet_decoder,
    TraceBlobView trace_packet_defaults) {
  if (PERFETTO_UNLIKELY(!packet_decoder.has_trusted_packet_sequence_id())) {
    context_->import_logs_tracker->RecordTokenizationError(
        stats::trace_packet_defaults_missing_sequence_id,
        trace_packet_defaults.offset());
    return;
  }

  auto* state = GetIncrementalStateForPacketSequence(
      packet_decoder.trusted_packet_sequence_id());
  state->UpdateTracePacketDefaults(std::move(trace_packet_defaults));
}

void ProtoTraceReader::ParseInternedData(
    const protos::pbzero::TracePacket::Decoder& packet_decoder,
    TraceBlobView interned_data) {
  if (PERFETTO_UNLIKELY(!packet_decoder.has_trusted_packet_sequence_id())) {
    context_->import_logs_tracker->RecordTokenizationError(
        stats::interned_data_missing_sequence_id, interned_data.offset());
    return;
  }

  auto* state = GetIncrementalStateForPacketSequence(
      packet_decoder.trusted_packet_sequence_id());

  // Don't parse interned data entries until incremental state is valid, because
  // they could otherwise be associated with the wrong generation in the state.
  if (!state->IsIncrementalStateValid()) {
    uint32_t seq_id = packet_decoder.trusted_packet_sequence_id();
    context_->import_logs_tracker->RecordTokenizationError(
        stats::interned_data_skipped_incremental_state_invalid,
        interned_data.offset(),
        [this, seq_id](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(packet_sequence_id_key_id_,
                          Variadic::UnsignedInteger(seq_id));
        });
    return;
  }

  // Store references to interned data submessages into the sequence's state.
  protozero::ProtoDecoder decoder(interned_data.data(), interned_data.length());
  for (protozero::Field f = decoder.ReadField(); f.valid();
       f = decoder.ReadField()) {
    auto bytes = f.as_bytes();
    state->InternMessage(f.id(), interned_data.slice(bytes.data, bytes.size));
  }
}

base::Status ProtoTraceReader::ParseClockSnapshot(ConstBytes blob,
                                                  uint32_t seq_id) {
  std::vector<ClockTracker::ClockTimestamp> clock_timestamps;
  protos::pbzero::ClockSnapshot::Decoder evt(blob.data, blob.size);
  for (auto it = evt.clocks(); it; ++it) {
    protos::pbzero::ClockSnapshot::Clock::Decoder clk(*it);
    ClockTracker::ClockId clock_id;
    if (ClockId::IsSequenceClock(clk.clock_id())) {
      if (!seq_id) {
        return base::ErrStatus(
            "ClockSnapshot packet is specifying a sequence-scoped clock id "
            "(%" PRIu32 ") but the TracePacket sequence_id is zero",
            clock_id.clock_id);
      }
      clock_id =
          ClockId::Sequence(context_->trace_id().value, seq_id, clk.clock_id());
    } else {
      clock_id = ClockId::Machine(clk.clock_id());
    }
    int64_t unit_multiplier_ns =
        clk.unit_multiplier_ns()
            ? static_cast<int64_t>(clk.unit_multiplier_ns())
            : 1;
    clock_timestamps.emplace_back(clock_id, clk.timestamp(), unit_multiplier_ns,
                                  clk.is_incremental());
  }

  if (evt.primary_trace_clock()) {
    auto clock =
        ClockId::Machine(static_cast<uint32_t>(evt.primary_trace_clock()));
    context_->clock_tracker->SetTraceDefaultClock(clock);
    context_->clock_tracker->SetGlobalClock(clock);
  } else {
    // For legacy proto traces without primary_trace_clock: if the snapshot
    // contains BOOTTIME but not TRACE_FILE, infer BOOTTIME as the trace time
    // clock. This matches the old behavior where BOOTTIME was hardcoded as
    // the default for proto traces.
    bool has_boottime = false;
    bool has_trace_file = false;
    for (const auto& ct : clock_timestamps) {
      if (ct.clock.id.clock_id == protos::pbzero::BUILTIN_CLOCK_BOOTTIME) {
        has_boottime = true;
      }
      if (ct.clock.id.clock_id == protos::pbzero::BUILTIN_CLOCK_TRACE_FILE) {
        has_trace_file = true;
      }
    }
    if (has_boottime && !has_trace_file) {
      auto clock = ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
      context_->clock_tracker->SetTraceDefaultClock(clock);
      context_->clock_tracker->SetGlobalClock(clock);
    }
  }

  base::StatusOr<uint32_t> snapshot_id =
      context_->clock_tracker->AddSnapshot(clock_timestamps);
  if (!snapshot_id.ok()) {
    PERFETTO_ELOG("%s", snapshot_id.status().c_message());
    return base::OkStatus();
  }

  std::optional<int64_t> trace_time_from_snapshot =
      context_->clock_tracker->ToTraceTimeFromSnapshot(clock_timestamps);

  // Add the all the clock snapshots to the clock snapshot table.
  std::optional<int64_t> trace_ts_for_check;
  for (const auto& clock_timestamp : clock_timestamps) {
    // If the clock is incremental, we need to use 0 to map correctly to
    // |absolute_timestamp|.
    int64_t ts_to_convert =
        clock_timestamp.clock.is_incremental ? 0 : clock_timestamp.timestamp;
    // Even if we have trace time from snapshot, we still run ToTraceTime to
    // optimise future conversions. Don't pass byte_offset since we expect
    // failures here (e.g., non-monotonic clocks).
    auto opt_trace_ts = context_->clock_tracker->ToTraceTime(
        clock_timestamp.clock.id, ts_to_convert);

    int64_t trace_ts_value;
    if (!opt_trace_ts) {
      // This can happen if |AddSnapshot| failed to resolve this clock, e.g. if
      // clock is not monotonic. Try to fetch trace time from snapshot.
      if (!trace_time_from_snapshot) {
        continue;
      }
      trace_ts_value = *trace_time_from_snapshot;
    } else {
      trace_ts_value = *opt_trace_ts;
    }

    // Double check that all the clocks in this snapshot resolve to the same
    // trace timestamp value.
    PERFETTO_DCHECK(!trace_ts_for_check ||
                    trace_ts_value == trace_ts_for_check.value());
    trace_ts_for_check = trace_ts_value;

    tables::ClockSnapshotTable::Row row;
    row.ts = trace_ts_value;
    row.clock_id = static_cast<int64_t>(clock_timestamp.clock.id.clock_id);
    row.clock_value =
        clock_timestamp.timestamp * clock_timestamp.clock.unit_multiplier_ns;
    row.clock_name =
        GetBuiltinClockNameOrNull(clock_timestamp.clock.id.clock_id);
    row.snapshot_id = *snapshot_id;
    row.machine_id = context_->machine_id();

    context_->storage->mutable_clock_snapshot_table()->Insert(row);
  }
  return base::OkStatus();
}

base::Status ProtoTraceReader::ParseRemoteClockSync(ConstBytes blob) {
  protos::pbzero::RemoteClockSync::Decoder evt(blob);

  std::vector<SyncClockSnapshots> sync_clock_snapshots;
  // Decode the RemoteClockSync message into a struct for calculating offsets.
  for (auto it = evt.synced_clocks(); it; ++it) {
    sync_clock_snapshots.emplace_back();
    auto& sync_clocks = sync_clock_snapshots.back();

    protos::pbzero::RemoteClockSync::SyncedClocks::Decoder synced_clocks(*it);
    protos::pbzero::ClockSnapshot::ClockSnapshot::Decoder host_clocks(
        synced_clocks.host_clocks());
    for (auto clock_it = host_clocks.clocks(); clock_it; clock_it++) {
      protos::pbzero::ClockSnapshot::ClockSnapshot::Clock::Decoder clock(
          *clock_it);
      sync_clocks[clock.clock_id()].first = clock.timestamp();
    }

    std::vector<ClockTracker::ClockTimestamp> clock_timestamps;
    protos::pbzero::ClockSnapshot::ClockSnapshot::Decoder client_clocks(
        synced_clocks.client_clocks());
    for (auto clock_it = client_clocks.clocks(); clock_it; clock_it++) {
      protos::pbzero::ClockSnapshot::ClockSnapshot::Clock::Decoder clock(
          *clock_it);
      sync_clocks[clock.clock_id()].second = clock.timestamp();
      clock_timestamps.emplace_back(ClockId::Machine(clock.clock_id()),
                                    clock.timestamp(), 1, false);
    }

    // In addition for calculating clock offsets, client clock snapshots are
    // also added to clock tracker to emulate tracing service taking periodical
    // clock snapshots. This builds a clock conversion path from a local trace
    // time (e.g. Chrome trace time) to client builtin clock (CLOCK_MONOTONIC)
    // which can be converted to host trace time (CLOCK_BOOTTIME).
    context_->clock_tracker->AddSnapshot(clock_timestamps);
  }

  // Calculate clock offsets and report to the ClockTracker.
  auto clock_offsets = CalculateClockOffsets(sync_clock_snapshots);
  for (auto it = clock_offsets.GetIterator(); it; ++it) {
    context_->clock_tracker->SetRemoteClockOffset(it.key(), it.value());
  }
  return base::OkStatus();
}

base::FlatHashMap<ClockTracker::ClockId /*Clock Id*/, int64_t /*Offset*/>
ProtoTraceReader::CalculateClockOffsets(
    std::vector<SyncClockSnapshots>& sync_clock_snapshots) {
  base::FlatHashMap<ClockTracker::ClockId, int64_t /*Offset*/> clock_offsets;

  // The RemoteClockSync message contains a sequence of |synced_clocks|
  // messages. Each |synced_clocks| message contains pairs of ClockSnapshots
  // taken on both the client and host sides.
  //
  // The "synced_clocks" messages are emitted periodically. A single round of
  // data collection involves four snapshots:
  //   1. Client snapshot
  //   2. Host snapshot (triggered by client's IPC message)
  //   3. Client snapshot (triggered by host's IPC message)
  //   4. Host snapshot
  //
  // These four snapshots are used to estimate the clock offset between the
  // client and host for each default clock domain present in the ClockSnapshot.
  std::map<uint32_t, std::vector<int64_t>> raw_clock_offsets;
  // Remote clock syncs happen in an interval of 30 sec. 2 adjacent clock
  // snapshots belong to the same round if they happen within 30 secs.
  constexpr uint64_t clock_sync_interval_ns = 30lu * 1000000000;
  for (size_t i = 1; i < sync_clock_snapshots.size(); i++) {
    // Synced clocks are taken by client snapshot -> host snapshot.
    auto& ping_clocks = sync_clock_snapshots[i - 1];
    auto& update_clocks = sync_clock_snapshots[i];

    auto ping_client =
        ping_clocks[protos::pbzero::BuiltinClock::BUILTIN_CLOCK_BOOTTIME]
            .second;
    auto update_client =
        update_clocks[protos::pbzero::BuiltinClock::BUILTIN_CLOCK_BOOTTIME]
            .second;
    // |ping_clocks| and |update_clocks| belong to 2 different rounds of remote
    // clock sync rounds.
    if (update_client - ping_client >= clock_sync_interval_ns)
      continue;

    for (auto it = ping_clocks.GetIterator(); it; ++it) {
      const auto clock_id = it.key();
      const auto [t1h, t1c] = it.value();
      const auto [t2h, t2c] = update_clocks[clock_id];

      if (!t1h || !t1c || !t2h || !t2c)
        continue;

      int64_t offset1 =
          (static_cast<int64_t>(t1c + t2c) / 2) - static_cast<int64_t>(t1h);
      int64_t offset2 =
          static_cast<int64_t>(t2c) - (static_cast<int64_t>(t1h + t2h) / 2);

      // Clock values are taken in the order of t1c, t1h, t2c, t2h. Offset
      // calculation requires at least 3 timestamps as a round trip. We have 4,
      // which can be treated as 2 round trips:
      //   1. t1c, t1h, t2c as the round trip initiated by the client. Offset 1
      //      = (t1c + t2c) / 2 - t1h
      //   2. t1h, t2c, t2h as the round trip initiated by the host. Offset 2 =
      //      t2c - (t1h + t2h) / 2
      raw_clock_offsets[clock_id].push_back(offset1);
      raw_clock_offsets[clock_id].push_back(offset2);
    }

    // Use the average of estimated clock offsets in the clock tracker.
    for (const auto& [clock_id, offsets] : raw_clock_offsets) {
      int64_t avg_offset =
          std::accumulate(offsets.begin(), offsets.end(), 0LL) /
          static_cast<int64_t>(offsets.size());
      clock_offsets[ClockId::Machine(clock_id)] = avg_offset;
    }
  }

  return clock_offsets;
}

std::optional<StringId> ProtoTraceReader::GetBuiltinClockNameOrNull(
    int64_t clock_id) {
  switch (clock_id) {
    case protos::pbzero::ClockSnapshot::Clock::REALTIME:
      return context_->storage->InternString("REALTIME");
    case protos::pbzero::ClockSnapshot::Clock::REALTIME_COARSE:
      return context_->storage->InternString("REALTIME_COARSE");
    case protos::pbzero::ClockSnapshot::Clock::MONOTONIC:
      return context_->storage->InternString("MONOTONIC");
    case protos::pbzero::ClockSnapshot::Clock::MONOTONIC_COARSE:
      return context_->storage->InternString("MONOTONIC_COARSE");
    case protos::pbzero::ClockSnapshot::Clock::MONOTONIC_RAW:
      return context_->storage->InternString("MONOTONIC_RAW");
    case protos::pbzero::ClockSnapshot::Clock::BOOTTIME:
      return context_->storage->InternString("BOOTTIME");
    default:
      return std::nullopt;
  }
}

base::Status ProtoTraceReader::ParseServiceEvent(int64_t ts, ConstBytes blob) {
  protos::pbzero::TracingServiceEvent::Decoder tse(blob);
  if (tse.tracing_started()) {
    context_->metadata_tracker->SetMetadata(metadata::tracing_started_ns,
                                            Variadic::Integer(ts));
  }
  if (tse.tracing_disabled()) {
    context_->metadata_tracker->SetMetadata(metadata::tracing_disabled_ns,
                                            Variadic::Integer(ts));
  }
  if (tse.all_data_sources_started()) {
    context_->metadata_tracker->SetMetadata(
        metadata::all_data_source_started_ns, Variadic::Integer(ts));
  }
  if (tse.all_data_sources_flushed()) {
    context_->metadata_tracker->AppendMetadata(
        metadata::all_data_source_flushed_ns, Variadic::Integer(ts));
    context_->sorter->NotifyFlushEvent();
  }
  if (tse.read_tracing_buffers_completed()) {
    context_->sorter->NotifyReadBufferEvent();
  }
  if (tse.has_slow_starting_data_sources()) {
    protos::pbzero::TracingServiceEvent::DataSources::Decoder msg(
        tse.slow_starting_data_sources());
    for (auto it = msg.data_source(); it; it++) {
      protos::pbzero::TracingServiceEvent::DataSources::DataSource::Decoder
          data_source(*it);
      std::string formatted = data_source.producer_name().ToStdString() + " " +
                              data_source.data_source_name().ToStdString();
      context_->metadata_tracker->AppendMetadata(
          metadata::slow_start_data_source,
          Variadic::String(
              context_->storage->InternString(base::StringView(formatted))));
    }
  }
  if (tse.has_clone_started()) {
    context_->storage->SetStats(stats::traced_clone_started_timestamp_ns, ts);
  }
  if (tse.has_buffer_cloned()) {
    context_->storage->SetIndexedStats(
        stats::traced_buf_clone_done_timestamp_ns,
        static_cast<int>(tse.buffer_cloned()), ts);
  }
  return base::OkStatus();
}

void ProtoTraceReader::ParseTraceStats(ConstBytes blob) {
  protos::pbzero::TraceStats::Decoder evt(blob.data, blob.size);
  auto* storage = context_->storage.get();
  storage->SetStats(stats::traced_producers_connected,
                    static_cast<int64_t>(evt.producers_connected()));
  storage->SetStats(stats::traced_producers_seen,
                    static_cast<int64_t>(evt.producers_seen()));
  storage->SetStats(stats::traced_data_sources_registered,
                    static_cast<int64_t>(evt.data_sources_registered()));
  storage->SetStats(stats::traced_data_sources_seen,
                    static_cast<int64_t>(evt.data_sources_seen()));
  storage->SetStats(stats::traced_tracing_sessions,
                    static_cast<int64_t>(evt.tracing_sessions()));
  storage->SetStats(stats::traced_total_buffers,
                    static_cast<int64_t>(evt.total_buffers()));
  storage->SetStats(stats::traced_chunks_discarded,
                    static_cast<int64_t>(evt.chunks_discarded()));
  storage->SetStats(stats::traced_patches_discarded,
                    static_cast<int64_t>(evt.patches_discarded()));
  storage->SetStats(stats::traced_flushes_requested,
                    static_cast<int64_t>(evt.flushes_requested()));
  storage->SetStats(stats::traced_flushes_succeeded,
                    static_cast<int64_t>(evt.flushes_succeeded()));
  storage->SetStats(stats::traced_flushes_failed,
                    static_cast<int64_t>(evt.flushes_failed()));

  if (evt.has_filter_stats()) {
    protos::pbzero::TraceStats::FilterStats::Decoder fstat(evt.filter_stats());
    storage->SetStats(stats::filter_errors,
                      static_cast<int64_t>(fstat.errors()));
    storage->SetStats(stats::filter_input_bytes,
                      static_cast<int64_t>(fstat.input_bytes()));
    storage->SetStats(stats::filter_input_packets,
                      static_cast<int64_t>(fstat.input_packets()));
    storage->SetStats(stats::filter_output_bytes,
                      static_cast<int64_t>(fstat.output_bytes()));
    storage->SetStats(stats::filter_time_taken_ns,
                      static_cast<int64_t>(fstat.time_taken_ns()));
    for (auto [i, it] = std::tuple{0, fstat.bytes_discarded_per_buffer()}; it;
         ++it, ++i) {
      storage->SetIndexedStats(stats::traced_buf_bytes_filtered_out, i,
                               static_cast<int64_t>(*it));
    }
  }

  switch (evt.final_flush_outcome()) {
    case protos::pbzero::TraceStats::FINAL_FLUSH_SUCCEEDED:
      storage->IncrementStats(stats::traced_final_flush_succeeded, 1);
      break;
    case protos::pbzero::TraceStats::FINAL_FLUSH_FAILED:
      storage->IncrementStats(stats::traced_final_flush_failed, 1);
      break;
    case protos::pbzero::TraceStats::FINAL_FLUSH_UNSPECIFIED:
      break;
  }

  int buf_num = 0;
  for (auto it = evt.buffer_stats(); it; ++it, ++buf_num) {
    protos::pbzero::TraceStats::BufferStats::Decoder buf(*it);
    storage->SetIndexedStats(stats::traced_buf_buffer_size, buf_num,
                             static_cast<int64_t>(buf.buffer_size()));
    storage->SetIndexedStats(stats::traced_buf_bytes_written, buf_num,
                             static_cast<int64_t>(buf.bytes_written()));
    storage->SetIndexedStats(stats::traced_buf_bytes_overwritten, buf_num,
                             static_cast<int64_t>(buf.bytes_overwritten()));
    storage->SetIndexedStats(stats::traced_buf_bytes_read, buf_num,
                             static_cast<int64_t>(buf.bytes_read()));
    storage->SetIndexedStats(stats::traced_buf_padding_bytes_written, buf_num,
                             static_cast<int64_t>(buf.padding_bytes_written()));
    storage->SetIndexedStats(stats::traced_buf_padding_bytes_cleared, buf_num,
                             static_cast<int64_t>(buf.padding_bytes_cleared()));
    storage->SetIndexedStats(stats::traced_buf_chunks_written, buf_num,
                             static_cast<int64_t>(buf.chunks_written()));
    storage->SetIndexedStats(stats::traced_buf_chunks_rewritten, buf_num,
                             static_cast<int64_t>(buf.chunks_rewritten()));
    storage->SetIndexedStats(stats::traced_buf_chunks_overwritten, buf_num,
                             static_cast<int64_t>(buf.chunks_overwritten()));
    storage->SetIndexedStats(stats::traced_buf_chunks_discarded, buf_num,
                             static_cast<int64_t>(buf.chunks_discarded()));
    storage->SetIndexedStats(stats::traced_buf_chunks_read, buf_num,
                             static_cast<int64_t>(buf.chunks_read()));
    storage->SetIndexedStats(
        stats::traced_buf_chunks_committed_out_of_order, buf_num,
        static_cast<int64_t>(buf.chunks_committed_out_of_order()));
    storage->SetIndexedStats(stats::traced_buf_write_wrap_count, buf_num,
                             static_cast<int64_t>(buf.write_wrap_count()));
    storage->SetIndexedStats(stats::traced_buf_patches_succeeded, buf_num,
                             static_cast<int64_t>(buf.patches_succeeded()));
    storage->SetIndexedStats(stats::traced_buf_patches_failed, buf_num,
                             static_cast<int64_t>(buf.patches_failed()));
    storage->SetIndexedStats(stats::traced_buf_readaheads_succeeded, buf_num,
                             static_cast<int64_t>(buf.readaheads_succeeded()));
    storage->SetIndexedStats(stats::traced_buf_readaheads_failed, buf_num,
                             static_cast<int64_t>(buf.readaheads_failed()));
    storage->SetIndexedStats(stats::traced_buf_abi_violations, buf_num,
                             static_cast<int64_t>(buf.abi_violations()));
    storage->SetIndexedStats(
        stats::traced_buf_trace_writer_packet_loss, buf_num,
        static_cast<int64_t>(buf.trace_writer_packet_loss()));
    if (buf.has_shadow_buffer_stats()) {
      protos::pbzero::TraceStats::BufferStats::ShadowBufferStats::Decoder sbs(
          buf.shadow_buffer_stats());
      storage->SetIndexedStats(stats::traced_buf_v2s_packets_seen, buf_num,
                               static_cast<int64_t>(sbs.packets_seen()));
      storage->SetIndexedStats(stats::traced_buf_v2s_packets_in_both, buf_num,
                               static_cast<int64_t>(sbs.packets_in_both()));
      storage->SetIndexedStats(stats::traced_buf_v2s_packets_only_v1, buf_num,
                               static_cast<int64_t>(sbs.packets_only_v1()));
      storage->SetIndexedStats(stats::traced_buf_v2s_packets_only_v2, buf_num,
                               static_cast<int64_t>(sbs.packets_only_v2()));
      storage->SetIndexedStats(stats::traced_buf_v2s_patches_attempted, buf_num,
                               static_cast<int64_t>(sbs.patches_attempted()));
      storage->SetIndexedStats(
          stats::traced_buf_v2s_v1_patches_succeeded, buf_num,
          static_cast<int64_t>(sbs.v1_patches_succeeded()));
      storage->SetIndexedStats(
          stats::traced_buf_v2s_v2_patches_succeeded, buf_num,
          static_cast<int64_t>(sbs.v2_patches_succeeded()));
      storage->SetIndexedStats(stats::traced_buf_v2s_stats_version, buf_num,
                               static_cast<uint32_t>(sbs.stats_version()));
    }
  }

  struct BufStats {
    uint32_t packet_loss = 0;
    uint32_t incremental_sequences_dropped = 0;
  };
  base::FlatHashMap<int32_t, BufStats> stats_per_buffer;
  for (auto it = evt.writer_stats(); it; ++it) {
    protos::pbzero::TraceStats::WriterStats::Decoder w(*it);
    auto seq_id = static_cast<uint32_t>(w.sequence_id());
    if (auto* s = sequence_state_.Find(seq_id)) {
      auto& stats = stats_per_buffer[static_cast<int32_t>(w.buffer())];
      stats.packet_loss += s->previous_packet_dropped_count;
      stats.incremental_sequences_dropped +=
          s->needs_incremental_state_skipped > 0 &&
          s->needs_incremental_state_skipped ==
              s->needs_incremental_state_total;
    }
  }

  for (auto it = stats_per_buffer.GetIterator(); it; ++it) {
    auto& v = it.value();
    storage->SetIndexedStats(stats::traced_buf_sequence_packet_loss, it.key(),
                             v.packet_loss);
    storage->SetIndexedStats(stats::traced_buf_incremental_sequences_dropped,
                             it.key(), v.incremental_sequences_dropped);
  }
}

base::Status ProtoTraceReader::OnPushDataToSorter() {
  received_eof_ = true;
  for (auto& packet : eof_deferred_packets_) {
    RETURN_IF_ERROR(TimestampTokenizeAndPushToSorter(std::move(packet)));
  }
  return base::OkStatus();
}

void ProtoTraceReader::OnEventsFullyExtracted() {
  for (auto& module : module_context_.modules) {
    module->OnEventsFullyExtracted();
  }
}

}  // namespace perfetto::trace_processor
