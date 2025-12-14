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

#include "src/trace_processor/importers/proto/v8_tracker.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "protos/perfetto/trace/chrome/v8.pbzero.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/jit_cache.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/proto/jit_tracker.h"
#include "src/trace_processor/importers/proto/string_encoding_utils.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/jit_tables_py.h"
#include "src/trace_processor/tables/v8_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::perfetto::protos::pbzero::InternedV8Isolate;
using ::perfetto::protos::pbzero::InternedV8JsFunction;
using ::perfetto::protos::pbzero::InternedV8JsScript;
using ::perfetto::protos::pbzero::InternedV8WasmScript;
using ::perfetto::protos::pbzero::V8CodeMove;
using ::perfetto::protos::pbzero::V8InternalCode;
using ::perfetto::protos::pbzero::V8JsCode;
using ::perfetto::protos::pbzero::V8RegExpCode;
using ::perfetto::protos::pbzero::V8String;
using ::perfetto::protos::pbzero::V8WasmCode;

bool IsInterpretedCode(const V8JsCode::Decoder& code) {
  switch (code.tier()) {
    case V8JsCode::TIER_IGNITION:
      return true;

    case V8JsCode::TIER_UNKNOWN:
    case V8JsCode::TIER_SPARKPLUG:
    case V8JsCode::TIER_MAGLEV:
    case V8JsCode::TIER_TURBOSHAFT:
    case V8JsCode::TIER_TURBOFAN:
      return false;
  }
  PERFETTO_FATAL("Unreachable");
}

bool IsNativeCode(const V8JsCode::Decoder& code) {
  switch (code.tier()) {
    case V8JsCode::TIER_UNKNOWN:
    case V8JsCode::TIER_IGNITION:
      return false;

    case V8JsCode::TIER_SPARKPLUG:
    case V8JsCode::TIER_MAGLEV:
    case V8JsCode::TIER_TURBOSHAFT:
    case V8JsCode::TIER_TURBOFAN:
      return true;
  }
  PERFETTO_FATAL("Unreachable");
}

base::StringView JsScriptTypeToString(int32_t type) {
  if (type < protos::pbzero::InternedV8JsScript_Type_MIN ||
      type > protos::pbzero::InternedV8JsScript_Type_MAX) {
    return "UNKNOWN";
  }
  base::StringView name =
      InternedV8JsScript::Type_Name(InternedV8JsScript::Type(type));
  // Remove the "TYPE_" prefix
  return name.substr(5);
}

base::StringView JsFunctionKindToString(int32_t kind) {
  if (kind < protos::pbzero::InternedV8JsFunction_Kind_MIN ||
      kind > protos::pbzero::InternedV8JsFunction_Kind_MAX) {
    return "UNKNOWN";
  }
  base::StringView name =
      InternedV8JsFunction::Kind_Name(InternedV8JsFunction::Kind(kind));
  // Remove the "KIND_" prefix
  return name.substr(5);
}

base::StringView JsCodeTierToString(int32_t tier) {
  if (tier < protos::pbzero::V8JsCode_Tier_MIN ||
      tier > protos::pbzero::V8JsCode_Tier_MAX) {
    return "UNKNOWN";
  }
  base::StringView name = V8JsCode::Tier_Name(V8JsCode::Tier(tier));
  // Remove the "TIER_" prefix
  return name.substr(5);
}

base::StringView InternalCodeTypeToString(int32_t type) {
  if (type < protos::pbzero::V8InternalCode_Type_MIN ||
      type > protos::pbzero::V8InternalCode_Type_MAX) {
    return "UNKNOWN";
  }
  base::StringView name = V8InternalCode::Type_Name(V8InternalCode::Type(type));
  // Remove the "TYPE_" prefix
  return name.substr(5);
}

base::StringView WasmCodeTierToString(int32_t tier) {
  if (tier < protos::pbzero::V8WasmCode_Tier_MIN ||
      tier > protos::pbzero::V8WasmCode_Tier_MAX) {
    return "UNKNOWN";
  }
  base::StringView name = V8WasmCode::Tier_Name(V8WasmCode::Tier(tier));
  // Remove the "TIER_" prefix
  return name.substr(5);
}

}  // namespace

