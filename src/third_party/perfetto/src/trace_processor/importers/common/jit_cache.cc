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

#include "src/trace_processor/importers/common/jit_cache.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/jit_tables_py.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

std::pair<FrameId, bool> JitCache::JittedFunction::InternFrame(
    TraceProcessorContext* context,
    FrameKey frame_key) {
  if (FrameId* id = interned_frames_.Find(frame_key); id) {
    return {*id, false};
  }

  FrameId frame_id =
      context->storage->mutable_stack_profile_frame_table()
          ->Insert({context->storage->jit_code_table()
                        .FindById(jit_code_id_)
                        ->function_name(),
                    frame_key.mapping_id,
                    static_cast<int64_t>(frame_key.rel_pc), symbol_set_id_})
          .id;
  interned_frames_.Insert(frame_key, frame_id);

  context->storage->mutable_jit_frame_table()->Insert({jit_code_id_, frame_id});

  return {frame_id, true};
}

tables::JitCodeTable::Id JitCache::LoadCode(
    int64_t timestamp,
    UniqueTid utid,
    AddressRange code_range,
    StringId function_name,
    std::optional<SourceLocation> source_location,
    TraceBlobView native_code) {
  PERFETTO_CHECK(range_.Contains(code_range));
  PERFETTO_CHECK(context_->storage->thread_table()
                     .FindById(tables::ThreadTable::Id(utid))
                     ->upid() == upid_);

  PERFETTO_CHECK(native_code.size() == 0 ||
                 native_code.size() == code_range.size());

  std::optional<uint32_t> symbol_set_id;
  if (source_location.has_value()) {
    // TODO(carlscab): Remove duplication via new SymbolTracker class
    symbol_set_id = context_->storage->symbol_table().row_count();
    context_->storage->mutable_symbol_table()->Insert(
        {*symbol_set_id, function_name, source_location->file_name,
         source_location->line_number});
  }

  auto* jit_code_table = context_->storage->mutable_jit_code_table();
  const auto jit_code_id =
      jit_code_table
          ->Insert({timestamp, std::nullopt, utid,
                    static_cast<int64_t>(code_range.start()),
                    static_cast<int64_t>(code_range.size()), function_name,
                    Base64Encode(native_code)})
          .id;

  functions_.DeleteOverlapsAndEmplace(
      [&](std::pair<const AddressRange, JittedFunction>& entry) {
        jit_code_table->FindById(entry.second.jit_code_id())
            ->set_estimated_delete_ts(timestamp);
      },
      code_range, jit_code_id, symbol_set_id);

  return jit_code_id;
}

tables::JitCodeTable::Id JitCache::MoveCode(int64_t timestamp,
                                            UniqueTid,
                                            uint64_t from_code_start,
                                            uint64_t to_code_start) {
  auto* jit_code_table = context_->storage->mutable_jit_code_table();

  auto it = functions_.Find(from_code_start);
  AddressRange old_code_range = it->first;
  JittedFunction func = std::move(it->second);
  functions_.erase(it);

  auto code_id = func.jit_code_id();
  AddressRange new_code_range(to_code_start, old_code_range.size());

  functions_.DeleteOverlapsAndEmplace(
      [&](std::pair<const AddressRange, JittedFunction>& entry) {
        jit_code_table->FindById(entry.second.jit_code_id())
            ->set_estimated_delete_ts(timestamp);
      },
      new_code_range, std::move(func));

  return code_id;
}

std::pair<FrameId, bool> JitCache::InternFrame(VirtualMemoryMapping* mapping,
                                               uint64_t rel_pc,
                                               base::StringView function_name) {
  FrameKey key{mapping->mapping_id(), rel_pc};

  if (auto it = functions_.Find(mapping->ToAddress(rel_pc));
      it != functions_.end()) {
    return it->second.InternFrame(context_, key);
  }

  if (FrameId* id = unknown_frames_.Find(key); id) {
    return {*id, false};
  }

  context_->storage->IncrementStats(stats::jit_unknown_frame);

  FrameId id =
      context_->storage->mutable_stack_profile_frame_table()
          ->Insert({context_->storage->InternString(
                        function_name.empty()
                            ? base::StringView(
                                  "[+" + base::Uint64ToHexString(rel_pc) + "]")
                            : function_name),
                    key.mapping_id, static_cast<int64_t>(rel_pc)})
          .id;
  unknown_frames_.Insert(key, id);
  return {id, true};
}

UserMemoryMapping& JitCache::CreateMapping() {
  CreateMappingParams params;
  params.memory_range = range_;
  params.name = "[jit: " + name_ + "]";
  return context_->mapping_tracker->CreateUserMemoryMapping(upid_,
                                                            std::move(params));
}

StringId JitCache::Base64Encode(const TraceBlobView& data) {
  return context_->storage->InternString(
      base::StringView(base::Base64Encode(data.data(), data.size())));
}

}  // namespace trace_processor
}  // namespace perfetto
