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

#include "src/trace_processor/plugins/art_process_metadata_importer/art_process_metadata_module.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/profiling/art_process_metadata.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

using protos::pbzero::TracePacket;

namespace {

base::StringView ExtractMethodName(base::StringView frame) {
  size_t open_paren = frame.find('(');
  base::StringView name = open_paren == base::StringView::npos
                              ? frame
                              : frame.substr(0, open_paren);
  size_t last_space = name.rfind(' ');
  if (last_space != base::StringView::npos) {
    name = name.substr(last_space + 1);
  }
  return name;
}

UniquePid GetOrCreateProcess(TraceProcessorContext* context,
                             uint32_t pid,
                             std::optional<base::StringView> process_name,
                             std::optional<uint32_t> uid) {
  context->process_tracker->UpdateThread(pid, pid);
  UniquePid upid = context->process_tracker->GetOrCreateProcess(pid);

  if (process_name.has_value()) {
    StringId process_name_id = context->storage->InternString(*process_name);
    context->process_tracker->UpdateProcessName(
        upid, process_name_id, ProcessNamePriority::kTrackDescriptor);
  }
  if (uid.has_value()) {
    context->process_tracker->SetProcessUid(upid, *uid);
  }
  return upid;
}

void UpdatePackageList(TraceProcessorContext* context,
                       tables::PackageListTable::Cursor& cursor,
                       base::StringView package_name,
                       int64_t uid) {
  StringId package_name_id = context->storage->InternString(package_name);
  cursor.SetFilterValueUnchecked(0, package_name_id.raw_id());
  cursor.SetFilterValueUnchecked(1, uid);
  cursor.Execute();
  bool found = !cursor.Eof();
  if (!found) {
    context->storage->mutable_package_list_table()->Insert(
        {package_name_id, uid, /*debuggable*/ false,
         /*profileable_from_shell*/ false, /*version_code*/ 0});
  }
}

tables::HeapGraphTable::Id InsertHeapGraph(TraceProcessorContext* context,
                                           int64_t ts,
                                           UniquePid upid,
                                           bool is_oome) {
  auto& heap_graph_table = *context->storage->mutable_heap_graph_table();

  tables::HeapGraphTable::Row heap_graph_row;
  heap_graph_row.ts = ts;
  heap_graph_row.upid = upid;
  if (is_oome) {
    heap_graph_row.dump_reason = context->storage->InternString("OOME");
  }

  return heap_graph_table.Insert(heap_graph_row).id;
}

void InsertOomeDetails(TraceProcessorContext* context,
                       tables::HeapGraphTable::Id heap_graph_id,
                       int64_t allocation_size_bytes,
                       int64_t total_bytes_free,
                       int64_t free_bytes_until_oom,
                       std::optional<base::StringView> error_msg) {
  tables::HeapGraphJavaOomeDetailsTable::Row oome_details_row;
  oome_details_row.heap_graph_id = heap_graph_id;
  oome_details_row.allocation_size_bytes = allocation_size_bytes;
  oome_details_row.total_bytes_free = total_bytes_free;
  oome_details_row.free_bytes_until_oom = free_bytes_until_oom;

  if (error_msg.has_value()) {
    oome_details_row.error_msg = context->storage->InternString(*error_msg);
  }

  context->storage->mutable_heap_graph_java_oome_details_table()->Insert(
      oome_details_row);
}

void InsertOomeHeapGraphCallsite(
    TraceProcessorContext* context,
    tables::HeapGraphTable::Id heap_graph_id,
    uint32_t pid,
    protozero::ConstBytes stack_bytes,
    DummyMemoryMapping*& art_process_metadata_mapping) {
  protos::pbzero::JavaStack::Decoder stack_decoder(stack_bytes.data,
                                                   stack_bytes.size);

  std::vector<::protozero::ConstBytes> raw_frames;
  for (auto it = stack_decoder.frames(); it; ++it) {
    raw_frames.push_back(*it);
  }

  std::optional<CallsiteId> current_callsite_id = std::nullopt;
  uint32_t depth = 0;

  if (!art_process_metadata_mapping) {
    art_process_metadata_mapping =
        &context->mapping_tracker->CreateDummyMapping("art_process_metadata");
  }

  for (auto it = raw_frames.rbegin(); it != raw_frames.rend(); ++it) {
    protos::pbzero::JavaFrame::Decoder frame_decoder(*it);
    if (!frame_decoder.has_method_name()) {
      continue;
    }

    base::StringView raw_method_name(
        reinterpret_cast<const char*>(frame_decoder.method_name().data),
        frame_decoder.method_name().size);
    base::StringView method_name = ExtractMethodName(raw_method_name);

    std::optional<base::StringView> source_file = std::nullopt;
    if (frame_decoder.has_source_file()) {
      source_file = base::StringView(
          reinterpret_cast<const char*>(frame_decoder.source_file().data),
          frame_decoder.source_file().size);
    }

    std::optional<uint32_t> line_number = std::nullopt;
    if (frame_decoder.has_line_number()) {
      line_number = static_cast<uint32_t>(frame_decoder.line_number());
    }

    FrameId frame_id = art_process_metadata_mapping->InternDummyFrame(
        method_name, source_file, line_number);

    current_callsite_id = context->stack_profile_tracker->InternCallsite(
        current_callsite_id, frame_id, depth++);
  }

  tables::HeapGraphThreadCallsiteTable::Row callsite_row;
  callsite_row.heap_graph_id = heap_graph_id;
  UniqueTid utid = context->process_tracker->UpdateThread(pid, pid);
  callsite_row.utid = utid;
  callsite_row.callsite_id = current_callsite_id;

  context->storage->mutable_heap_graph_thread_callsite_table()->Insert(
      callsite_row);
}

}  // namespace

