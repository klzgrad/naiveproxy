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

#include "src/trace_processor/plugins/perfetto_manifest/perfetto_manifest_reader.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/global_metadata_tracker.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_manifest_state.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/clock_synchronizer.h"
#include "src/trace_processor/util/json_value.h"
#include "src/trace_processor/util/trace_type.h"

#include "perfetto/ext/base/string_utils.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor::perfetto_manifest {
namespace {

using FileEntry = TraceManifestState::FileEntry;
using ClockOverride = TraceManifestState::ClockOverride;

base::StatusOr<uint32_t> ParseClockName(const json::Dom& value) {
  if (!value.IsString()) {
    return base::ErrStatus("perfetto_manifest: clock name must be a string");
  }
  using protos::pbzero::BuiltinClock;
  std::string name = value.AsString();
  if (name == "REALTIME")
    return BuiltinClock::BUILTIN_CLOCK_REALTIME;
  if (name == "REALTIME_COARSE")
    return BuiltinClock::BUILTIN_CLOCK_REALTIME_COARSE;
  if (name == "MONOTONIC")
    return BuiltinClock::BUILTIN_CLOCK_MONOTONIC;
  if (name == "MONOTONIC_COARSE")
    return BuiltinClock::BUILTIN_CLOCK_MONOTONIC_COARSE;
  if (name == "MONOTONIC_RAW")
    return BuiltinClock::BUILTIN_CLOCK_MONOTONIC_RAW;
  if (name == "BOOTTIME")
    return BuiltinClock::BUILTIN_CLOCK_BOOTTIME;
  return base::ErrStatus(
      "perfetto_manifest: unknown clock name: %s. Use one of REALTIME, "
      "REALTIME_COARSE, MONOTONIC, MONOTONIC_COARSE, MONOTONIC_RAW, BOOTTIME.",
      name.c_str());
}

// Parses a file's "clocks" block ("manual" mode): relate one of this file's
// clocks to a clock in another trace (`sync_to`) at a fixed `offset_ns`.
// Normalized into ClockOverride (see its doc for the field semantics).
//   {sync_to:{file:"f"}}                        relate to f's sole clock
//   {sync_to:{file:"f", clock:"X"}}             relate to f's clock X
//   {clock:"X", sync_to:{file:"f", clock:"Y"}}  relate this file's X to f's Y
//   {machine:"m", clock:"X", sync_to:{...}}     ... where X is on this file's
//                                               declared machine m
//   {..., offset_ns:N}                          at offset N
// A top-level `clock` names which of this file's clocks to relate (an existing
// clock of an internally-clocked trace); without it the file's own private
// clock is pinned. `machine` names which machine declared by this file owns
// that clock. sync_to.file/machine/clock pick the reference clock.
base::StatusOr<ClockOverride> ParseClocks(const json::Dom& clocks) {
  ClockOverride result;

  if (clocks.HasMember("machine")) {
    if (!clocks["machine"].IsString()) {
      return base::ErrStatus(
          "perfetto_manifest: clocks: machine must be a string");
    }
    result.source_machine = clocks["machine"].AsString();
  }
  if (clocks.HasMember("clock")) {
    ASSIGN_OR_RETURN(uint32_t source_clock, ParseClockName(clocks["clock"]));
    result.source_clock = source_clock;
  }

  if (!clocks.HasMember("sync_to")) {
    return base::ErrStatus(
        "perfetto_manifest: clocks: a sync_to block is required. Manual clock "
        R"(handling relates this file to a clock in another trace, e.g. )"
        R"("sync_to": {"file": "other.pb"}.)");
  }
  const json::Dom& sync_to = clocks["sync_to"];
  if (!sync_to.IsObject()) {
    return base::ErrStatus(
        "perfetto_manifest: clocks: sync_to must be an object");
  }
  // sync_to.file and sync_to.machine are complementary, not exclusive: naming a
  // machine inside a multi-machine file needs both (see resolve_ref below).
  if (!sync_to.HasMember("file") || !sync_to["file"].IsString()) {
    return base::ErrStatus(
        "perfetto_manifest: clocks: sync_to.file is required and must be a "
        "string");
  }
  result.ref_file = sync_to["file"].AsString();
  if (sync_to.HasMember("machine")) {
    if (!sync_to["machine"].IsString()) {
      return base::ErrStatus(
          "perfetto_manifest: clocks: sync_to.machine must be a string");
    }
    result.ref_machine = sync_to["machine"].AsString();
  }
  if (sync_to.HasMember("clock")) {
    ASSIGN_OR_RETURN(uint32_t ref_clock, ParseClockName(sync_to["clock"]));
    result.ref_clock = ref_clock;
  }

  if (clocks.HasMember("offset_ns")) {
    if (!clocks["offset_ns"].IsIntegral()) {
      return base::ErrStatus(
          "perfetto_manifest: clocks: offset_ns must be an integer");
    }
    int64_t offset_ns = clocks["offset_ns"].AsInt64();
    if (offset_ns == std::numeric_limits<int64_t>::min()) {
      return base::ErrStatus(
          "perfetto_manifest: clocks: offset_ns is out of range");
    }
    result.offset_ns = offset_ns;
  }
  return result;
}

// Parses a file's internal `__exported_table_schema` block. Only the JSON
// shape is checked here; the trace_export plugin owns its version and schema
// semantics.
base::StatusOr<TraceManifestState::PerfettoExportTable>
ParsePerfettoExportTable(const json::Dom& table) {
  if (!table.IsObject() || !table["format"].IsIntegral() ||
      !table["name"].IsString() || !table["row_count"].IsIntegral() ||
      !table["columns"].IsArray()) {
    return base::ErrStatus(
        "perfetto_manifest: __exported_table_schema needs an integer format, "
        "a string name, an integer row_count and a columns array");
  }
  TraceManifestState::PerfettoExportTable result;
  result.format = table["format"].AsInt64();
  result.name = table["name"].AsString();
  int64_t row_count = table["row_count"].AsInt64();
  if (row_count < 0 || row_count > std::numeric_limits<uint32_t>::max()) {
    return base::ErrStatus("perfetto_manifest: table '%s': invalid row_count",
                           result.name.c_str());
  }
  result.row_count = static_cast<uint32_t>(row_count);
  for (const json::Dom& col : table["columns"]) {
    if (!col.IsObject() || !col["name"].IsString() || !col["type"].IsString() ||
        !col["nullability"].IsString() || !col["sort"].IsString() ||
        !col["duplicates"].IsString()) {
      return base::ErrStatus(
          "perfetto_manifest: table '%s': malformed column entry",
          result.name.c_str());
    }
    result.columns.push_back({col["name"].AsString(), col["type"].AsString(),
                              col["nullability"].AsString(),
                              col["sort"].AsString(),
                              col["duplicates"].AsString()});
  }
  return std::move(result);
}

using Attribute = std::pair<std::string, std::variant<int64_t, std::string>>;

base::StatusOr<std::vector<Attribute>> ParseAttributes(
    const json::Dom& attributes) {
  if (!attributes.IsObject()) {
    return base::ErrStatus(
        "perfetto_manifest: attributes must be an object of string or "
        R"(integer values, e.g. "attributes": {"benchmark": "startup"}.)");
  }
  std::vector<Attribute> result;
  for (const std::string& key : attributes.GetMemberNames()) {
    if (key.empty()) {
      return base::ErrStatus(
          "perfetto_manifest: attributes: keys must be non-empty");
    }
    const json::Dom& value = attributes[key];
    if (value.IsString()) {
      result.emplace_back(key, value.AsString());
    } else if (value.IsIntegral()) {
      result.emplace_back(key, value.AsInt64());
    } else {
      return base::ErrStatus(
          "perfetto_manifest: attributes: '%s' must be a string or an "
          "integer",
          key.c_str());
    }
  }
  return result;
}

void ApplyAttributes(TraceProcessorContext* context,
                     const std::vector<Attribute>& attrs) {
  for (const auto& [key, value] : attrs) {
    StringId key_id = context->storage->InternString(
        base::StringView("manifest_attribute." + key));
    Variadic variadic =
        std::holds_alternative<int64_t>(value)
            ? Variadic::Integer(std::get<int64_t>(value))
            : Variadic::String(context->storage->InternString(
                  base::StringView(std::get<std::string>(value))));
    context->global_metadata_tracker->SetDynamicMetadata(
        std::nullopt, std::nullopt, key_id, variadic);
  }
}

base::StatusOr<FileEntry> ParseFileEntry(const json::Dom& file) {
  if (!file.IsObject() || !file["path"].IsString()) {
    return base::ErrStatus(
        "perfetto_manifest: files entries must be objects with a string "
        "path");
  }
  FileEntry entry;
  entry.path = file["path"].AsString();
  if (file.HasMember("__exported_table_schema")) {
    if (file.HasMember("clocks") || file.HasMember("machine") ||
        file.HasMember("machines")) {
      return base::ErrStatus(
          "perfetto_manifest: '%s': an exported table is not a trace, so "
          "__exported_table_schema cannot be combined with "
          "clocks/machine/machines",
          entry.path.c_str());
    }
    ASSIGN_OR_RETURN(entry.exported_table_schema,
                     ParsePerfettoExportTable(file["__exported_table_schema"]));
    return std::move(entry);
  }
  if (file.HasMember("clocks")) {
    if (!file["clocks"].IsObject()) {
      return base::ErrStatus("perfetto_manifest: clocks must be an object");
    }
    ASSIGN_OR_RETURN(entry.clock_override, ParseClocks(file["clocks"]));
  }
  if (file.HasMember("machine") && file.HasMember("machines")) {
    return base::ErrStatus(
        "perfetto_manifest: machine and machines are mutually exclusive. Use "
        "`machine` for a single-machine file, or `machines` to remap a "
        "multi-machine proto's embedded ids; not both.");
  }
  // `machine` attributes the whole file to one named machine. It is an object
  // (rather than a bare string) so future per-machine attributes can be added
  // without a breaking format change.
  if (file.HasMember("machine")) {
    const json::Dom& machine = file["machine"];
    if (!machine.IsObject() || !machine.HasMember("name") ||
        !machine["name"].IsString()) {
      return base::ErrStatus(
          "perfetto_manifest: machine must be an object with a string name, "
          R"(e.g. "machine": {"name": "phone"}.)");
    }
    std::string name = machine["name"].AsString();
    if (name.empty()) {
      return base::ErrStatus(
          "perfetto_manifest: machine: name must be non-empty");
    }
    entry.machine_name = std::move(name);
  }
  // `machines` remaps a multi-machine proto's embedded machine_ids to named
  // machines.
  if (file.HasMember("machines")) {
    if (!file["machines"].IsArray()) {
      return base::ErrStatus("perfetto_manifest: machines must be an array");
    }
    for (const json::Dom& m : file["machines"]) {
      if (!m.IsObject() || !m.HasMember("id") || !m["id"].IsNumeric() ||
          !m.HasMember("name") || !m["name"].IsString()) {
        return base::ErrStatus(
            "perfetto_manifest: machines entries need an integer id and a "
            R"(string name, e.g. "machines": [{"id": 0, "name": "phone"}].)");
      }
      int64_t id = m["id"].AsInt64();
      if (id < 0 || id > std::numeric_limits<uint32_t>::max()) {
        return base::ErrStatus(
            "perfetto_manifest: machines: id must be in [0, 4294967295]");
      }
      std::string name = m["name"].AsString();
      if (name.empty()) {
        return base::ErrStatus(
            "perfetto_manifest: machines: name must be non-empty");
      }
      entry.machine_mappings.emplace_back(static_cast<uint32_t>(id),
                                          std::move(name));
    }
  }
  return std::move(entry);
}

// Allocates (once) the machine-table row for |raw_machine_id| and records it so
// later forks reuse the same row. Returns the row id.
uint32_t EnsureMachineRow(TraceProcessorContext* context,
                          int64_t raw_machine_id) {
  auto& map = context->trace_manifest_state->raw_id_to_table_id;
  if (uint32_t* row = map.Find(raw_machine_id)) {
    return *row;
  }
  uint32_t row = context->storage->mutable_machine_table()
                     ->Insert({raw_machine_id})
                     .id.value;
  map.Insert(raw_machine_id, row);
  return row;
}

}  // namespace

