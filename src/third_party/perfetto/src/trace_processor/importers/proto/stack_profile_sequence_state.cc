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

#include "src/trace_processor/importers/proto/stack_profile_sequence_state.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/create_mapping_params.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/profile_packet_utils.h"
#include "src/trace_processor/importers/proto/track_event_thread_descriptor.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

namespace perfetto {
namespace trace_processor {
namespace {
// Determine wether this is the magical kernel mapping created in
// `perfetto::::profiling::Unwinder::SymbolizeKernelCallchain`
bool IsMagicalKernelMapping(const CreateMappingParams& params) {
  return params.memory_range.start() == 0 &&
         params.memory_range.length() == 0 && params.exact_offset == 0 &&
         !params.build_id.has_value() && (params.name == "/kernel");
}

// Maps a well-known Frame.Kind to the canonical lowercase label stored in
// stack_profile_frame.type. Returns nullptr for KIND_UNKNOWN / unrecognized.
const char* StringifyFrameKind(protos::pbzero::Frame::Kind kind) {
  using protos::pbzero::Frame;
  switch (kind) {
    case Frame::Kind::KIND_UNKNOWN:
      return nullptr;
    case Frame::Kind::KIND_NATIVE:
      return "native";
    case Frame::Kind::KIND_KERNEL:
      return "kernel";
    case Frame::Kind::KIND_INTERPRETED:
      return "interpreted";
    case Frame::Kind::KIND_JIT:
      return "jit";
    case Frame::Kind::KIND_GC:
      return "gc";
    case Frame::Kind::KIND_RUNTIME:
      return "runtime";
  }
  return nullptr;
}

}  // namespace

StackProfileSequenceState::StackProfileSequenceState(
    TraceProcessorContext* context)
    : context_(context) {}

StackProfileSequenceState::~StackProfileSequenceState() = default;

VirtualMemoryMapping* StackProfileSequenceState::FindOrInsertMapping(
    PacketSequenceStateGeneration* state,
    uint64_t iid) {
  const auto& thread = state->thread_descriptor();
  if (thread.valid()) {
    return FindOrInsertMappingImpl(
        state,
        context_->process_tracker->GetOrCreateProcess(
            static_cast<uint32_t>(thread.pid())),
        iid);
  }

  return FindOrInsertMappingImpl(state, std::nullopt, iid);
}

VirtualMemoryMapping* StackProfileSequenceState::FindOrInsertMappingImpl(
    PacketSequenceStateGeneration* state,
    std::optional<UniquePid> upid,
    uint64_t iid) {
  if (VirtualMemoryMapping** ptr = cached_mappings_.Find({upid, iid}); ptr) {
    return *ptr;
  }
  auto* decoder = state->LookupInternedMessage<
      protos::pbzero::InternedData::kMappingsFieldNumber,
      protos::pbzero::Mapping>(iid);
  if (!decoder) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_mapping_id);
    return nullptr;
  }

  std::vector<base::StringView> path_components;
  for (auto it = decoder->path_string_ids(); it; ++it) {
    std::optional<base::StringView> str = LookupInternedMappingPath(state, *it);
    if (!str) {
      // For backward compatibility reasons we do not return an error but
      // instead stop adding path components.
      break;
    }
    path_components.push_back(*str);
  }

  CreateMappingParams params;
  std::optional<base::StringView> build_id =
      LookupInternedBuildId(state, decoder->build_id());
  if (!build_id) {
    return nullptr;
  }
  if (!build_id->empty()) {
    params.build_id = BuildId::FromRaw(*build_id);
  }

  params.memory_range = AddressRange(decoder->start(), decoder->end());
  params.exact_offset = decoder->exact_offset();
  params.start_offset = decoder->start_offset();
  params.load_bias = decoder->load_bias();
  params.name = ProfilePacketUtils::MakeMappingName(path_components);

  VirtualMemoryMapping* mapping;

  if (IsMagicalKernelMapping(params)) {
    mapping = &context_->mapping_tracker->CreateKernelMemoryMapping(
        std::move(params));
    // A lot of tests to not set a proper mapping range
    // Dummy mappings can also be emitted (e.g. for errors during unwinding)
  } else if (params.memory_range.empty()) {
    mapping =
        &context_->mapping_tracker->InternMemoryMapping(std::move(params));
  } else if (upid.has_value()) {
    mapping = &context_->mapping_tracker->CreateUserMemoryMapping(
        *upid, std::move(params));
  } else {
    mapping =
        &context_->mapping_tracker->InternMemoryMapping(std::move(params));
  }

  cached_mappings_.Insert({upid, iid}, mapping);
  return mapping;
}

std::optional<base::StringView>
StackProfileSequenceState::LookupInternedBuildId(
    PacketSequenceStateGeneration* state,
    uint64_t iid) {
  // This should really be an error (value not set) or at the very least return
  // a null string, but for backward compatibility use an empty string instead.
  if (iid == 0) {
    return "";
  }
  std::optional<base::StringView> str = state->InternedStringView(
      protos::pbzero::InternedData::kBuildIdsFieldNumber, iid);
  if (!str) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_string_id);
    return std::nullopt;
  }
  return *str;
}

std::optional<base::StringView>
StackProfileSequenceState::LookupInternedMappingPath(
    PacketSequenceStateGeneration* state,
    uint64_t iid) {
  std::optional<base::StringView> str = state->InternedStringView(
      protos::pbzero::InternedData::kMappingPathsFieldNumber, iid);
  if (!str) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_string_id);
    return std::nullopt;
  }
  return *str;
}