V8Tracker::V8Tracker(TraceProcessorContext* context)
    : context_(context), jit_tracker_(context) {}

V8Tracker::~V8Tracker() = default;

std::optional<IsolateId> V8Tracker::InternIsolate(protozero::ConstBytes bytes) {
  InternedV8Isolate::Decoder isolate(bytes);

  const IsolateKey isolate_key{
      context_->process_tracker->GetOrCreateProcess(isolate.pid()),
      isolate.isolate_id()};

  if (auto* id = isolate_index_.Find(isolate_key); id) {
    return *id;
  }

  // TODO(b/347250452): Implement support for no code range
  if (!isolate.has_code_range()) {
    context_->storage->IncrementStats(stats::v8_isolate_has_no_code_range);
    isolate_index_.Insert(isolate_key, std::nullopt);
    return std::nullopt;
  }

  return *isolate_index_.Insert(isolate_key, CreateIsolate(isolate)).first;
}

UserMemoryMapping* V8Tracker::FindEmbeddedBlobMapping(
    UniquePid upid,
    AddressRange embedded_blob_code) const {
  UserMemoryMapping* m = context_->mapping_tracker->FindUserMappingForAddress(
      upid, embedded_blob_code.start());
  if (!m) {
    return nullptr;
  }

  if (m->memory_range().start() == embedded_blob_code.start() &&
      embedded_blob_code.end() <= m->memory_range().end()) {
    return m;
  }

  return nullptr;
}

std::pair<V8Tracker::IsolateCodeRanges, bool> V8Tracker::GetIsolateCodeRanges(
    UniquePid upid,
    const protos::pbzero::InternedV8Isolate::Decoder& isolate) {
  PERFETTO_CHECK(isolate.has_code_range());

  IsolateCodeRanges res;

  InternedV8Isolate::CodeRange::Decoder code_range_proto(isolate.code_range());
  AddressRange code_range = AddressRange::FromStartAndSize(
      code_range_proto.base_address(), code_range_proto.size());

  res.heap_code.Add(code_range);
  if (isolate.has_embedded_blob_code_start_address() &&
      isolate.embedded_blob_code_size() != 0) {
    res.embedded_blob = AddressRange::FromStartAndSize(
        isolate.embedded_blob_code_start_address(),
        isolate.embedded_blob_code_size());

    if (UserMemoryMapping* m =
            FindEmbeddedBlobMapping(upid, *res.embedded_blob);
        m) {
      res.embedded_blob = m->memory_range();
    }

    res.heap_code.Remove(*res.embedded_blob);
  }

  return {std::move(res), code_range_proto.is_process_wide()};
}

AddressRangeMap<JitCache*> V8Tracker::CreateJitCaches(
    UniquePid upid,
    const IsolateCodeRanges& code_ranges) {
  AddressRangeMap<JitCache*> jit_caches;
  for (const AddressRange& r : code_ranges.heap_code) {
    jit_caches.Emplace(r, jit_tracker_.CreateJitCache("v8 code", upid, r));
  }
  if (code_ranges.embedded_blob) {
    jit_caches.Emplace(*code_ranges.embedded_blob,
                       jit_tracker_.CreateJitCache("v8 blob", upid,
                                                   *code_ranges.embedded_blob));
  }

  return jit_caches;
}

AddressRangeMap<JitCache*> V8Tracker::GetOrCreateSharedJitCaches(
    UniquePid upid,
    const IsolateCodeRanges& code_ranges) {
  if (auto* shared = shared_code_ranges_.Find(upid); shared) {
    PERFETTO_CHECK(shared->code_ranges == code_ranges);
    return shared->jit_caches;
  }

  return shared_code_ranges_
      .Insert(upid, {code_ranges, CreateJitCaches(upid, code_ranges)})
      .first->jit_caches;
}

