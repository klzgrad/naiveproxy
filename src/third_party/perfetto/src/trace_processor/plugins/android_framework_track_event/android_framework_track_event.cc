/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/plugins/android_framework_track_event/android_framework_track_event.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/third_party/android/frameworks/base/proto/tracing/frameworks_base_track_event.pbzero.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/proto/track_event_extension_parser.h"
#include "src/trace_processor/plugins/android_framework_track_event/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/descriptors.h"

namespace perfetto::trace_processor::android_framework_track_event {
namespace {

using FBTE = ::com::android::internal::pbzero::FrameworksBaseTrackEvent;
using AndroidProcessStartEvent =
    ::com::android::internal::pbzero::AndroidProcessStartEvent;
using AndroidBinderDiedEvent =
    ::com::android::internal::pbzero::AndroidBinderDiedEvent;
using AndroidTrackEventProcessTable = tables::AndroidTrackEventProcessTable;

// Records AndroidProcessStartEvent and AndroidBinderDiedEvent into
// __intrinsic_android_track_event_process.
class Parser : public TrackEventExtensionParser {
 public:
  Parser(TrackEventExtensionParserContext* extension_parser_context,
         TraceProcessorContext* context,
         AndroidTrackEventProcessTable* table)
      : TrackEventExtensionParser(extension_parser_context),
        trace_context_(context),
        table_(table) {
    RegisterTrackEventExtension(FBTE::kProcessStartEventFieldNumber);
    RegisterTrackEventExtension(FBTE::kBinderDiedEventFieldNumber);
  }
  ~Parser() override = default;

  Result OnTrackEventSliceExtension(const TrackEventExtensionField& field,
                                    SliceId id) override {
    int64_t ts = trace_context_->storage->slice_table()[id].ts();
    switch (field.id()) {
      case FBTE::kProcessStartEventFieldNumber:
        HandleProcessStart(field.Cast<FBTE::kProcessStartEvent>(), ts);
        break;
      case FBTE::kBinderDiedEventFieldNumber:
        HandleBinderDied(field.Cast<FBTE::kBinderDiedEvent>(), ts);
        break;
      default:
        break;
    }
    return Result::kHandled;
  }

 private:
  void SetProcessMetadata(UniquePid upid, protozero::ConstBytes process_start) {
    AndroidProcessStartEvent::Decoder evt(process_start);
    if (evt.has_uid()) {
      trace_context_->process_tracker->SetProcessUid(
          upid, static_cast<uint32_t>(evt.uid()));
    }
    if (evt.has_process_name()) {
      trace_context_->process_tracker->UpdateProcessName(
          upid, trace_context_->storage->InternString(evt.process_name()),
          ProcessNamePriority::kOther);
    }
  }

  AndroidTrackEventProcessTable::RowReference GetOrInsertRow(UniquePid upid) {
    auto it_and_ins =
        upid_to_row_.Insert(upid, AndroidTrackEventProcessTable::Id{0});
    if (it_and_ins.second) {
      AndroidTrackEventProcessTable::Row row;
      row.upid = upid;
      *it_and_ins.first = table_->Insert(row).id;
    }
    return (*table_)[*it_and_ins.first];
  }

