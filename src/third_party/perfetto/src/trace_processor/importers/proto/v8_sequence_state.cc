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

#include "src/trace_processor/importers/proto/v8_sequence_state.h"
#include <optional>

#include "protos/perfetto/trace/chrome/v8.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/string_encoding_utils.h"
#include "src/trace_processor/importers/proto/v8_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/v8_tables_py.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::perfetto::protos::pbzero::InternedData;
using ::perfetto::protos::pbzero::InternedV8JsFunction;
using ::perfetto::protos::pbzero::InternedV8String;

protozero::ConstBytes ToConstBytes(const TraceBlobView& view) {
  return {view.data(), view.size()};
}

}  // namespace

V8SequenceState::V8SequenceState(TraceProcessorContext* context)
    : context_(context), v8_tracker_(V8Tracker::GetOrCreate(context_)) {}

V8SequenceState::~V8SequenceState() = default;

std::optional<IsolateId> V8SequenceState::GetOrInsertIsolate(uint64_t iid) {
  if (auto* id = isolates_.Find(iid); id != nullptr) {
    return *id;
  }

  auto* view = GetInternedMessageView(InternedData::kV8IsolateFieldNumber, iid);
  if (!view) {
    context_->storage->IncrementStats(stats::v8_intern_errors);
    return std::nullopt;
  }

  auto isolate_id = v8_tracker_->InternIsolate(ToConstBytes(view->message()));
  isolates_.Insert(iid, isolate_id);
  return isolate_id;
}

std::optional<tables::V8JsFunctionTable::Id>
V8SequenceState::GetOrInsertJsFunction(uint64_t iid, IsolateId isolate_id) {
  if (auto* id = js_functions_.Find(iid); id != nullptr) {
    return *id;
  }

  auto* view =
      GetInternedMessageView(InternedData::kV8JsFunctionFieldNumber, iid);
  if (!view) {
    context_->storage->IncrementStats(stats::v8_intern_errors);
    return std::nullopt;
  }

  InternedV8JsFunction::Decoder function(ToConstBytes(view->message()));

  std::optional<tables::V8JsScriptTable::Id> script_id =
      GetOrInsertJsScript(function.v8_js_script_iid(), isolate_id);
  if (!script_id) {
    return std::nullopt;
  }

  auto name = GetOrInsertJsFunctionName(function.v8_js_function_name_iid());
  if (!name) {
    return std::nullopt;
  }

  auto function_id = v8_tracker_->InternJsFunction(
      ToConstBytes(view->message()), *name, *script_id);

  js_functions_.Insert(iid, function_id);
  return function_id;
}

std::optional<tables::V8WasmScriptTable::Id>
V8SequenceState::GetOrInsertWasmScript(uint64_t iid, IsolateId isolate_id) {
  if (auto* id = wasm_scripts_.Find(iid); id != nullptr) {
    return *id;
  }
  auto* view =
      GetInternedMessageView(InternedData::kV8WasmScriptFieldNumber, iid);
  if (!view) {
    context_->storage->IncrementStats(stats::v8_intern_errors);
    return std::nullopt;
  }

  tables::V8WasmScriptTable::Id script_id =
      v8_tracker_->InternWasmScript(ToConstBytes(view->message()), isolate_id);
  wasm_scripts_.Insert(iid, script_id);
  return script_id;
}

std::optional<tables::V8JsScriptTable::Id> V8SequenceState::GetOrInsertJsScript(
    uint64_t iid,
    IsolateId v8_isolate_id) {
  if (auto* id = js_scripts_.Find(iid); id != nullptr) {
    return *id;
  }
  auto* view =
      GetInternedMessageView(InternedData::kV8JsScriptFieldNumber, iid);
  if (!view) {
    context_->storage->IncrementStats(stats::v8_intern_errors);
    return std::nullopt;
  }

  tables::V8JsScriptTable::Id script_id =
      v8_tracker_->InternJsScript(ToConstBytes(view->message()), v8_isolate_id);
  js_scripts_.Insert(iid, script_id);
  return script_id;
}

std::optional<StringId> V8SequenceState::GetOrInsertJsFunctionName(
    uint64_t iid) {
  if (auto* id = js_function_names_.Find(iid); id != nullptr) {
    return *id;
  }

  auto* view =
      GetInternedMessageView(InternedData::kV8JsFunctionNameFieldNumber, iid);

  if (!view) {
    context_->storage->IncrementStats(stats::v8_intern_errors);
    return std::nullopt;
  }

  InternedV8String::Decoder function_name(ToConstBytes(view->message()));
  auto& storage = *context_->storage;
  StringId id;
  if (function_name.has_latin1()) {
    id = storage.InternString(
        base::StringView(ConvertLatin1ToUtf8(function_name.latin1())));
  } else if (function_name.has_utf16_le()) {
    id = storage.InternString(
        base::StringView(ConvertUtf16LeToUtf8(function_name.utf16_le())));
  } else if (function_name.has_utf16_be()) {
    id = storage.InternString(
        base::StringView(ConvertUtf16BeToUtf8(function_name.utf16_be())));
  } else {
    id = storage.InternString("");
  }

  js_function_names_.Insert(iid, id);
  return id;
}

}  // namespace trace_processor
}  // namespace perfetto
