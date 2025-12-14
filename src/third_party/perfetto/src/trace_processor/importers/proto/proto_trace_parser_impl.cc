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

#include "src/trace_processor/importers/proto/proto_trace_parser_impl.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/fixed_string_writer.h"
#include "perfetto/ext/base/metatrace_events.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/etw/etw_module.h"
#include "src/trace_processor/importers/ftrace/ftrace_module.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/track_event_module.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/config/trace_config.pbzero.h"
#include "protos/perfetto/trace/chrome/chrome_trace_event.pbzero.h"
#include "protos/perfetto/trace/perfetto/perfetto_metatrace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

ProtoTraceParserImpl::ProtoTraceParserImpl(
    TraceProcessorContext* context,
    ProtoImporterModuleContext* module_context)
    : context_(context),
      module_context_(module_context),
      metatrace_id_(context->storage->InternString("metatrace")),
      data_name_id_(context->storage->InternString("data")),
      raw_chrome_metadata_event_id_(
          context->storage->InternString("chrome_event.metadata")),
      raw_chrome_legacy_system_trace_event_id_(
          context->storage->InternString("chrome_event.legacy_system_trace")),
      raw_chrome_legacy_user_trace_event_id_(
          context->storage->InternString("chrome_event.legacy_user_trace")),
      missing_metatrace_interned_string_id_(
          context->storage->InternString("MISSING STRING")) {}

ProtoTraceParserImpl::~ProtoTraceParserImpl() = default;

void ProtoTraceParserImpl::ParseTracePacket(int64_t ts, TracePacketData data) {
  const TraceBlobView& blob = data.packet;
  protos::pbzero::TracePacket::Decoder packet(blob.data(), blob.length());
  // TODO(eseckler): Propagate statuses from modules.
  auto& modules = module_context_->modules_by_field;
  for (uint32_t field_id = 1; field_id < modules.size(); ++field_id) {
    if (!modules[field_id].empty() && packet.Get(field_id).valid()) {
      for (ProtoImporterModule* module : modules[field_id])
        module->ParseTracePacketData(packet, ts, data, field_id);
      return;
    }
  }

  if (packet.has_chrome_events()) {
    ParseChromeEvents(ts, packet.chrome_events());
  }

  if (packet.has_perfetto_metatrace()) {
    ParseMetatraceEvent(ts, packet.perfetto_metatrace());
  }

  if (packet.has_trace_config()) {
    // TODO(eseckler): Propagate statuses from modules.
    protos::pbzero::TraceConfig::Decoder config(packet.trace_config());
    for (auto& module : module_context_->modules) {
      module->ParseTraceConfig(config);
    }
  }
}

void ProtoTraceParserImpl::ParseTrackEvent(int64_t ts, TrackEventData data) {
  const TraceBlobView& blob = data.trace_packet_data.packet;
  protos::pbzero::TracePacket::Decoder packet(blob.data(), blob.length());
  module_context_->track_module->ParseTrackEventData(packet, ts, data);
}

void ProtoTraceParserImpl::ParseEtwEvent(uint32_t cpu,
                                         int64_t ts,
                                         TracePacketData data) {
  PERFETTO_DCHECK(module_context_->etw_module);
  module_context_->etw_module->ParseEtwEventData(cpu, ts, data);
}

void ProtoTraceParserImpl::ParseFtraceEvent(uint32_t cpu,
                                            int64_t ts,
                                            TracePacketData data) {
  PERFETTO_DCHECK(module_context_->ftrace_module);
  module_context_->ftrace_module->ParseFtraceEventData(cpu, ts, data);
}

void ProtoTraceParserImpl::ParseInlineSchedSwitch(uint32_t cpu,
                                                  int64_t ts,
                                                  InlineSchedSwitch data) {
  PERFETTO_DCHECK(module_context_->ftrace_module);
  module_context_->ftrace_module->ParseInlineSchedSwitch(cpu, ts, data);
}

void ProtoTraceParserImpl::ParseInlineSchedWaking(uint32_t cpu,
                                                  int64_t ts,
                                                  InlineSchedWaking data) {
  PERFETTO_DCHECK(module_context_->ftrace_module);
  module_context_->ftrace_module->ParseInlineSchedWaking(cpu, ts, data);
}

