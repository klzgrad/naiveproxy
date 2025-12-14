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

#include "src/trace_processor/importers/proto/v8_module.h"

#include <cstdint>
#include <optional>

#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "protos/perfetto/trace/chrome/v8.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/v8_sequence_state.h"
#include "src/trace_processor/importers/proto/v8_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/v8_tables_py.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::perfetto::protos::pbzero::TracePacket;
using ::perfetto::protos::pbzero::V8CodeDefaults;
using ::perfetto::protos::pbzero::V8CodeMove;
using ::perfetto::protos::pbzero::V8InternalCode;
using ::perfetto::protos::pbzero::V8JsCode;
using ::perfetto::protos::pbzero::V8RegExpCode;
using ::perfetto::protos::pbzero::V8WasmCode;

}  // namespace

V8Module::V8Module(ProtoImporterModuleContext* module_context,
                   TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      v8_tracker_(std::make_unique<V8Tracker>(context)) {
  RegisterForField(TracePacket::kV8JsCodeFieldNumber);
  RegisterForField(TracePacket::kV8InternalCodeFieldNumber);
  RegisterForField(TracePacket::kV8WasmCodeFieldNumber);
  RegisterForField(TracePacket::kV8RegExpCodeFieldNumber);
  RegisterForField(TracePacket::kV8CodeMoveFieldNumber);
}

V8Module::~V8Module() = default;

ModuleResult V8Module::TokenizePacket(
    const TracePacket::Decoder&,
    TraceBlobView* /*packet*/,
    int64_t /*packet_timestamp*/,
    RefPtr<PacketSequenceStateGeneration> /*state*/,
    uint32_t /*field_id*/) {
  return ModuleResult::Ignored();
}

void V8Module::ParseTracePacketData(const TracePacket::Decoder& decoder,
                                    int64_t ts,
                                    const TracePacketData& data,
                                    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kV8JsCodeFieldNumber:
      ParseV8JsCode(decoder.v8_js_code(), ts, data);
      break;
    case TracePacket::kV8InternalCodeFieldNumber:
      ParseV8InternalCode(decoder.v8_internal_code(), ts, data);
      break;
    case TracePacket::kV8WasmCodeFieldNumber:
      ParseV8WasmCode(decoder.v8_wasm_code(), ts, data);
      break;
    case TracePacket::kV8RegExpCodeFieldNumber:
      ParseV8RegExpCode(decoder.v8_reg_exp_code(), ts, data);
      break;
    case TracePacket::kV8CodeMoveFieldNumber:
      ParseV8CodeMove(decoder.v8_code_move(), ts, data);
      break;
    default:
      break;
  }
}

template <typename CodeDecoder>
std::optional<UniqueTid> V8Module::GetUtid(
    PacketSequenceStateGeneration& generation,
    IsolateId isolate_id,
    const CodeDecoder& code) {
  auto* pid = isolate_to_pid_.Find(isolate_id);
  if (!pid) {
    tables::ProcessTable::Id upid(
        context_->storage->v8_isolate_table().FindById(isolate_id)->upid());
    pid = isolate_to_pid_
              .Insert(
                  isolate_id,
                  static_cast<uint32_t>(
                      context_->storage->process_table().FindById(upid)->pid()))
              .first;
  }

  if (code.has_tid()) {
    return context_->process_tracker->UpdateThread(code.tid(), *pid);
  }

  if (auto tid = GetDefaultTid(generation); tid.has_value()) {
    return context_->process_tracker->UpdateThread(*tid, *pid);
  }

  return std::nullopt;
}

std::optional<uint32_t> V8Module::GetDefaultTid(
    PacketSequenceStateGeneration& generation) const {
  auto* tp_defaults = generation.GetTracePacketDefaults();
  if (!tp_defaults) {
    context_->storage->IncrementStats(stats::v8_no_defaults);
    return std::nullopt;
  }
  if (!tp_defaults->has_v8_code_defaults()) {
    context_->storage->IncrementStats(stats::v8_no_defaults);
    return std::nullopt;
  }

  V8CodeDefaults::Decoder v8_defaults(tp_defaults->v8_code_defaults());

  if (!v8_defaults.has_tid()) {
    context_->storage->IncrementStats(stats::v8_no_defaults);
    return std::nullopt;
  }

  return v8_defaults.tid();
}

