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

#include "src/trace_processor/importers/common/stack_profile_tracker.h"

#include <cstddef>
#include <cstdint>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/profiler_util.h"

namespace perfetto {
namespace trace_processor {

std::vector<FrameId> StackProfileTracker::JavaFramesForName(
    NameInPackage name) const {
  if (const auto* frames = java_frames_for_name_.Find(name); frames) {
    return std::vector<FrameId>(frames->begin(), frames->end());
  }
  return {};
}

CallsiteId StackProfileTracker::InternCallsite(
    std::optional<CallsiteId> parent_callsite_id,
    FrameId frame_id,
    uint32_t depth) {
  tables::StackProfileCallsiteTable::Row row{depth, parent_callsite_id,
                                             frame_id};
  if (CallsiteId* id = callsite_unique_row_index_.Find(row); id) {
    return *id;
  }

  CallsiteId callsite_id =
      context_->storage->mutable_stack_profile_callsite_table()->Insert(row).id;
  callsite_unique_row_index_.Insert(row, callsite_id);
  return callsite_id;
}

void StackProfileTracker::OnFrameCreated(FrameId frame_id) {
  auto frame =
      *context_->storage->stack_profile_frame_table().FindById(frame_id);
  const MappingId mapping_id = frame.mapping();
  const StringId name_id = frame.name();
  const auto function_name = context_->storage->GetString(name_id);

  if (function_name.find('.') != base::StringView::npos) {
    // Java frames always contain a '.'
    base::StringView mapping_name = context_->storage->GetString(
        context_->storage->stack_profile_mapping_table()
            .FindById(mapping_id)
            ->name());
    std::optional<std::string> package =
        PackageFromLocation(context_->storage.get(), mapping_name);
    if (package) {
      NameInPackage nip{
          name_id, context_->storage->InternString(base::StringView(*package))};
      java_frames_for_name_[nip].insert(frame_id);
    } else if (mapping_name.find("/memfd:") == 0) {
      NameInPackage nip{name_id, context_->storage->InternString("memfd")};
      java_frames_for_name_[nip].insert(frame_id);
    } else {
      java_frames_with_unknown_packages_.insert(frame_id);
    }
  }
}

void StackProfileTracker::SetPackageForFrame(StringId package,
                                             FrameId frame_id) {
  auto frame =
      context_->storage->stack_profile_frame_table().FindById(frame_id);
  PERFETTO_CHECK(frame.has_value());
  NameInPackage nip{frame->name(), package};
  java_frames_for_name_[nip].insert(frame_id);
}

bool StackProfileTracker::HasFramesWithoutKnownPackage() const {
  return !java_frames_with_unknown_packages_.empty();
}

bool StackProfileTracker::FrameHasUnknownPackage(FrameId frame_id) const {
  return java_frames_with_unknown_packages_.count(frame_id) != 0;
}

}  // namespace trace_processor
}  // namespace perfetto