PerfettoManifestReader::PerfettoManifestReader(TraceProcessorContext* context,
                                               uint32_t file_id)
    : context_(context), file_id_(file_id) {}

PerfettoManifestReader::~PerfettoManifestReader() = default;

base::Status PerfettoManifestReader::Parse(TraceBlobView blob) {
  buffer_.append(reinterpret_cast<const char*>(blob.data()), blob.size());
  return base::OkStatus();
}

base::Status PerfettoManifestReader::OnPushDataToSorter() {
  auto* state = context_->trace_manifest_state.get();
  if (state->config_seen) {
    return base::ErrStatus("multiple perfetto_manifest files in archive");
  }
  state->config_seen = true;

  ASSIGN_OR_RETURN(json::Dom root, json::Parse(buffer_));
  buffer_.clear();
  if (!root.IsObject() || !root.HasMember("perfetto_manifest") ||
      !root["perfetto_manifest"].IsObject()) {
    return base::ErrStatus(
        "perfetto_manifest: expected a JSON object with a top-level "
        "perfetto_manifest key");
  }
  const json::Dom& meta = root["perfetto_manifest"];

  if (!meta.HasMember("version")) {
    return base::ErrStatus(
        "perfetto_manifest: missing required field: version");
  }
  const json::Dom& version = meta["version"];
  if (!version.IsInt() && !version.IsUint()) {
    return base::ErrStatus("perfetto_manifest: version must be an integer");
  }
  if (version.AsInt64() != 1) {
    return base::ErrStatus("perfetto_manifest: unsupported version: %" PRId64
                           ". Only version 1 is supported; set \"version\": 1.",
                           version.AsInt64());
  }

  if (meta.HasMember("trace_time")) {
    const json::Dom& trace_time = meta["trace_time"];
    if (!trace_time.IsObject()) {
      return base::ErrStatus("perfetto_manifest: trace_time must be an object");
    }
    if (!trace_time.HasMember("clock")) {
      return base::ErrStatus("perfetto_manifest: trace_time needs a clock");
    }
    ASSIGN_OR_RETURN(uint32_t clock, ParseClockName(trace_time["clock"]));
    state->trace_time_clock = clock;
    if (trace_time.HasMember("file")) {
      if (!trace_time["file"].IsString()) {
        return base::ErrStatus(
            "perfetto_manifest: trace_time: file must be a string");
      }
      state->trace_time_file = trace_time["file"].AsString();
    }
    if (trace_time.HasMember("machine")) {
      if (!trace_time["machine"].IsString()) {
        return base::ErrStatus(
            "perfetto_manifest: trace_time: machine must be a string");
      }
      state->trace_time_machine = trace_time["machine"].AsString();
    }
  }

  if (meta.HasMember("files")) {
    if (!meta["files"].IsArray()) {
      return base::ErrStatus("perfetto_manifest: files must be an array");
    }
    for (const json::Dom& file : meta["files"]) {
      ASSIGN_OR_RETURN(FileEntry entry, ParseFileEntry(file));
      state->files.push_back(std::move(entry));
    }
  }

  if (meta.HasMember("attributes")) {
    ASSIGN_OR_RETURN(std::vector<Attribute> attrs,
                     ParseAttributes(meta["attributes"]));
    ApplyAttributes(context_, attrs);
  }

  // Every member declared as a serialized table must be present: a missing
  // one would otherwise silently yield an empty table. All archive members
  // are added to the trace_file table before any is parsed, so this can be
  // caught here, before any table is deserialized.
  for (const FileEntry& entry : state->files) {
    if (!entry.exported_table_schema) {
      continue;
    }
    StringId name =
        context_->storage->InternString(base::StringView(entry.path));
    bool found = false;
    for (auto it = context_->storage->trace_file_table().IterateRows(); it;
         ++it) {
      if (it.name() && *it.name() == name) {
        found = true;
        break;
      }
    }
    if (!found) {
      return base::ErrStatus(
          "perfetto_manifest: table member '%s' is declared but not present "
          "in the archive",
          entry.path.c_str());
    }
  }
  return ApplyManifest();
}