ArtProcessMetadataModule::ArtProcessMetadataModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      package_list_cursor_(
          context->storage->mutable_package_list_table()->CreateCursor({
              dataframe::FilterSpec{
                  tables::PackageListTable::ColumnIndex::package_name,
                  0,
                  dataframe::Eq{},
                  {},
              },
              dataframe::FilterSpec{
                  tables::PackageListTable::ColumnIndex::uid,
                  1,
                  dataframe::Eq{},
                  {},
              },
          })) {
  RegisterForField(TracePacket::kArtProcessMetadataFieldNumber);
}

ArtProcessMetadataModule::~ArtProcessMetadataModule() = default;

void ArtProcessMetadataModule::ParseField(const ParseFieldArgs& args) {
  switch (args.field.id()) {
    case TracePacket::kArtProcessMetadataFieldNumber:
      ParseArtProcessMetadata(
          args.ts, args.field.Cast<TracePacket::kArtProcessMetadata>());
      return;
  }
}

void ArtProcessMetadataModule::ParseArtProcessMetadata(
    int64_t ts,
    protozero::ConstBytes blob) {
  protos::pbzero::ArtProcessMetadata::Decoder decoder(blob.data, blob.size);

  uint32_t pid = static_cast<uint32_t>(decoder.pid());
  std::optional<base::StringView> process_name;
  if (decoder.has_process_name()) {
    process_name = decoder.process_name();
  }
  std::optional<uint32_t> uid;
  if (decoder.has_uid()) {
    uid = static_cast<uint32_t>(decoder.uid());
  }

  UniquePid upid = GetOrCreateProcess(context_, pid, process_name, uid);

  if (decoder.has_package_name() && decoder.has_uid()) {
    UpdatePackageList(context_, package_list_cursor_, decoder.package_name(),
                      decoder.uid());
  }

  bool is_oome = decoder.has_oom_allocation_size();
  tables::HeapGraphTable::Id heap_graph_id =
      InsertHeapGraph(context_, ts, upid, is_oome);

  if (!is_oome) {
    return;
  }

  std::optional<base::StringView> error_msg;
  if (decoder.has_oom_error_msg()) {
    error_msg = decoder.oom_error_msg();
  }

  InsertOomeDetails(context_, heap_graph_id,
                    static_cast<int64_t>(decoder.oom_allocation_size()),
                    static_cast<int64_t>(decoder.oom_total_bytes_free()),
                    static_cast<int64_t>(decoder.oom_free_bytes_until_oom()),
                    error_msg);

  if (decoder.has_oom_thread_java_stack()) {
    InsertOomeHeapGraphCallsite(context_, heap_graph_id, pid,
                                decoder.oom_thread_java_stack(),
                                art_process_metadata_mapping_);
  }
}

}  // namespace perfetto::trace_processor