  void HandleProcessStart(protozero::ConstBytes data, int64_t ts) {
    AndroidProcessStartEvent::Decoder evt(data);
    if (!evt.has_pid()) {
      return;
    }
    UniquePid upid = trace_context_->process_tracker->GetOrCreateProcess(
        static_cast<uint32_t>(evt.pid()));
    SetProcessMetadata(upid, data);

    auto row = GetOrInsertRow(upid);
    if (evt.has_start_seq_id()) {
      row.set_start_seq_id(evt.start_seq_id());
    }
    if (evt.has_package_uid()) {
      row.set_package_uid(evt.package_uid());
    }
    if (evt.has_caller_uid()) {
      row.set_caller_uid(evt.caller_uid());
    }
    if (evt.has_defining_uid()) {
      row.set_defining_uid(evt.defining_uid());
    }
    if (!row.fw_start_ts().has_value()) {
      row.set_fw_start_ts(ts);
    }
    if (evt.has_trigger_type()) {
      row.set_trigger_type(
          InternEnum(trigger_type_cache_, ".com.android.internal.TriggerType",
                     static_cast<int32_t>(evt.trigger_type())));
    }
    if (evt.has_hosting_type()) {
      row.set_hosting_type(
          InternEnum(hosting_type_cache_, ".com.android.internal.HostingTypeId",
                     static_cast<int32_t>(evt.hosting_type())));
    }
    if (evt.has_hosting_name()) {
      row.set_hosting_name(
          trace_context_->storage->InternString(evt.hosting_name()));
    }
    if (evt.has_bind_application_delay_ms()) {
      row.set_bind_application_delay_ms(evt.bind_application_delay_ms());
    }
    if (evt.has_process_start_delay_ms()) {
      row.set_process_start_delay_ms(evt.process_start_delay_ms());
    }
  }

  void HandleBinderDied(protozero::ConstBytes data, int64_t ts) {
    AndroidBinderDiedEvent::Decoder evt(data);
    if (!evt.has_pid()) {
      return;
    }

    std::optional<UniqueTid> utid =
        trace_context_->process_tracker->GetThreadOrNull(
            static_cast<uint32_t>(evt.pid()));
    if (!utid) {
      return;
    }
    std::optional<UniquePid> upid =
        trace_context_->storage->thread_table()[*utid].upid();
    if (!upid) {
      return;
    }
    GetOrInsertRow(*upid).set_fw_end_ts(ts);
    trace_context_->process_tracker->EndThread(
        ts, static_cast<uint32_t>(evt.pid()));
  }

  StringId InternEnum(DescriptorPool::CachedDescriptor& cache,
                      const char* enum_name,
                      int32_t value) {
    auto name = trace_context_->descriptor_pool_->FindEnumString(
        cache, enum_name, value);
    return trace_context_->storage->InternString(
        base::StringView(name ? *name : std::to_string(value)));
  }

  TraceProcessorContext* trace_context_;
  DescriptorPool::CachedDescriptor trigger_type_cache_;
  DescriptorPool::CachedDescriptor hosting_type_cache_;
  AndroidTrackEventProcessTable* table_;
  base::FlatHashMap<UniquePid, AndroidTrackEventProcessTable::Id> upid_to_row_;
};

class AndroidFrameworkTrackEventPlugin
    : public Plugin<AndroidFrameworkTrackEventPlugin> {
 public:
  ~AndroidFrameworkTrackEventPlugin() override;

  void RegisterDataframes(std::vector<PluginDataframe>& out) override {
    EnsureTable();
    out.push_back(
        {&table_->dataframe(), AndroidTrackEventProcessTable::Name(), {}});
  }

  void RegisterTrackEventExtensions(
      TrackEventExtensionParserContext* ctx,
      TraceProcessorContext* trace_context) override {
    EnsureTable();
    ctx->parsers.emplace_back(
        std::make_unique<Parser>(ctx, trace_context, table_.get()));
  }

 private:
  void EnsureTable() {
    if (!table_) {
      table_ = std::make_unique<AndroidTrackEventProcessTable>(
          trace_context_->storage->mutable_string_pool());
    }
  }

  std::unique_ptr<AndroidTrackEventProcessTable> table_;
};

AndroidFrameworkTrackEventPlugin::~AndroidFrameworkTrackEventPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<AndroidFrameworkTrackEventPlugin>();
      },
      AndroidFrameworkTrackEventPlugin::kPluginId,
      AndroidFrameworkTrackEventPlugin::kDepIds.data(),
      AndroidFrameworkTrackEventPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::android_framework_track_event