// Applies the parsed manifest to the clock graph and machine table: allocates
// and names a machine row per declared machine, claims the global trace-time
// clock, and injects every file's clock-override edge. Runs entirely at parse
// time (before any other trace file), so forward references resolve.
base::Status PerfettoManifestReader::ApplyManifest() {
  auto* state = context_->trace_manifest_state.get();

  // Allocate one raw machine id per distinct name (the `machine` shorthand and
  // every `machines` entry share the namespace, so the same name is one
  // machine), pre-allocating and naming its row so clock references resolve to
  // it and ForkContextForTrace reuses it. Then set each file's base machine and
  // its embedded-id remap.
  auto& machine_table = *context_->storage->mutable_machine_table();
  base::FlatHashMap<std::string, int64_t> name_to_id;
  int64_t next_id = kFirstManifestMachineId;
  auto raw_id_for_name = [&](const std::string& name) {
    if (int64_t* id = name_to_id.Find(name)) {
      return *id;
    }
    int64_t id = next_id++;
    machine_table[MachineId(EnsureMachineRow(context_, id))].set_name(
        context_->storage->InternString(base::StringView(name)));
    name_to_id.Insert(name, id);
    return id;
  };
  for (FileEntry& entry : state->files) {
    if (entry.machine_name) {
      entry.machine_id = raw_id_for_name(*entry.machine_name);
    }
    for (const auto& [embedded, name] : entry.machine_mappings) {
      int64_t raw = raw_id_for_name(name);
      entry.machine_remap.Insert(embedded, raw);
      if (embedded == 0) {
        entry.machine_id = raw;
        entry.machine_name = name;
      }
    }
  }

  // Resolves a clock-reference target (a clocks is.file/is.machine, or the
  // trace_time file/machine) to a raw machine id. |file_label|/|machine_label|
  // name the two fields in error messages. The rules:
  //   - machine without file: ambiguous, rejected. A machine name alone is not
  //     a stable key because embedded ids are scoped to their trace.
  //   - file + machine: the machine must be one |ref_file| itself declares;
  //     (file, name) is the key.
  //   - file alone: only a single-machine file; a multi-machine file is several
  //     machines, so it must also name which one.
  //   - neither: |fallback| (the referencing file's own machine).
  auto resolve_ref = [&](const char* file_label, const char* machine_label,
                         const std::optional<std::string>& ref_file,
                         const std::optional<std::string>& ref_machine,
                         int64_t fallback) -> base::StatusOr<int64_t> {
    if (ref_machine && !ref_file) {
      return base::ErrStatus(
          "perfetto_manifest: %s '%s' needs %s too: a machine name alone is "
          "ambiguous because embedded machine ids are scoped to their trace.",
          machine_label, ref_machine->c_str(), file_label);
    }
    if (!ref_file) {
      return fallback;
    }
    FileEntry* r = state->FindEntry(*ref_file);
    if (!r) {
      return base::ErrStatus(
          "perfetto_manifest: %s names unknown file '%s'. It must match the "
          "`path` of an entry in the `files` array.",
          file_label, ref_file->c_str());
    }
    if (!ref_machine) {
      if (!r->machine_mappings.empty()) {
        return base::ErrStatus(
            "perfetto_manifest: %s '%s' is a multi-machine trace; also name "
            "the machine with %s.",
            file_label, ref_file->c_str(), machine_label);
      }
      return r->machine_id.value_or(0);
    }
    bool declared = r->machine_name && *r->machine_name == *ref_machine;
    for (const auto& [embedded, name] : r->machine_mappings) {
      if (name == *ref_machine) {
        declared = true;
        break;
      }
    }
    if (!declared) {
      return base::ErrStatus(
          "perfetto_manifest: %s '%s' is not a machine declared by file '%s'.",
          machine_label, ref_machine->c_str(), ref_file->c_str());
    }
    return *name_to_id.Find(*ref_machine);
  };

  // Claim the global trace time clock directly: the manifest is the first file,
  // so its claim wins over later traces. trace_time.file (+ .machine) pins it
  // to that file's (pre-allocated) machine.
  if (state->trace_time_clock) {
    ClockId trace_time = ClockId::Machine(*state->trace_time_clock);
    if (state->trace_time_file || state->trace_time_machine) {
      ASSIGN_OR_RETURN(
          int64_t raw,
          resolve_ref("trace_time: file", "trace_time: machine",
                      state->trace_time_file, state->trace_time_machine,
                      /*fallback=*/0));
      trace_time = ClockId::Machine(EnsureMachineRow(context_, raw),
                                    *state->trace_time_clock);
    }
    context_->trace_time_state->TrySetClock(trace_time, file_id_);
    context_->global_metadata_tracker->SetMetadata(
        std::nullopt, std::nullopt, metadata::trace_time_clock_id,
        Variadic::Integer(*state->trace_time_clock));
  }

  // Resolves which machine declared by |entry| owns its source clock: the named
  // |source_machine| (which must be one this file itself declares), or - when
  // unset - the file's sole machine. A multi-machine file is several machines,
  // so it must name which one.
  auto resolve_source_machine =
      [&](const FileEntry& entry,
          const ClockOverride& co) -> base::StatusOr<int64_t> {
    if (!co.source_machine) {
      if (!entry.machine_mappings.empty()) {
        return base::ErrStatus(
            "perfetto_manifest: clocks: file '%s' is a multi-machine trace; "
            "name which machine the clock is on with clocks: machine.",
            entry.path.c_str());
      }
      return entry.machine_id.value_or(0);
    }
    bool declared =
        entry.machine_name && *entry.machine_name == *co.source_machine;
    for (const auto& [embedded, name] : entry.machine_mappings) {
      if (name == *co.source_machine) {
        declared = true;
        break;
      }
    }
    if (!declared) {
      return base::ErrStatus(
          "perfetto_manifest: clocks: machine '%s' is not a machine declared "
          "by file '%s'.",
          co.source_machine->c_str(), entry.path.c_str());
    }
    return *name_to_id.Find(*co.source_machine);
  };

  // Resolves a declared file's trace_file_table id by path; a TraceFile-clock
  // edge endpoint needs it. All archive members are added to the table before
  // any is parsed, so the ids exist by the time this (the first file's) reader
  // runs.
  auto find_trace_file_id =
      [&](const std::string& path) -> base::StatusOr<uint32_t> {
    StringId name = context_->storage->InternString(base::StringView(path));
    for (auto it = context_->storage->trace_file_table().IterateRows(); it;
         ++it) {
      if (it.name() && *it.name() == name) {
        return it.id().value;
      }
    }
    return base::ErrStatus(
        "perfetto_manifest: clocks: could not resolve file '%s'. It must match "
        "the `path` of an entry in the `files` array.",
        path.c_str());
  };

  // Add every override's cross-machine edge to the global clock graph now,
  // before any file is parsed (a file may reference another parsed later),
  // recording it in the clock_snapshot table as ClockTracker would. The source
  // is a named builtin (RELATE) or this file's own private TraceFile clock (a
  // pinned/clockless source); the reference is a named builtin or - an omitted
  // clock - the reference's private TraceFile clock. At a common instant the
  // source reads T when the reference reads T + offset_ns.
  ClockId trace_time = context_->trace_time_state->clock_id;
  for (const FileEntry& entry : state->files) {
    if (!entry.clock_override || !entry.clock_override->ref_file) {
      continue;
    }
    const ClockOverride& co = *entry.clock_override;
    ASSIGN_OR_RETURN(int64_t source_raw, resolve_source_machine(entry, co));
    ASSIGN_OR_RETURN(
        int64_t ref_raw,
        resolve_ref("clocks: sync_to.file", "clocks: sync_to.machine",
                    co.ref_file, co.ref_machine, source_raw));
    uint32_t source_row = EnsureMachineRow(context_, source_raw);
    uint32_t ref_row = EnsureMachineRow(context_, ref_raw);

    ClockId source;
    if (co.source_clock) {
      source = ClockId::Machine(source_row, *co.source_clock);
    } else {
      ASSIGN_OR_RETURN(uint32_t tid, find_trace_file_id(entry.path));
      source = ClockId::TraceFile(tid);
      source.machine_id = source_row;
    }
    ClockId ref;
    if (co.ref_clock) {
      ref = ClockId::Machine(ref_row, *co.ref_clock);
    } else {
      ASSIGN_OR_RETURN(uint32_t tid, find_trace_file_id(*co.ref_file));
      ref = ClockId::TraceFile(tid);
      ref.machine_id = ref_row;
    }
    int64_t source_ts = co.offset_ns < 0 ? -co.offset_ns : 0;
    int64_t ref_ts = co.offset_ns > 0 ? co.offset_ns : 0;
    std::vector<ClockTimestamp> clocks = {{source, source_ts}, {ref, ref_ts}};
    ASSIGN_OR_RETURN(uint32_t id, context_->clock_sync->AddSnapshot(clocks));
    ClockTracker::AddSnapshotToTable(context_->storage.get(),
                                     context_->clock_sync.get(), trace_time, id,
                                     clocks);
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::perfetto_manifest

namespace perfetto::trace_processor {
namespace {

// A perfetto_manifest sidecar file: a JSON object whose only top-level key is
// "perfetto_manifest". Must be the first file; overrides clock/machine handling
// for the files that follow, so it produces no timeline and forks no context.
class PerfettoManifestImporter
    : public TraceImporter<PerfettoManifestImporter> {
 public:
  PerfettoManifestImporter() : TraceImporter(MakeDescriptor()) {}
  ~PerfettoManifestImporter() override;

  bool Sniff(const uint8_t* data, size_t size) const override {
    std::string start(reinterpret_cast<const char*>(data),
                      std::min<size_t>(size, kGuessTraceMaxLookahead));
    start.erase(std::remove_if(start.begin(), start.end(), base::IsSpace),
                start.end());
    return base::StartsWith(start, "{\"perfetto_manifest\"");
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t file_id) const override {
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<perfetto_manifest::PerfettoManifestReader>(context,
                                                                    file_id));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "perfetto_manifest";
    d.archive_priority = -1;
    d.forks_context = false;
    d.is_manifest = true;
    d.detection_priority = 90;
    return d;
  }
};

PerfettoManifestImporter::~PerfettoManifestImporter() = default;

}  // namespace

std::unique_ptr<TraceImporterBase> CreatePerfettoManifestImporter() {
  return std::make_unique<PerfettoManifestImporter>();
}

}  // namespace perfetto::trace_processor