std::optional<CallsiteId> StackProfileSequenceState::FindOrInsertCallstack(
    PacketSequenceStateGeneration* state,
    std::optional<UniquePid> upid,
    uint64_t iid) {
  if (CallsiteId* id = cached_callstacks_.Find({upid, iid}); id) {
    return *id;
  }
  auto* decoder = state->LookupInternedMessage<
      protos::pbzero::InternedData::kCallstacksFieldNumber,
      protos::pbzero::Callstack>(iid);
  if (!decoder) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_callstack_id);
    return std::nullopt;
  }

  std::optional<CallsiteId> callsite_id =
      FindOrInsertCallstackFromFrames(state, upid, *decoder);
  if (!callsite_id) {
    return std::nullopt;
  }

  cached_callstacks_.Insert({upid, iid}, *callsite_id);

  return callsite_id;
}

std::optional<CallsiteId>
StackProfileSequenceState::FindOrInsertCallstackFromFrames(
    PacketSequenceStateGeneration* state,
    std::optional<UniquePid> upid,
    const protos::pbzero::Callstack_Decoder& callstack) {
  std::optional<CallsiteId> parent_callsite_id;
  uint32_t depth = 0;
  for (auto it = callstack.frame_ids(); it; ++it) {
    std::optional<FrameId> frame_id = FindOrInsertFrame(state, upid, *it);
    if (!frame_id) {
      return std::nullopt;
    }
    parent_callsite_id = context_->stack_profile_tracker->InternCallsite(
        parent_callsite_id, *frame_id, depth);
    ++depth;
  }

  if (!parent_callsite_id) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_empty_callstack);
    return std::nullopt;
  }

  return parent_callsite_id;
}

std::optional<FrameId> StackProfileSequenceState::FindOrInsertFrame(
    PacketSequenceStateGeneration* state,
    std::optional<UniquePid> upid,
    uint64_t iid) {
  if (FrameId* id = cached_frames_.Find({upid, iid}); id) {
    return *id;
  }
  auto* decoder = state->LookupInternedMessage<
      protos::pbzero::InternedData::kFramesFieldNumber, protos::pbzero::Frame>(
      iid);
  if (!decoder) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_frame_id);
    return std::nullopt;
  }

  base::StringView function_name;
  if (decoder->function_name_id() != 0) {
    std::optional<base::StringView> func =
        LookupInternedFunctionName(state, decoder->function_name_id());
    if (!func) {
      return std::nullopt;
    }
    function_name = *func;
  }

  // Extract source file and line number (used by both dummy and regular frames)
  std::optional<base::StringView> source_file;
  if (decoder->has_source_path_iid()) {
    source_file = LookupInternedSourcePath(state, decoder->source_path_iid());
    if (!source_file) {
      return std::nullopt;
    }
  }

  std::optional<uint32_t> line_number;
  if (decoder->has_line_number()) {
    line_number = decoder->line_number();
  }

  // mapping_id == 0 means a "dummy" frame (no real mapping): use the dummy
  // mapping API with source file and line number. Otherwise it's a regular
  // frame in a real mapping. Both paths land in `frame_id`.
  FrameId frame_id;
  bool cache = true;
  if (decoder->mapping_id() == 0) {
    // Get or create the dummy mapping for interned frames with mapping_id = 0
    if (!dummy_mapping_for_interned_frames_) {
      dummy_mapping_for_interned_frames_ =
          &context_->mapping_tracker->CreateDummyMapping("");
    }
    frame_id = dummy_mapping_for_interned_frames_->InternDummyFrame(
        function_name, source_file, line_number);
  } else {
    VirtualMemoryMapping* mapping =
        FindOrInsertMappingImpl(state, upid, decoder->mapping_id());
    if (!mapping) {
      return std::nullopt;
    }
    // InternFrame will create the symbol entry if source_file or line_number is
    // provided
    frame_id = mapping->InternFrame(decoder->rel_pc(), function_name,
                                    source_file, line_number);
    cache = !mapping->is_jitted();
  }

  // Back-patch the frame kind (type) if the producer reported one.
  std::optional<StringId> type_id;
  if (decoder->has_kind()) {
    const char* kind = StringifyFrameKind(
        static_cast<protos::pbzero::Frame::Kind>(decoder->kind()));
    if (kind) {
      type_id = context_->storage->InternString(kind);
    }
  } else if (decoder->has_kind_str()) {
    type_id = context_->storage->InternString(decoder->kind_str());
  }
  if (type_id) {
    auto* frames = context_->storage->mutable_stack_profile_frame_table();
    auto rr = (*frames)[frame_id];
    rr.set_type(*type_id);
  }

  if (cache) {
    cached_frames_.Insert({upid, iid}, frame_id);
  }
  return frame_id;
}

std::optional<base::StringView>
StackProfileSequenceState::LookupInternedFunctionName(
    PacketSequenceStateGeneration* state,
    uint64_t iid) {
  // This should really be an error (value not set) or at the very least return
  // a null string, but for backward compatibility use an empty string instead.
  if (iid == 0) {
    return "";
  }
  std::optional<base::StringView> str = state->InternedStringView(
      protos::pbzero::InternedData::kFunctionNamesFieldNumber, iid);
  if (!str) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_string_id);
    return std::nullopt;
  }
  return *str;
}

std::optional<base::StringView>
StackProfileSequenceState::LookupInternedSourcePath(
    PacketSequenceStateGeneration* state,
    uint64_t iid) {
  if (iid == 0) {
    return std::nullopt;
  }
  std::optional<base::StringView> str = state->InternedStringView(
      protos::pbzero::InternedData::kSourcePathsFieldNumber, iid);
  if (!str) {
    context_->stats_tracker->IncrementStats(
        stats::stackprofile_invalid_string_id);
    return std::nullopt;
  }
  return *str;
}

}  // namespace trace_processor
}  // namespace perfetto