void ProtoTraceParserImpl::ParseChromeEvents(int64_t ts, ConstBytes blob) {
  TraceStorage* storage = context_->storage.get();
  protos::pbzero::ChromeEventBundle::Decoder bundle(blob);
  ArgsTracker args(context_);
  if (bundle.has_metadata()) {
    tables::ChromeRawTable::Id id =
        storage->mutable_chrome_raw_table()
            ->Insert({ts, raw_chrome_metadata_event_id_, 0, 0})
            .id;
    auto inserter = args.AddArgsTo(id);

    uint32_t bundle_index =
        context_->metadata_tracker->IncrementChromeMetadataBundleCount();

    // The legacy untyped metadata is proxied via a special event in the raw
    // table to JSON export.
    for (auto it = bundle.metadata(); it; ++it) {
      protos::pbzero::ChromeMetadata::Decoder metadata(*it);
      Variadic value = Variadic::Null();
      if (metadata.has_string_value()) {
        value =
            Variadic::String(storage->InternString(metadata.string_value()));
      } else if (metadata.has_int_value()) {
        value = Variadic::Integer(metadata.int_value());
      } else if (metadata.has_bool_value()) {
        value = Variadic::Integer(metadata.bool_value());
      } else if (metadata.has_json_value()) {
        value = Variadic::Json(storage->InternString(metadata.json_value()));
      } else {
        context_->storage->IncrementStats(stats::empty_chrome_metadata);
        continue;
      }

      StringId name_id = storage->InternString(metadata.name());
      args.AddArgsTo(id).AddArg(name_id, value);

      char buffer[2048];
      base::FixedStringWriter writer(buffer, sizeof(buffer));
      writer.AppendString("cr-");
      // If we have data from multiple Chrome instances, append a suffix
      // to differentiate them.
      if (bundle_index > 1) {
        writer.AppendUnsignedInt(bundle_index);
        writer.AppendChar('-');
      }
      writer.AppendString(metadata.name());

      auto metadata_id = storage->InternString(writer.GetStringView());
      context_->metadata_tracker->SetDynamicMetadata(metadata_id, value);
    }
  }

  if (bundle.has_legacy_ftrace_output()) {
    tables::ChromeRawTable::Id id =
        storage->mutable_chrome_raw_table()
            ->Insert({ts, raw_chrome_legacy_system_trace_event_id_, 0, 0})
            .id;

    std::string data;
    for (auto it = bundle.legacy_ftrace_output(); it; ++it) {
      data += (*it).ToStdString();
    }
    Variadic value =
        Variadic::String(storage->InternString(base::StringView(data)));
    args.AddArgsTo(id).AddArg(data_name_id_, value);
  }

  if (bundle.has_legacy_json_trace()) {
    for (auto it = bundle.legacy_json_trace(); it; ++it) {
      protos::pbzero::ChromeLegacyJsonTrace::Decoder legacy_trace(*it);
      if (legacy_trace.type() !=
          protos::pbzero::ChromeLegacyJsonTrace::USER_TRACE) {
        continue;
      }
      tables::ChromeRawTable::Id id =
          storage->mutable_chrome_raw_table()
              ->Insert({ts, raw_chrome_legacy_user_trace_event_id_, 0, 0})
              .id;
      Variadic value =
          Variadic::String(storage->InternString(legacy_trace.data()));
      args.AddArgsTo(id).AddArg(data_name_id_, value);
    }
  }
}

