/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/util/profile_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/trace_processor/demangle.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/jit_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/tables/v8_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/annotated_callsites.h"

#include "protos/third_party/pprof/profile.pbzero.h"

namespace perfetto::trace_processor {
namespace {

using protos::pbzero::Stack;
using third_party::perftools::profiles::pbzero::Profile;
using third_party::perftools::profiles::pbzero::Sample;

base::StringView ToString(CallsiteAnnotation annotation) {
  switch (annotation) {
    case CallsiteAnnotation::kNone:
      return "";
    case CallsiteAnnotation::kArtAot:
      return "aot";
    case CallsiteAnnotation::kArtInterpreted:
      return "interp";
    case CallsiteAnnotation::kArtJit:
      return "jit";
    case CallsiteAnnotation::kCommonFrame:
      return "common-frame";
    case CallsiteAnnotation::kCommonFrameInterp:
      return "common-frame-interp";
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

GProfileBuilder::StringTable::StringTable(
    protozero::HeapBuffered<third_party::perftools::profiles::pbzero::Profile>*
        result,
    const StringPool* string_pool)
    : string_pool_(*string_pool), result_(*result) {
  // String at index 0 of the string table must be the empty string (see
  // profile.proto)
  int64_t empty_index = WriteString("");
  PERFETTO_CHECK(empty_index == kEmptyStringIndex);
}

int64_t GProfileBuilder::StringTable::InternString(base::StringView str) {
  if (str.empty()) {
    return kEmptyStringIndex;
  }
  auto hash = str.Hash();
  auto it = seen_strings_.find(hash);
  if (it != seen_strings_.end()) {
    return it->second;
  }

  auto pool_id = string_pool_.GetId(str);
  int64_t index = pool_id ? InternString(*pool_id) : WriteString(str);

  seen_strings_.insert({hash, index});
  return index;
}

int64_t GProfileBuilder::StringTable::InternString(
    StringPool::Id string_pool_id) {
  auto it = seen_string_pool_ids_.find(string_pool_id);
  if (it != seen_string_pool_ids_.end()) {
    return it->second;
  }

  NullTermStringView str = string_pool_.Get(string_pool_id);

  int64_t index = str.empty() ? kEmptyStringIndex : WriteString(str);
  seen_string_pool_ids_.insert({string_pool_id, index});
  return index;
}

int64_t GProfileBuilder::StringTable::GetAnnotatedString(
    StringPool::Id str,
    CallsiteAnnotation annotation) {
  if (str.is_null() || annotation == CallsiteAnnotation::kNone) {
    return InternString(str);
  }
  return GetAnnotatedString(string_pool_.Get(str), annotation);
}

int64_t GProfileBuilder::StringTable::GetAnnotatedString(
    base::StringView str,
    CallsiteAnnotation annotation) {
  if (str.empty() || annotation == CallsiteAnnotation::kNone) {
    return InternString(str);
  }
  return InternString(base::StringView(
      str.ToStdString() + " [" + ToString(annotation).ToStdString() + "]"));
}

int64_t GProfileBuilder::StringTable::WriteString(base::StringView str) {
  result_->add_string_table(str.data(), str.size());
  return next_index_++;
}

GProfileBuilder::MappingKey::MappingKey(
    const tables::StackProfileMappingTable::ConstRowReference& mapping,
    StringTable& string_table) {
  size = static_cast<uint64_t>(mapping.end() - mapping.start());
  file_offset = static_cast<uint64_t>(mapping.exact_offset());
  build_id_or_filename = string_table.InternString(mapping.build_id());
  if (build_id_or_filename == kEmptyStringIndex) {
    build_id_or_filename = string_table.InternString(mapping.name());
  }
}

GProfileBuilder::Mapping::Mapping(
    const tables::StackProfileMappingTable::ConstRowReference& mapping,
    const StringPool& string_pool,
    StringTable& string_table)
    : memory_start(static_cast<uint64_t>(mapping.start())),
      memory_limit(static_cast<uint64_t>(mapping.end())),
      file_offset(static_cast<uint64_t>(mapping.exact_offset())),
      filename(string_table.InternString(mapping.name())),
      build_id(string_table.InternString(mapping.build_id())),
      filename_str(string_pool.Get(mapping.name()).ToStdString()) {}

// Do some very basic scoring.
int64_t GProfileBuilder::Mapping::ComputeMainBinaryScore() const {
  constexpr const char* kBadSuffixes[] = {".so"};
  constexpr const char* kBadPrefixes[] = {"/apex", "/system", "/[", "["};

  int64_t score = 0;
  if (build_id != kEmptyStringIndex) {
    score += 10;
  }

  if (filename != kEmptyStringIndex) {
    score += 10;
  }

  if (debug_info.has_functions) {
    score += 10;
  }
  if (debug_info.has_filenames) {
    score += 10;
  }
  if (debug_info.has_line_numbers) {
    score += 10;
  }
  if (debug_info.has_inline_frames) {
    score += 10;
  }

  if (memory_limit == memory_start) {
    score -= 1000;
  }

  for (const char* suffix : kBadSuffixes) {
    if (base::EndsWith(filename_str, suffix)) {
      score -= 1000;
      break;
    }
  }

  for (const char* prefix : kBadPrefixes) {
    if (base::StartsWith(filename_str, prefix)) {
      score -= 1000;
      break;
    }
  }

  return score;
}

bool GProfileBuilder::SampleAggregator::AddSample(
    const protozero::PackedVarInt& location_ids,
    const std::vector<int64_t>& values) {
  SerializedLocationId key(location_ids.data(),
                           location_ids.data() + location_ids.size());
  std::vector<int64_t>* agg_values = samples_.Find(key);
  if (!agg_values) {
    samples_.Insert(std::move(key), values);
    return true;
  }
  // All samples must have the same number of values.
  if (values.size() != agg_values->size()) {
    return false;
  }
  std::transform(values.begin(), values.end(), agg_values->begin(),
                 agg_values->begin(), std::plus<int64_t>());
  return true;
}

void GProfileBuilder::SampleAggregator::WriteTo(Profile& profile) {
  protozero::PackedVarInt values;
  for (auto it = samples_.GetIterator(); it; ++it) {
    values.Reset();
    for (int64_t value : it.value()) {
      values.Append(value);
    }
    Sample* sample = profile.add_sample();
    sample->set_value(values);
    // Map key is the serialized varint. Just append the bytes.
    sample->AppendBytes(Sample::kLocationIdFieldNumber, it.key().data(),
                        it.key().size());
  }
}

GProfileBuilder::GProfileBuilder(const TraceProcessorContext* context,
                                 const std::vector<ValueType>& sample_types)
    : context_(*context),
      string_table_(&result_, &context->storage->string_pool()),
      annotations_(context),
      jit_frame_cursor_(context->storage->jit_frame_table().CreateCursor({
          dataframe::FilterSpec{
              tables::JitFrameTable::ColumnIndex::frame_id,
              0,
              dataframe::Eq{},
              {},
          },
      })),
      v8_js_code_cursor_(context->storage->v8_js_code_table().CreateCursor({
          dataframe::FilterSpec{
              tables::V8JsCodeTable::ColumnIndex::jit_code_id,
              0,
              dataframe::Eq{},
              {},
          },
      })),
      v8_wasm_code_cursor_(context->storage->v8_wasm_code_table().CreateCursor({
          dataframe::FilterSpec{
              tables::V8WasmCodeTable::ColumnIndex::jit_code_id,
              0,
              dataframe::Eq{},
              {},
          },
      })),
      v8_regexp_code_cursor_(
          context->storage->v8_regexp_code_table().CreateCursor({
              dataframe::FilterSpec{
                  tables::V8RegexpCodeTable::ColumnIndex::jit_code_id,
                  0,
                  dataframe::Eq{},
                  {},
              },
          })),
      v8_internal_code_cursor_(
          context->storage->v8_internal_code_table().CreateCursor({
              dataframe::FilterSpec{
                  tables::V8InternalCodeTable::ColumnIndex::jit_code_id,
                  0,
                  dataframe::Eq{},
                  {},
              },
          })),
      jit_code_cursor_(context->storage->jit_code_table().CreateCursor({
          dataframe::FilterSpec{
              tables::JitCodeTable::ColumnIndex::function_name,
              0,
              dataframe::Eq{},
              {},
          },
      })),
      symbol_cursor_(context->storage->symbol_table().CreateCursor({
          dataframe::FilterSpec{
              tables::SymbolTable::ColumnIndex::symbol_set_id,
              0,
              dataframe::Eq{},
              {},
          },
      })) {
  // Make sure the empty function always gets id 0 which will be ignored
  // when writing the proto file.
  functions_.insert(
      {Function{kEmptyStringIndex, kEmptyStringIndex, kEmptyStringIndex},
       kNullFunctionId});
  WriteSampleTypes(sample_types);
}

GProfileBuilder::~GProfileBuilder() = default;

void GProfileBuilder::WriteSampleTypes(
    const std::vector<ValueType>& sample_types) {
  for (const auto& value_type : sample_types) {
    // Write strings first
    int64_t type =
        string_table_.InternString(base::StringView(value_type.type));
    int64_t unit =
        string_table_.InternString(base::StringView(value_type.unit));
    // Add message later, remember protozero does not allow you to
    // interleave these write calls.
    auto* sample_type = result_->add_sample_type();
    sample_type->set_type(type);
    sample_type->set_unit(unit);
  }
}

bool GProfileBuilder::AddSample(const Stack::Decoder& stack,
                                const std::vector<int64_t>& values) {
  PERFETTO_CHECK(!finalized_);

  auto it = stack.entries();
  if (!it) {
    return true;
  }

  auto next = it;
  ++next;
  if (!next) {
    Stack::Entry::Decoder entry(it->as_bytes());
    if (entry.has_callsite_id() || entry.has_annotated_callsite_id()) {
      bool annotated = entry.has_annotated_callsite_id();
      uint32_t callsite_id = entry.has_callsite_id()
                                 ? entry.callsite_id()
                                 : entry.annotated_callsite_id();
      return samples_.AddSample(
          GetLocationIdsForCallsite(CallsiteId(callsite_id), annotated),
          values);
    }
  }

  // Note pprof orders the stacks leafs first. That is also the ordering
  // StackBlob uses for entries
  protozero::PackedVarInt location_ids;
  for (; it; ++it) {
    Stack::Entry::Decoder entry(it->as_bytes());
    if (entry.has_name()) {
      location_ids.Append(
          WriteFakeLocationIfNeeded(entry.name().ToStdString()));
    } else if (entry.has_callsite_id() || entry.has_annotated_callsite_id()) {
      bool annotated = entry.has_annotated_callsite_id();
      uint32_t callsite_id = entry.has_callsite_id()
                                 ? entry.callsite_id()
                                 : entry.annotated_callsite_id();
      const protozero::PackedVarInt& ids =
          GetLocationIdsForCallsite(CallsiteId(callsite_id), annotated);
      for (const uint8_t* p = ids.data(); p < ids.data() + ids.size();) {
        uint64_t location_id;
        p = protozero::proto_utils::ParseVarInt(p, ids.data() + ids.size(),
                                                &location_id);
        location_ids.Append(location_id);
      }
    } else if (entry.has_frame_id()) {
      location_ids.Append(WriteLocationIfNeeded(FrameId(entry.frame_id()),
                                                CallsiteAnnotation::kNone));
    }
  }
  return samples_.AddSample(location_ids, values);
}

void GProfileBuilder::Finalize() {
  if (finalized_) {
    return;
  }
  WriteMappings();
  WriteFunctions();
  WriteLocations();
  samples_.WriteTo(*result_.get());
  finalized_ = true;
}

std::string GProfileBuilder::Build() {
  Finalize();
  return result_.SerializeAsString();
}

const protozero::PackedVarInt& GProfileBuilder::GetLocationIdsForCallsite(
    const CallsiteId& callsite_id,
    bool annotated) {
  auto it = cached_location_ids_.find({callsite_id, annotated});
  if (it != cached_location_ids_.end()) {
    return it->second;
  }

  protozero::PackedVarInt& location_ids =
      cached_location_ids_[{callsite_id, annotated}];

  const auto& cs_table = context_.storage->stack_profile_callsite_table();

  std::optional<tables::StackProfileCallsiteTable::ConstRowReference>
      start_ref = cs_table.FindById(callsite_id);
  if (!start_ref) {
    return location_ids;
  }

  location_ids.Append(WriteLocationIfNeeded(
      start_ref->frame_id(), annotated ? annotations_.GetAnnotation(*start_ref)
                                       : CallsiteAnnotation::kNone));

  std::optional<CallsiteId> parent_id = start_ref->parent_id();
  while (parent_id) {
    auto parent_ref = cs_table.FindById(*parent_id);
    location_ids.Append(WriteLocationIfNeeded(
        parent_ref->frame_id(), annotated
                                    ? annotations_.GetAnnotation(*parent_ref)
                                    : CallsiteAnnotation::kNone));
    parent_id = parent_ref->parent_id();
  }

  return location_ids;
}

uint64_t GProfileBuilder::WriteLocationIfNeeded(FrameId frame_id,
                                                CallsiteAnnotation annotation) {
  AnnotatedFrameId key{frame_id, annotation};
  auto it = seen_locations_.find(key);
  if (it != seen_locations_.end()) {
    return it->second;
  }

  const auto& frames = context_.storage->stack_profile_frame_table();
  auto frame = *frames.FindById(key.frame_id);

  const auto& mappings = context_.storage->stack_profile_mapping_table();
  auto mapping = *mappings.FindById(frame.mapping());
  uint64_t mapping_id = WriteMappingIfNeeded(mapping);

  uint64_t& id =
      locations_[Location{mapping_id, static_cast<uint64_t>(frame.rel_pc()),
                          GetLines(frame, key.annotation, mapping_id)}];

  if (id == 0) {
    id = locations_.size();
  }

  seen_locations_.insert({key, id});

  return id;
}

uint64_t GProfileBuilder::WriteFakeLocationIfNeeded(const std::string& name) {
  int64_t name_id = string_table_.InternString(base::StringView(name));
  auto it = seen_fake_locations_.find(name_id);
  if (it != seen_fake_locations_.end()) {
    return it->second;
  }

  uint64_t& id =
      locations_[Location{0, 0, {{WriteFakeFunctionIfNeeded(name_id), 0}}}];
  if (id == 0) {
    id = locations_.size();
  }
  seen_fake_locations_.insert({name_id, id});
  return id;
}

void GProfileBuilder::WriteLocations() {
  for (const auto& entry : locations_) {
    auto* location = result_->add_location();
    location->set_id(entry.second);
    location->set_mapping_id(entry.first.mapping_id);
    if (entry.first.mapping_id != 0) {
      location->set_address(entry.first.rel_pc +
                            GetMapping(entry.first.mapping_id).memory_start);
    }
    for (const Line& line : entry.first.lines) {
      auto* l = location->add_line();
      l->set_function_id(line.function_id);
      if (line.line != 0) {
        l->set_line(line.line);
      }
    }
  }
}

std::vector<GProfileBuilder::Line> GProfileBuilder::GetLines(
    const tables::StackProfileFrameTable::ConstRowReference& frame,
    CallsiteAnnotation annotation,
    uint64_t mapping_id) {
  std::vector<Line> lines;

  lines = GetLinesForJitFrame(frame, annotation, mapping_id);
  if (!lines.empty()) {
    return lines;
  }

  lines = GetLinesForSymbolSetId(frame.symbol_set_id(), annotation, mapping_id);
  if (!lines.empty()) {
    return lines;
  }

  if (uint64_t function_id =
          WriteFunctionIfNeeded(frame, annotation, mapping_id);
      function_id != kNullFunctionId) {
    lines.push_back({function_id, 0});
  }

  return lines;
}

std::vector<GProfileBuilder::Line> GProfileBuilder::GetLinesForJitFrame(
    const tables::StackProfileFrameTable::ConstRowReference& frame,
    CallsiteAnnotation annotation,
    uint64_t mapping_id) {
  // Execute the equivalent of the SQL logic in callstacks/stack_profile.sql,
  // namely
  //
  // ```
  // COALESCE(
  //     'JS: ' || IIF(jsf.name = "", "(anonymous)", jsf.name) || ':' ||
  //     jsf.line || ':' || jsf.col || ' [' || LOWER(jsc.tier) || ']', 'WASM: '
  //     || wc.function_name || ' [' || LOWER(wc.tier) || ']', 'REGEXP: ' ||
  //     rc.pattern, 'V8: ' || v8c.function_name, 'JIT: ' || jc.function_name
  //   ) AS name,
  // FROM _callstack_spc_raw_forest c
  // JOIN stack_profile_frame f ON c.frame_id = f.id
  // LEFT JOIN _v8_js_code jsc USING(jit_code_id)
  // LEFT JOIN v8_js_function jsf USING(v8_js_function_id)
  // LEFT JOIN _v8_internal_code v8c USING(jit_code_id)
  // LEFT JOIN _v8_wasm_code wc USING(jit_code_id)
  // LEFT JOIN _v8_regexp_code rc USING(jit_code_id)
  // LEFT JOIN __intrinsic_jit_code jc ON c.jit_code_id = jc.id
  // ```
  //
  // TODO(leszeks): Unify these manual table lookups with the SQL
  // implementation, e.g. by calling _callstacks_for_callsites in
  // GProfileBuilder.

  jit_frame_cursor_.SetFilterValueUnchecked(0, frame.id().value);
  jit_frame_cursor_.Execute();
  if (jit_frame_cursor_.Eof()) {
    return {};
  }

  v8_js_code_cursor_.SetFilterValueUnchecked(
      0, jit_frame_cursor_.jit_code_id().value);
  v8_js_code_cursor_.Execute();
  if (!v8_js_code_cursor_.Eof()) {
    const auto& v8_js_funcs = context_.storage->v8_js_function_table();
    auto maybe_jsf =
        v8_js_funcs.FindById(v8_js_code_cursor_.v8_js_function_id());
    if (maybe_jsf) {
      auto jsf = *maybe_jsf;
      auto jsf_name = context_.storage->string_pool().Get(jsf.name());
      auto jsf_tier =
          context_.storage->string_pool().Get(v8_js_code_cursor_.tier());
      base::StackString<64> name(
          "JS: %s:%u:%u [%s]",
          jsf_name.empty() ? "(anonymous)" : jsf_name.c_str(),
          jsf.line().value_or(0), jsf.col().value_or(0), jsf_tier.c_str());

      const auto& v8_js_scripts = context_.storage->v8_js_script_table();
      auto maybe_jss = v8_js_scripts.FindById(jsf.v8_js_script_id());

      return {
          Line{WriteFunctionIfNeeded(
                   name.string_view(),
                   maybe_jss.has_value() ? maybe_jss->name() : kNullStringId,
                   annotation, mapping_id),
               jsf.line().value_or(0)}};
    }
  }

  v8_wasm_code_cursor_.SetFilterValueUnchecked(
      0, jit_frame_cursor_.jit_code_id().value);
  v8_wasm_code_cursor_.Execute();
  if (!v8_wasm_code_cursor_.Eof()) {
    std::string name =
        "WASM: " +
        context_.storage->GetString(v8_wasm_code_cursor_.function_name())
            .ToStdString();
    return {Line{WriteFunctionIfNeeded(base::StringView(name), kNullStringId,
                                       annotation, mapping_id),
                 0}};
  }

  v8_regexp_code_cursor_.SetFilterValueUnchecked(
      0, jit_frame_cursor_.jit_code_id().value);
  v8_regexp_code_cursor_.Execute();
  if (!v8_regexp_code_cursor_.Eof()) {
    std::string name =
        "REGEXP: " +
        context_.storage->GetString(v8_regexp_code_cursor_.pattern())
            .ToStdString();
    return {Line{WriteFunctionIfNeeded(base::StringView(name), kNullStringId,
                                       annotation, mapping_id),
                 0}};
  }

  v8_internal_code_cursor_.SetFilterValueUnchecked(
      0, jit_frame_cursor_.jit_code_id().value);
  v8_internal_code_cursor_.Execute();
  if (!v8_internal_code_cursor_.Eof()) {
    std::string name =
        "V8: " +
        context_.storage->GetString(v8_internal_code_cursor_.function_name())
            .ToStdString();
    return {Line{WriteFunctionIfNeeded(base::StringView(name), kNullStringId,
                                       annotation, mapping_id),
                 0}};
  }

  jit_code_cursor_.SetFilterValueUnchecked(
      0, jit_frame_cursor_.jit_code_id().value);
  jit_code_cursor_.Execute();
  if (!jit_code_cursor_.Eof()) {
    std::string name =
        "JIT: " + context_.storage->GetString(jit_code_cursor_.function_name())
                      .ToStdString();
    return {Line{WriteFunctionIfNeeded(base::StringView(name), kNullStringId,
                                       annotation, mapping_id),
                 0}};
  }
  return {};
}

std::vector<GProfileBuilder::Line> GProfileBuilder::GetLinesForSymbolSetId(
    std::optional<uint32_t> symbol_set_id,
    CallsiteAnnotation annotation,
    uint64_t mapping_id) {
  if (!symbol_set_id) {
    return {};
  }

  using RowRef =
      perfetto::trace_processor::tables::SymbolTable::ConstRowReference;
  std::vector<RowRef> symbol_set;
  symbol_cursor_.SetFilterValueUnchecked(0, *symbol_set_id);
  symbol_cursor_.Execute();

  std::vector<GProfileBuilder::Line> lines;
  for (; !symbol_cursor_.Eof(); symbol_cursor_.Next()) {
    if (uint64_t function_id =
            WriteFunctionIfNeeded(symbol_cursor_, annotation, mapping_id);
        function_id != kNullFunctionId) {
      lines.push_back({function_id, symbol_cursor_.line_number().value_or(0)});
    }
  }

  GetMapping(mapping_id).debug_info.has_inline_frames = true;
  GetMapping(mapping_id).debug_info.has_line_numbers = true;

  return lines;
}

uint64_t GProfileBuilder::WriteFakeFunctionIfNeeded(int64_t name_id) {
  auto ins = functions_.insert(
      {Function{name_id, kEmptyStringIndex, kEmptyStringIndex},
       functions_.size() + 1});
  return ins.first->second;
}

uint64_t GProfileBuilder::WriteFunctionIfNeeded(base::StringView name,
                                                StringPool::Id filename,
                                                CallsiteAnnotation annotation,
                                                uint64_t mapping_id) {
  int64_t name_id = string_table_.GetAnnotatedString(name, annotation);
  int64_t filename_id = !filename.is_null()
                            ? string_table_.InternString(filename)
                            : kEmptyStringIndex;

  auto ins =
      functions_.insert({Function{name_id, kEmptyStringIndex, filename_id},
                         functions_.size() + 1});
  uint64_t id = ins.first->second;

  if (ins.second) {
    if (name_id != kEmptyStringIndex) {
      GetMapping(mapping_id).debug_info.has_functions = true;
    }
    if (filename_id != kEmptyStringIndex) {
      GetMapping(mapping_id).debug_info.has_filenames = true;
    }
  }

  return id;
}

uint64_t GProfileBuilder::WriteFunctionIfNeeded(
    const tables::SymbolTable::ConstCursor& symbol,
    CallsiteAnnotation annotation,
    uint64_t mapping_id) {
  int64_t name = string_table_.GetAnnotatedString(symbol.name(), annotation);
  int64_t filename = symbol.source_file().has_value()
                         ? string_table_.InternString(*symbol.source_file())
                         : kEmptyStringIndex;

  auto ins = functions_.insert(
      {Function{name, kEmptyStringIndex, filename}, functions_.size() + 1});
  uint64_t id = ins.first->second;

  if (ins.second) {
    if (name != kEmptyStringIndex) {
      GetMapping(mapping_id).debug_info.has_functions = true;
    }
    if (filename != kEmptyStringIndex) {
      GetMapping(mapping_id).debug_info.has_filenames = true;
    }
  }

  return id;
}

int64_t GProfileBuilder::GetNameForFrame(
    const tables::StackProfileFrameTable::ConstRowReference& frame,
    CallsiteAnnotation annotation) {
  NullTermStringView system_name = context_.storage->GetString(frame.name());
  int64_t name = kEmptyStringIndex;
  if (frame.deobfuscated_name()) {
    name = string_table_.GetAnnotatedString(*frame.deobfuscated_name(),
                                            annotation);
  } else if (!system_name.empty()) {
    std::unique_ptr<char, base::FreeDeleter> demangled =
        demangle::Demangle(system_name.c_str());
    if (demangled) {
      name = string_table_.GetAnnotatedString(demangled.get(), annotation);
    } else {
      // demangling failed, expected if the name wasn't mangled. In any case
      // reuse the system_name as this is what UI will usually display.
      name = string_table_.GetAnnotatedString(frame.name(), annotation);
    }
  }
  return name;
}

int64_t GProfileBuilder::GetSystemNameForFrame(
    const tables::StackProfileFrameTable::ConstRowReference& frame) {
  return string_table_.InternString(frame.name());
}

uint64_t GProfileBuilder::WriteFunctionIfNeeded(
    const tables::StackProfileFrameTable::ConstRowReference& frame,
    CallsiteAnnotation annotation,
    uint64_t mapping_id) {
  AnnotatedFrameId key{frame.id(), annotation};
  auto it = seen_functions_.find(key);
  if (it != seen_functions_.end()) {
    return it->second;
  }

  auto ins = functions_.insert(
      {Function{GetNameForFrame(frame, annotation),
                GetSystemNameForFrame(frame), kEmptyStringIndex},
       functions_.size() + 1});
  uint64_t id = ins.first->second;
  seen_functions_.insert({key, id});

  if (ins.second && (ins.first->first.name != kEmptyStringIndex ||
                     ins.first->first.system_name != kEmptyStringIndex)) {
    GetMapping(mapping_id).debug_info.has_functions = true;
  }

  return id;
}

void GProfileBuilder::WriteFunctions() {
  for (const auto& entry : functions_) {
    if (entry.second == kNullFunctionId) {
      continue;
    }
    auto* func = result_->add_function();
    func->set_id(entry.second);
    if (entry.first.name != 0) {
      func->set_name(entry.first.name);
    }
    if (entry.first.system_name != 0) {
      func->set_system_name(entry.first.system_name);
    }
    if (entry.first.filename != 0) {
      func->set_filename(entry.first.filename);
    }
  }
}

uint64_t GProfileBuilder::WriteMappingIfNeeded(
    const tables::StackProfileMappingTable::ConstRowReference& mapping_ref) {
  auto it = seen_mappings_.find(mapping_ref.id());
  if (it != seen_mappings_.end()) {
    return it->second;
  }

  auto ins = mapping_keys_.insert(
      {MappingKey(mapping_ref, string_table_), mapping_keys_.size() + 1});

  if (ins.second) {
    mappings_.emplace_back(mapping_ref, context_.storage->string_pool(),
                           string_table_);
  }

  return ins.first->second;
}

void GProfileBuilder::WriteMapping(uint64_t mapping_id) {
  const Mapping& mapping = GetMapping(mapping_id);
  auto* m = result_->add_mapping();
  m->set_id(mapping_id);
  m->set_memory_start(mapping.memory_start);
  m->set_memory_limit(mapping.memory_limit);
  m->set_file_offset(mapping.file_offset);
  m->set_filename(mapping.filename);
  m->set_build_id(mapping.build_id);
  m->set_has_functions(mapping.debug_info.has_functions);
  m->set_has_filenames(mapping.debug_info.has_filenames);
  m->set_has_line_numbers(mapping.debug_info.has_line_numbers);
  m->set_has_inline_frames(mapping.debug_info.has_inline_frames);
}

void GProfileBuilder::WriteMappings() {
  // The convention in pprof files is to write the mapping for the main
  // binary first. So lets do just that.
  std::optional<uint64_t> main_mapping_id = GuessMainBinary();
  if (main_mapping_id) {
    WriteMapping(*main_mapping_id);
  }

  for (size_t i = 0; i < mappings_.size(); ++i) {
    uint64_t mapping_id = i + 1;
    if (main_mapping_id && *main_mapping_id == mapping_id) {
      continue;
    }
    WriteMapping(mapping_id);
  }
}

std::optional<uint64_t> GProfileBuilder::GuessMainBinary() const {
  std::vector<int64_t> mapping_scores;
  mapping_scores.reserve(mappings_.size());

  for (const auto& mapping : mappings_) {
    mapping_scores.push_back(mapping.ComputeMainBinaryScore());
  }

  auto it = std::max_element(mapping_scores.begin(), mapping_scores.end());

  if (it == mapping_scores.end()) {
    return std::nullopt;
  }

  return static_cast<uint64_t>(std::distance(mapping_scores.begin(), it) + 1);
}

}  // namespace perfetto::trace_processor