IsolateId V8Tracker::CreateIsolate(
    const InternedV8Isolate::Decoder& isolate_proto) {
  auto v8_isolate = InsertIsolate(isolate_proto);
  const UniquePid upid = v8_isolate.upid();

  auto [code_ranges, is_process_wide] =
      GetIsolateCodeRanges(upid, isolate_proto);

  PERFETTO_CHECK(isolates_
                     .Insert(v8_isolate.id(),
                             is_process_wide
                                 ? GetOrCreateSharedJitCaches(upid, code_ranges)
                                 : CreateJitCaches(upid, code_ranges))
                     .second);

  return v8_isolate.id();
}

tables::V8IsolateTable::ConstRowReference V8Tracker::InsertIsolate(
    const InternedV8Isolate::Decoder& isolate) {
  InternedV8Isolate::CodeRange::Decoder code_range(isolate.code_range());
  return context_->storage->mutable_v8_isolate_table()
      ->Insert(
          {context_->process_tracker->GetOrCreateProcess(isolate.pid()),
           isolate.isolate_id(),
           static_cast<int64_t>(isolate.embedded_blob_code_start_address()),
           static_cast<int64_t>(isolate.embedded_blob_code_size()),
           static_cast<int64_t>(code_range.base_address()),
           static_cast<int64_t>(code_range.size()),
           code_range.is_process_wide(),
           code_range.has_embedded_blob_code_copy_start_address()
               ? std::make_optional(static_cast<int64_t>(
                     code_range.embedded_blob_code_copy_start_address()))
               : std::nullopt})
      .row_reference;
}

tables::V8JsScriptTable::Id V8Tracker::InternJsScript(
    protozero::ConstBytes bytes,
    IsolateId isolate_id) {
  InternedV8JsScript::Decoder script(bytes);

  if (auto* id =
          js_script_index_.Find(std::make_pair(isolate_id, script.script_id()));
      id) {
    return *id;
  }

  tables::V8JsScriptTable::Row row;
  row.v8_isolate_id = isolate_id;
  row.internal_script_id = script.script_id();
  row.script_type =
      context_->storage->InternString(JsScriptTypeToString(script.type()));
  row.name = InternV8String(V8String::Decoder(script.name()));
  row.source = InternV8String(V8String::Decoder(script.source()));

  tables::V8JsScriptTable::Id script_id =
      context_->storage->mutable_v8_js_script_table()->Insert(row).id;
  js_script_index_.Insert(std::make_pair(isolate_id, script.script_id()),
                          script_id);
  return script_id;
}

tables::V8WasmScriptTable::Id V8Tracker::InternWasmScript(
    protozero::ConstBytes bytes,
    IsolateId isolate_id) {
  InternedV8WasmScript::Decoder script(bytes);

  if (auto* id = wasm_script_index_.Find(
          std::make_pair(isolate_id, script.script_id()));
      id) {
    return *id;
  }

  tables::V8WasmScriptTable::Row row;
  row.v8_isolate_id = isolate_id;
  row.internal_script_id = script.script_id();
  row.url = context_->storage->InternString(script.url());
  row.wire_bytes_base64 = context_->storage->InternString(base::StringView(
      base::Base64Encode(script.wire_bytes().data, script.wire_bytes().size)));

  tables::V8WasmScriptTable::Id script_id =
      context_->storage->mutable_v8_wasm_script_table()->Insert(row).id;
  wasm_script_index_.Insert(std::make_pair(isolate_id, script.script_id()),
                            script_id);
  return script_id;
}

tables::V8JsFunctionTable::Id V8Tracker::InternJsFunction(
    protozero::ConstBytes bytes,
    StringId name,
    tables::V8JsScriptTable::Id script_id) {
  InternedV8JsFunction::Decoder function(bytes);

  tables::V8JsFunctionTable::Row row;
  row.name = name;
  row.v8_js_script_id = script_id;
  row.is_toplevel = function.is_toplevel();
  row.kind =
      context_->storage->InternString(JsFunctionKindToString(function.kind()));
  if (function.has_line() && function.has_column()) {
    row.line = function.line();
    row.col = function.column();
  } else if (function.has_byte_offset()) {
    // TODO(carlscab): Line and column are hard. Offset is in bytes, line and
    // column are in characters and we potentially have a multi byte encoding
    // (UTF16). Good luck!
    row.line = 1;
    row.col = function.byte_offset();
  }

  if (auto* id = js_function_index_.Find(row); id) {
    return *id;
  }

  tables::V8JsFunctionTable::Id function_id =
      context_->storage->mutable_v8_js_function_table()->Insert(row).id;
  js_function_index_.Insert(row, function_id);
  return function_id;
}