void ProtoTraceParserImpl::ParseMetatraceEvent(int64_t ts, ConstBytes blob) {
  protos::pbzero::PerfettoMetatrace::Decoder event(blob);
  auto utid = context_->process_tracker->GetOrCreateThread(event.thread_id());

  StringId cat_id = metatrace_id_;
  for (auto it = event.interned_strings(); it; ++it) {
    protos::pbzero::PerfettoMetatrace::InternedString::Decoder interned_string(
        it->data(), it->size());
    metatrace_interned_strings_.Insert(
        interned_string.iid(),
        context_->storage->InternString(interned_string.value()));
  }

  // This function inserts the args from the proto into the args table.
  // Args inserted with the same key multiple times are treated as an array:
  // this function correctly creates the key and flat key for each arg array.
  auto args_fn = [this, &event](ArgsTracker::BoundInserter* inserter) {
    using Arg = std::pair<StringId, StringId>;

    // First, get a list of all the args so we can group them by key.
    std::vector<Arg> interned;
    for (auto it = event.args(); it; ++it) {
      protos::pbzero::PerfettoMetatrace::Arg::Decoder arg_proto(*it);
      StringId key;
      if (arg_proto.has_key_iid()) {
        key = GetMetatraceInternedString(arg_proto.key_iid());
      } else {
        key = context_->storage->InternString(arg_proto.key());
      }
      StringId value;
      if (arg_proto.has_value_iid()) {
        value = GetMetatraceInternedString(arg_proto.value_iid());
      } else {
        value = context_->storage->InternString(arg_proto.value());
      }
      interned.emplace_back(key, value);
    }

    // We stable sort instead of sorting here to avoid changing the order of the
    // args in arrays.
    std::stable_sort(interned.begin(), interned.end(),
                     [](const Arg& a, const Arg& b) {
                       return a.first.raw_id() < b.first.raw_id();
                     });

    // Compute the correct key for each arg, possibly adding an index to
    // the end of the key if needed.
    char buffer[2048];
    uint32_t current_idx = 0;
    for (auto it = interned.begin(); it != interned.end(); ++it) {
      auto next = it + 1;
      StringId key = it->first;
      StringId next_key = next == interned.end() ? kNullStringId : next->first;

      if (key != next_key && current_idx == 0) {
        inserter->AddArg(key, Variadic::String(it->second));
      } else {
        constexpr size_t kMaxIndexSize = 20;
        NullTermStringView key_str = context_->storage->GetString(key);
        if (key_str.size() >= sizeof(buffer) - kMaxIndexSize) {
          PERFETTO_DLOG("Ignoring arg with unreasonbly large size");
          continue;
        }

        base::StackString<2048> array_key("%s[%u]", key_str.c_str(),
                                          current_idx);
        StringId new_key =
            context_->storage->InternString(array_key.string_view());
        inserter->AddArg(key, new_key, Variadic::String(it->second));

        current_idx = key == next_key ? current_idx + 1 : 0;
      }
    }
  };

  if (event.has_event_id() || event.has_event_name() ||
      event.has_event_name_iid()) {
    StringId name_id;
    if (event.has_event_id()) {
      auto eid = event.event_id();
      if (eid < metatrace::EVENTS_MAX) {
        name_id = context_->storage->InternString(metatrace::kEventNames[eid]);
      } else {
        base::StackString<64> fallback("Event %u", eid);
        name_id = context_->storage->InternString(fallback.string_view());
      }
    } else if (event.has_event_name_iid()) {
      name_id = GetMetatraceInternedString(event.event_name_iid());
    } else {
      name_id = context_->storage->InternString(event.event_name());
    }
    TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
    context_->slice_tracker->Scoped(
        ts, track_id, cat_id, name_id,
        static_cast<int64_t>(event.event_duration_ns()), args_fn);
  } else if (event.has_counter_id() || event.has_counter_name()) {
    static constexpr auto kBlueprint = tracks::CounterBlueprint(
        "metatrace_counter", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(
            tracks::kThreadDimensionBlueprint,
            tracks::StringDimensionBlueprint("counter_name")),
        tracks::DynamicNameBlueprint());
    TrackId track;
    if (event.has_counter_id()) {
      auto cid = event.counter_id();
      StringId name_id;
      if (cid < metatrace::COUNTERS_MAX) {
        name_id =
            context_->storage->InternString(metatrace::kCounterNames[cid]);
      } else {
        base::StackString<64> fallback("Counter %u", cid);
        name_id = context_->storage->InternString(fallback.string_view());
      }
      track = context_->track_tracker->InternTrack(
          kBlueprint,
          tracks::Dimensions(utid, context_->storage->GetString(name_id)),
          tracks::DynamicName(name_id));
    } else {
      track = context_->track_tracker->InternTrack(
          kBlueprint, tracks::Dimensions(utid, event.counter_name()),
          tracks::DynamicName(
              context_->storage->InternString(event.counter_name())));
    }
    auto opt_id =
        context_->event_tracker->PushCounter(ts, event.counter_value(), track);
    if (opt_id) {
      ArgsTracker args_tracker(context_);
      auto inserter = args_tracker.AddArgsTo(*opt_id);
      args_fn(&inserter);
    }
  }

  if (event.has_overruns())
    context_->storage->IncrementStats(stats::metatrace_overruns);
}

StringId ProtoTraceParserImpl::GetMetatraceInternedString(uint64_t iid) {
  StringId* maybe_id = metatrace_interned_strings_.Find(iid);
  if (!maybe_id)
    return missing_metatrace_interned_string_id_;
  return *maybe_id;
}

}  // namespace perfetto::trace_processor