void V8Module::ParseV8JsCode(protozero::ConstBytes bytes,
                             int64_t ts,
                             const TracePacketData& data) {
  V8SequenceState& state =
      *data.sequence_state->GetCustomState<V8SequenceState>(v8_tracker_.get());

  V8JsCode::Decoder code(bytes);

  auto v8_isolate_id = state.GetOrInsertIsolate(code.v8_isolate_iid());
  if (!v8_isolate_id) {
    return;
  }

  std::optional<UniqueTid> utid =
      GetUtid(*data.sequence_state, *v8_isolate_id, code);
  if (!utid) {
    return;
  }

  auto v8_function_id =
      state.GetOrInsertJsFunction(code.v8_js_function_iid(), *v8_isolate_id);
  if (!v8_function_id) {
    return;
  }

  v8_tracker_->AddJsCode(ts, *utid, *v8_isolate_id, *v8_function_id, code);
}

void V8Module::ParseV8InternalCode(protozero::ConstBytes bytes,
                                   int64_t ts,
                                   const TracePacketData& data) {
  V8SequenceState& state =
      *data.sequence_state->GetCustomState<V8SequenceState>(v8_tracker_.get());

  V8InternalCode::Decoder code(bytes);

  auto v8_isolate_id = state.GetOrInsertIsolate(code.v8_isolate_iid());
  if (!v8_isolate_id) {
    return;
  }

  std::optional<UniqueTid> utid =
      GetUtid(*data.sequence_state, *v8_isolate_id, code);
  if (!utid) {
    return;
  }

  v8_tracker_->AddInternalCode(ts, *utid, *v8_isolate_id, code);
}

void V8Module::ParseV8WasmCode(protozero::ConstBytes bytes,
                               int64_t ts,
                               const TracePacketData& data) {
  V8SequenceState& state =
      *data.sequence_state->GetCustomState<V8SequenceState>(v8_tracker_.get());

  V8WasmCode::Decoder code(bytes);

  auto v8_isolate_id = state.GetOrInsertIsolate(code.v8_isolate_iid());
  if (!v8_isolate_id) {
    return;
  }

  auto v8_wasm_script_id =
      state.GetOrInsertWasmScript(code.v8_wasm_script_iid(), *v8_isolate_id);
  if (!v8_wasm_script_id) {
    return;
  }

  std::optional<UniqueTid> utid =
      GetUtid(*data.sequence_state, *v8_isolate_id, code);
  if (!utid) {
    return;
  }

  v8_tracker_->AddWasmCode(ts, *utid, *v8_isolate_id, *v8_wasm_script_id, code);
}

void V8Module::ParseV8RegExpCode(protozero::ConstBytes bytes,
                                 int64_t ts,
                                 const TracePacketData& data) {
  V8SequenceState& state =
      *data.sequence_state->GetCustomState<V8SequenceState>(v8_tracker_.get());

  V8RegExpCode::Decoder code(bytes);

  auto v8_isolate_id = state.GetOrInsertIsolate(code.v8_isolate_iid());
  if (!v8_isolate_id) {
    return;
  }

  std::optional<UniqueTid> utid =
      GetUtid(*data.sequence_state, *v8_isolate_id, code);
  if (!utid) {
    return;
  }

  v8_tracker_->AddRegExpCode(ts, *utid, *v8_isolate_id, code);
}

void V8Module::ParseV8CodeMove(protozero::ConstBytes bytes,
                               int64_t ts,
                               const TracePacketData& data) {
  V8SequenceState& state =
      *data.sequence_state->GetCustomState<V8SequenceState>(v8_tracker_.get());
  protos::pbzero::V8CodeMove::Decoder v8_code_move(bytes);

  std::optional<IsolateId> isolate_id =
      state.GetOrInsertIsolate(v8_code_move.isolate_iid());
  if (!isolate_id) {
    return;
  }

  std::optional<UniqueTid> utid =
      GetUtid(*data.sequence_state, *isolate_id, v8_code_move);
  if (!utid) {
    return;
  }

  v8_tracker_->MoveCode(ts, *utid, *isolate_id, v8_code_move);
}

}  // namespace trace_processor
}  // namespace perfetto