JitCache* V8Tracker::MaybeFindJitCache(IsolateId isolate_id,
                                       AddressRange code_range) const {
  if (code_range.empty()) {
    context_->storage->IncrementStats(stats::v8_code_load_missing_code_range);
    return nullptr;
  }
  auto* isolate = isolates_.Find(isolate_id);
  PERFETTO_CHECK(isolate);
  if (auto it = isolate->FindRangeThatContains(code_range);
      it != isolate->end()) {
    return it->second;
  }

  return nullptr;
}

JitCache* V8Tracker::FindJitCache(IsolateId isolate_id,
                                  AddressRange code_range) const {
  if (code_range.empty()) {
    context_->storage->IncrementStats(stats::v8_code_load_missing_code_range);
    return nullptr;
  }
  JitCache* cache = MaybeFindJitCache(isolate_id, code_range);
  if (!cache) {
    context_->storage->IncrementStats(stats::v8_no_code_range);
  }
  return cache;
}

void V8Tracker::AddJsCode(int64_t timestamp,
                          UniqueTid utid,
                          IsolateId isolate_id,
                          tables::V8JsFunctionTable::Id function_id,
                          const V8JsCode::Decoder& code) {
  const StringId tier =
      context_->storage->InternString(JsCodeTierToString(code.tier()));

  const AddressRange code_range = AddressRange::FromStartAndSize(
      code.instruction_start(), code.instruction_size_bytes());

  JitCache* jit_cache = nullptr;

  if (IsInterpretedCode(code)) {
    // If --interpreted_frames_native_stack is specified interpreted frames will
    // also be emitted as native functions.
    // TODO(carlscab): Add an additional tier to for NATIVE_IGNITION_FRAME. Int
    // he meantime we can infer that this is the case if we have a hit in the
    // jit cache. NOte we call MaybeFindJitCache to not log an error if there is
    // no hit.
    jit_cache = MaybeFindJitCache(isolate_id, code_range);
    if (!jit_cache) {
      context_->storage->mutable_v8_js_code_table()->Insert(
          {std::nullopt, function_id, tier,
           context_->storage->InternString(base::StringView(base::Base64Encode(
               code.bytecode().data, code.bytecode().size)))});
      return;
    }
  } else if (IsNativeCode(code)) {
    jit_cache = FindJitCache(isolate_id, code_range);
  } else {
    context_->storage->IncrementStats(stats::v8_unknown_code_type);
    return;
  }

  if (!jit_cache) {
    return;
  }

  auto function =
      *context_->storage->v8_js_function_table().FindById(function_id);
  auto script = *context_->storage->v8_js_script_table().FindById(
      function.v8_js_script_id());
  const auto jit_code_id = jit_cache->LoadCode(
      timestamp, utid, code_range, function.name(),
      JitCache::SourceLocation{script.name(), function.line().value_or(0)},
      code.has_machine_code()
          ? TraceBlobView(TraceBlob::CopyFrom(code.machine_code().data,
                                              code.machine_code().size))
          : TraceBlobView());

  context_->storage->mutable_v8_js_code_table()->Insert(
      {jit_code_id, function_id, tier});
}

void V8Tracker::AddInternalCode(int64_t timestamp,
                                UniqueTid utid,
                                IsolateId isolate_id,
                                const V8InternalCode::Decoder& code) {
  const AddressRange code_range = AddressRange::FromStartAndSize(
      code.instruction_start(), code.instruction_size_bytes());
  JitCache* const jit_cache = FindJitCache(isolate_id, code_range);
  if (!jit_cache) {
    return;
  }

  const StringId function_name = context_->storage->InternString(code.name());
  const StringId type =
      context_->storage->InternString(InternalCodeTypeToString(code.type()));
  const auto jit_code_id = jit_cache->LoadCode(
      timestamp, utid, code_range, function_name, std::nullopt,
      code.has_machine_code()
          ? TraceBlobView(TraceBlob::CopyFrom(code.machine_code().data,
                                              code.machine_code().size))
          : TraceBlobView());

  context_->storage->mutable_v8_internal_code_table()->Insert(
      {jit_code_id, isolate_id, function_name, type});
}

void V8Tracker::AddWasmCode(int64_t timestamp,
                            UniqueTid utid,
                            IsolateId isolate_id,
                            tables::V8WasmScriptTable::Id script_id,
                            const V8WasmCode::Decoder& code) {
  const AddressRange code_range = AddressRange::FromStartAndSize(
      code.instruction_start(), code.instruction_size_bytes());
  JitCache* const jit_cache = FindJitCache(isolate_id, code_range);
  if (!jit_cache) {
    return;
  }

  const StringId function_name =
      context_->storage->InternString(code.function_name());
  const StringId tier =
      context_->storage->InternString(WasmCodeTierToString(code.tier()));

  const auto jit_code_id = jit_cache->LoadCode(
      timestamp, utid, code_range, function_name, std::nullopt,
      code.has_machine_code()
          ? TraceBlobView(TraceBlob::CopyFrom(code.machine_code().data,
                                              code.machine_code().size))
          : TraceBlobView());

  context_->storage->mutable_v8_wasm_code_table()->Insert(
      {jit_code_id, isolate_id, script_id, function_name, tier});
}

void V8Tracker::AddRegExpCode(int64_t timestamp,
                              UniqueTid utid,
                              IsolateId isolate_id,
                              const V8RegExpCode::Decoder& code) {
  const AddressRange code_range = AddressRange::FromStartAndSize(
      code.instruction_start(), code.instruction_size_bytes());
  JitCache* const jit_cache = FindJitCache(isolate_id, code_range);
  if (!jit_cache) {
    return;
  }

  const StringId function_name = context_->storage->InternString("[RegExp]");
  const StringId pattern = InternV8String(V8String::Decoder(code.pattern()));
  const auto jit_code_id = jit_cache->LoadCode(
      timestamp, utid, code_range, function_name, std::nullopt,
      code.has_machine_code()
          ? TraceBlobView(TraceBlob::CopyFrom(code.machine_code().data,
                                              code.machine_code().size))
          : TraceBlobView());

  context_->storage->mutable_v8_regexp_code_table()->Insert(
      {jit_code_id, isolate_id, pattern});
}

void V8Tracker::MoveCode(int64_t timestamp,
                         UniqueTid utid,
                         IsolateId isolate_id,
                         const V8CodeMove::Decoder& code) {
  if (!code.has_from_instruction_start_address())
    return;

  const AddressRange code_range = AddressRange::FromStartAndSize(
      code.from_instruction_start_address(), code.instruction_size_bytes());
  JitCache* const jit_cache = FindJitCache(isolate_id, code_range);
  if (!jit_cache) {
    return;
  }

  jit_cache->MoveCode(timestamp, utid, code.from_instruction_start_address(),
                      code.to_instruction_start_address());
}

StringId V8Tracker::InternV8String(const V8String::Decoder& v8_string) {
  auto& storage = *context_->storage;
  if (v8_string.has_latin1()) {
    return storage.InternString(
        base::StringView(ConvertLatin1ToUtf8(v8_string.latin1())));
  }

  if (v8_string.has_utf16_le()) {
    return storage.InternString(
        base::StringView(ConvertUtf16LeToUtf8(v8_string.utf16_le())));
  }

  if (v8_string.has_utf16_be()) {
    return storage.InternString(
        base::StringView(ConvertUtf16BeToUtf8(v8_string.utf16_be())));
  }
  return storage.InternString("");
}

}  // namespace trace_processor
}  // namespace perfetto
