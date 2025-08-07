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

#include "perfetto/profiling/pprof_builder.h"

#include "perfetto/base/build_config.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <cxxabi.h>
#endif

#include <algorithm>
#include <cinttypes>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/traceconv/utils.h"

#include "protos/third_party/pprof/profile.pbzero.h"

// Quick hint on navigating the file:
// Conversions for both perf and heap profiles start with |TraceToPprof|.
// Non-shared logic is in the |heap_profile| and |perf_profile| namespaces.
//
// To build one or more profiles, first the callstack information is queried
// from the SQL tables, and converted into an in-memory representation by
// |PreprocessLocations|. Then an instance of |GProfileBuilder| is used to
// accumulate samples for that profile, and emit all additional information as a
// serialized proto. Only the entities referenced by that particular
// |GProfileBuilder| instance are emitted.
//
// See protos/third_party/pprof/profile.proto for the meaning of terms like
// function/location/line.

namespace {
using StringId = ::perfetto::trace_processor::StringPool::Id;

// In-memory representation of a Profile.Function.
struct Function {
  StringId name_id = StringId::Null();
  StringId system_name_id = StringId::Null();
  StringId filename_id = StringId::Null();

  Function(StringId n, StringId s, StringId f)
      : name_id(n), system_name_id(s), filename_id(f) {}

  bool operator==(const Function& other) const {
    return std::tie(name_id, system_name_id, filename_id) ==
           std::tie(other.name_id, other.system_name_id, other.filename_id);
  }
};

// In-memory representation of a Profile.Line.
struct Line {
  int64_t function_id = 0;  // LocationTracker's interned Function id
  int64_t line_no = 0;

  Line(int64_t func, int64_t line) : function_id(func), line_no(line) {}

  bool operator==(const Line& other) const {
    return function_id == other.function_id && line_no == other.line_no;
  }
};

// In-memory representation of a Profile.Location.
struct Location {
  int64_t mapping_id = 0;  // sqlite row id
  // Common case: location references a single function.
  int64_t single_function_id = 0;  // interned Function id
  // Alternatively: multiple inlined functions, recovered via offline
  // symbolisation. Leaf-first ordering.
  std::vector<Line> inlined_functions;

  Location(int64_t map, int64_t func, std::vector<Line> inlines)
      : mapping_id(map),
        single_function_id(func),
        inlined_functions(std::move(inlines)) {}

  bool operator==(const Location& other) const {
    return std::tie(mapping_id, single_function_id, inlined_functions) ==
           std::tie(other.mapping_id, other.single_function_id,
                    other.inlined_functions);
  }
};
}  // namespace

template <>
struct std::hash<Function> {
  size_t operator()(const Function& loc) const {
    perfetto::base::Hasher hasher;
    hasher.Update(loc.name_id.raw_id());
    hasher.Update(loc.system_name_id.raw_id());
    hasher.Update(loc.filename_id.raw_id());
    return static_cast<size_t>(hasher.digest());
  }
};

template <>
struct std::hash<Location> {
  size_t operator()(const Location& loc) const {
    perfetto::base::Hasher hasher;
    hasher.Update(loc.mapping_id);
    hasher.Update(loc.single_function_id);
    for (auto line : loc.inlined_functions) {
      hasher.Update(line.function_id);
      hasher.Update(line.line_no);
    }
    return static_cast<size_t>(hasher.digest());
  }
};

namespace perfetto {
namespace trace_to_text {
namespace {

using ::perfetto::trace_processor::Iterator;

uint64_t ToPprofId(int64_t id) {
  PERFETTO_DCHECK(id >= 0);
  return static_cast<uint64_t>(id) + 1;
}

std::string AsCsvString(std::vector<uint64_t> vals) {
  std::string ret;
  for (size_t i = 0; i < vals.size(); i++) {
    if (i != 0) {
      ret += ",";
    }
    ret += std::to_string(vals[i]);
  }
  return ret;
}

std::optional<int64_t> GetStatsEntry(
    trace_processor::TraceProcessor* tp,
    const std::string& name,
    std::optional<uint64_t> idx = std::nullopt) {
  std::string query = "select value from stats where name == '" + name + "'";
  if (idx.has_value())
    query += " and idx == " + std::to_string(idx.value());

  auto it = tp->ExecuteQuery(query);
  if (!it.Next()) {
    if (!it.Status().ok()) {
      PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                              it.Status().message().c_str());
      return std::nullopt;
    }
    // some stats are not present unless non-zero
    return std::make_optional(0);
  }
  return std::make_optional(it.Get(0).AsLong());
}

// Interns Locations, Lines, and Functions. Interning is done by the entity's
// contents, and has no relation to the row ids in the SQL tables.
// Contains all data for the trace, so can be reused when emitting multiple
// profiles.
//
// TODO(rsavitski): consider moving mappings into here as well. For now, they're
// still emitted in a single scan during profile building. Mappings should be
// unique-enough already in the SQL tables, with only incremental state clearing
// duplicating entries.
class LocationTracker {
 public:
  int64_t InternLocation(Location loc) {
    auto it = locations_.find(loc);
    if (it == locations_.end()) {
      bool inserted = false;
      std::tie(it, inserted) = locations_.emplace(
          std::move(loc), static_cast<int64_t>(locations_.size()));
      PERFETTO_DCHECK(inserted);
    }
    return it->second;
  }

  int64_t InternFunction(Function func) {
    auto it = functions_.find(func);
    if (it == functions_.end()) {
      bool inserted = false;
      std::tie(it, inserted) =
          functions_.emplace(func, static_cast<int64_t>(functions_.size()));
      PERFETTO_DCHECK(inserted);
    }
    return it->second;
  }

  bool IsCallsiteProcessed(int64_t callstack_id) const {
    return callsite_to_locations_.find(callstack_id) !=
           callsite_to_locations_.end();
  }

  void MaybeSetCallsiteLocations(int64_t callstack_id,
                                 const std::vector<int64_t>& locs) {
    // nop if already set
    callsite_to_locations_.emplace(callstack_id, locs);
  }

  const std::vector<int64_t>& LocationsForCallstack(
      int64_t callstack_id) const {
    auto it = callsite_to_locations_.find(callstack_id);
    PERFETTO_CHECK(callstack_id >= 0 && it != callsite_to_locations_.end());
    return it->second;
  }

  const std::unordered_map<Location, int64_t>& AllLocations() const {
    return locations_;
  }
  const std::unordered_map<Function, int64_t>& AllFunctions() const {
    return functions_;
  }

 private:
  // Root-first location ids for a given callsite id.
  std::unordered_map<int64_t, std::vector<int64_t>> callsite_to_locations_;
  std::unordered_map<Location, int64_t> locations_;
  std::unordered_map<Function, int64_t> functions_;
};

struct PreprocessedInline {
  // |name_id| is already demangled
  StringId name_id = StringId::Null();
  StringId filename_id = StringId::Null();
  int64_t line_no = 0;

  PreprocessedInline(StringId s, StringId f, int64_t line)
      : name_id(s), filename_id(f), line_no(line) {}
};

std::unordered_map<int64_t, std::vector<PreprocessedInline>>
PreprocessInliningInfo(trace_processor::TraceProcessor* tp,
                       trace_processor::StringPool* interner) {
  std::unordered_map<int64_t, std::vector<PreprocessedInline>> inlines;

  // Most-inlined function (leaf) has the lowest id within a symbol set. Query
  // such that the per-set line vectors are built up leaf-first.
  Iterator it = tp->ExecuteQuery(
      "select symbol_set_id, name, source_file, line_number from "
      "stack_profile_symbol order by symbol_set_id asc, id asc;");
  while (it.Next()) {
    int64_t symbol_set_id = it.Get(0).AsLong();
    auto func_sysname = it.Get(1).is_null() ? "" : it.Get(1).AsString();
    auto filename = it.Get(2).is_null() ? "" : it.Get(2).AsString();
    int64_t line_no = it.Get(3).is_null() ? 0 : it.Get(3).AsLong();

    inlines[symbol_set_id].emplace_back(interner->InternString(func_sysname),
                                        interner->InternString(filename),
                                        line_no);
  }

  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
    return {};
  }
  return inlines;
}

// Extracts and interns the unique frames and locations (as defined by the proto
// format) from the callstack SQL tables.
//
// Approach:
//   * for each callstack (callsite ids of the leaves):
//     * use experimental_annotated_callstack to build the full list of
//       constituent frames
//     * for each frame (root to leaf):
//         * intern the location and function(s)
//         * remember the mapping from callsite_id to the callstack so far (from
//            the root and including the frame being considered)
//
// Optionally mixes in the annotations as a frame name suffix (since there's no
// good way to attach extra info to locations in the proto format). This relies
// on the annotations (produced by experimental_annotated_callstack) to be
// stable for a given callsite (equivalently: dependent only on their parents).
LocationTracker PreprocessLocations(trace_processor::TraceProcessor* tp,
                                    trace_processor::StringPool* interner,
                                    bool annotate_frames) {
  LocationTracker tracker;

  // Keyed by symbol_set_id, discarded once this function converts the inlines
  // into Line and Function entries.
  std::unordered_map<int64_t, std::vector<PreprocessedInline>> inlining_info =
      PreprocessInliningInfo(tp, interner);

  // Higher callsite ids most likely correspond to the deepest stacks, so we'll
  // fill more of the overall callsite->location map by visiting the callsited
  // in decreasing id order. Since processing a callstack also fills in the data
  // for all parent callsites.
  Iterator cid_it = tp->ExecuteQuery(
      "select id from stack_profile_callsite order by id desc;");
  while (cid_it.Next()) {
    int64_t query_cid = cid_it.Get(0).AsLong();

    // If the leaf has been processed, the rest of the stack is already known.
    if (tracker.IsCallsiteProcessed(query_cid))
      continue;

    std::string annotated_query =
        "select sp.id, sp.annotation, spf.mapping, spf.name, "
        "coalesce(spf.deobfuscated_name, demangle(spf.name), spf.name), "
        "spf.symbol_set_id from "
        "experimental_annotated_callstack(" +
        std::to_string(query_cid) +
        ") sp join stack_profile_frame spf on (sp.frame_id == spf.id) "
        "order by depth asc";
    Iterator c_it = tp->ExecuteQuery(annotated_query);

    std::vector<int64_t> callstack_loc_ids;
    while (c_it.Next()) {
      int64_t cid = c_it.Get(0).AsLong();
      auto annotation = c_it.Get(1).is_null() ? "" : c_it.Get(1).AsString();
      int64_t mapping_id = c_it.Get(2).AsLong();
      auto func_sysname = c_it.Get(3).is_null() ? "" : c_it.Get(3).AsString();
      auto func_name = c_it.Get(4).is_null() ? "" : c_it.Get(4).AsString();
      std::optional<int64_t> symbol_set_id =
          c_it.Get(5).is_null() ? std::nullopt
                                : std::make_optional(c_it.Get(5).AsLong());

      Location loc(mapping_id, /*single_function_id=*/-1, {});

      auto intern_function = [interner, &tracker, annotate_frames](
                                 StringId func_sysname_id,
                                 StringId original_func_name_id,
                                 StringId filename_id,
                                 const std::string& anno) {
        std::string fname = interner->Get(original_func_name_id).ToStdString();
        if (annotate_frames && !anno.empty() && !fname.empty())
          fname = fname + " [" + anno + "]";
        StringId func_name_id = interner->InternString(base::StringView(fname));
        Function func(func_name_id, func_sysname_id, filename_id);
        return tracker.InternFunction(func);
      };

      // Inlining information available
      if (symbol_set_id.has_value()) {
        auto it = inlining_info.find(*symbol_set_id);
        if (it == inlining_info.end()) {
          PERFETTO_DFATAL_OR_ELOG(
              "Failed to find stack_profile_symbol entry for symbol_set_id "
              "%" PRIi64 "",
              *symbol_set_id);
          return {};
        }

        // N inlined functions
        // The symbolised packets currently assume pre-demangled data (as that's
        // the default of llvm-symbolizer), so we don't have a system name for
        // each deinlined frame. Set the human-readable name for both fields. We
        // can change this, but there's no demand for accurate system names in
        // pprofs.
        for (const auto& line : it->second) {
          int64_t func_id = intern_function(line.name_id, line.name_id,
                                            line.filename_id, annotation);

          loc.inlined_functions.emplace_back(func_id, line.line_no);
        }
      } else {
        // Otherwise - single function
        int64_t func_id =
            intern_function(interner->InternString(func_sysname),
                            interner->InternString(func_name),
                            /*filename_id=*/StringId::Null(), annotation);
        loc.single_function_id = func_id;
      }

      int64_t loc_id = tracker.InternLocation(std::move(loc));

      // Update the tracker with the locations so far (for example, at depth 2,
      // we'll have 3 root-most locations in |callstack_loc_ids|).
      callstack_loc_ids.push_back(loc_id);
      tracker.MaybeSetCallsiteLocations(cid, callstack_loc_ids);
    }

    if (!c_it.Status().ok()) {
      PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                              c_it.Status().message().c_str());
      return {};
    }
  }

  if (!cid_it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            cid_it.Status().message().c_str());
    return {};
  }

  return tracker;
}

// Builds the |perftools.profiles.Profile| proto.
class GProfileBuilder {
 public:
  GProfileBuilder(const LocationTracker& locations,
                  trace_processor::StringPool* interner)
      : locations_(locations), interner_(interner) {
    // The pprof format requires the first entry in the string table to be the
    // empty string.
    int64_t empty_id = ToStringTableId(StringId::Null());
    PERFETTO_CHECK(empty_id == 0);
  }

  void WriteSampleTypes(
      const std::vector<std::pair<std::string, std::string>>& sample_types) {
    for (const auto& st : sample_types) {
      auto* sample_type = result_->add_sample_type();
      sample_type->set_type(
          ToStringTableId(interner_->InternString(base::StringView(st.first))));
      sample_type->set_unit(ToStringTableId(
          interner_->InternString(base::StringView(st.second))));
    }
  }

  bool AddSample(const protozero::PackedVarInt& values, int64_t callstack_id) {
    const auto& location_ids = locations_.LocationsForCallstack(callstack_id);
    if (location_ids.empty()) {
      PERFETTO_DFATAL_OR_ELOG(
          "Failed to find frames for callstack id %" PRIi64 "", callstack_id);
      return false;
    }

    // LocationTracker stores location lists root-first, but the pprof format
    // requires leaf-first.
    protozero::PackedVarInt packed_locs;
    for (auto it = location_ids.rbegin(); it != location_ids.rend(); ++it)
      packed_locs.Append(ToPprofId(*it));

    auto* gsample = result_->add_sample();
    gsample->set_value(values);
    gsample->set_location_id(packed_locs);

    // Remember the locations s.t. we only serialize the referenced ones.
    seen_locations_.insert(location_ids.cbegin(), location_ids.cend());
    return true;
  }

  std::string CompleteProfile(trace_processor::TraceProcessor* tp,
                              bool write_mappings = true) {
    std::set<int64_t> seen_mappings;
    std::set<int64_t> seen_functions;

    if (!WriteLocations(&seen_mappings, &seen_functions))
      return {};
    if (!WriteFunctions(seen_functions))
      return {};
    if (write_mappings && !WriteMappings(tp, seen_mappings))
      return {};

    WriteStringTable();
    return result_.SerializeAsString();
  }

 private:
  // Serializes the Profile.Location entries referenced by this profile.
  bool WriteLocations(std::set<int64_t>* seen_mappings,
                      std::set<int64_t>* seen_functions) {
    const std::unordered_map<Location, int64_t>& locations =
        locations_.AllLocations();

    size_t written_locations = 0;
    for (const auto& loc_and_id : locations) {
      const auto& loc = loc_and_id.first;
      int64_t id = loc_and_id.second;

      if (seen_locations_.find(id) == seen_locations_.end())
        continue;

      written_locations += 1;
      seen_mappings->emplace(loc.mapping_id);

      auto* glocation = result_->add_location();
      glocation->set_id(ToPprofId(id));
      glocation->set_mapping_id(ToPprofId(loc.mapping_id));

      if (!loc.inlined_functions.empty()) {
        for (const auto& line : loc.inlined_functions) {
          seen_functions->insert(line.function_id);

          auto* gline = glocation->add_line();
          gline->set_function_id(ToPprofId(line.function_id));
          gline->set_line(line.line_no);
        }
      } else {
        seen_functions->insert(loc.single_function_id);

        glocation->add_line()->set_function_id(
            ToPprofId(loc.single_function_id));
      }
    }

    if (written_locations != seen_locations_.size()) {
      PERFETTO_DFATAL_OR_ELOG(
          "Found only %zu/%zu locations during serialization.",
          written_locations, seen_locations_.size());
      return false;
    }
    return true;
  }

  // Serializes the Profile.Function entries referenced by this profile.
  bool WriteFunctions(const std::set<int64_t>& seen_functions) {
    const std::unordered_map<Function, int64_t>& functions =
        locations_.AllFunctions();

    size_t written_functions = 0;
    for (const auto& func_and_id : functions) {
      const auto& func = func_and_id.first;
      int64_t id = func_and_id.second;

      if (seen_functions.find(id) == seen_functions.end())
        continue;

      written_functions += 1;

      auto* gfunction = result_->add_function();
      gfunction->set_id(ToPprofId(id));
      gfunction->set_name(ToStringTableId(func.name_id));
      gfunction->set_system_name(ToStringTableId(func.system_name_id));
      if (!func.filename_id.is_null())
        gfunction->set_filename(ToStringTableId(func.filename_id));
    }

    if (written_functions != seen_functions.size()) {
      PERFETTO_DFATAL_OR_ELOG(
          "Found only %zu/%zu functions during serialization.",
          written_functions, seen_functions.size());
      return false;
    }
    return true;
  }

  // Serializes the Profile.Mapping entries referenced by this profile.
  bool WriteMappings(trace_processor::TraceProcessor* tp,
                     const std::set<int64_t>& seen_mappings) {
    Iterator mapping_it = tp->ExecuteQuery(
        "SELECT id, exact_offset, start, end, name, build_id "
        "FROM stack_profile_mapping;");
    size_t mappings_no = 0;
    while (mapping_it.Next()) {
      int64_t id = mapping_it.Get(0).AsLong();
      if (seen_mappings.find(id) == seen_mappings.end())
        continue;
      ++mappings_no;
      auto interned_filename = ToStringTableId(
          interner_->InternString(mapping_it.Get(4).AsString()));
      auto interned_build_id = ToStringTableId(
          interner_->InternString(mapping_it.Get(5).AsString()));
      auto* gmapping = result_->add_mapping();
      gmapping->set_id(ToPprofId(id));
      gmapping->set_file_offset(
          static_cast<uint64_t>(mapping_it.Get(1).AsLong()));
      gmapping->set_memory_start(
          static_cast<uint64_t>(mapping_it.Get(2).AsLong()));
      gmapping->set_memory_limit(
          static_cast<uint64_t>(mapping_it.Get(3).AsLong()));
      gmapping->set_filename(interned_filename);
      gmapping->set_build_id(interned_build_id);
    }
    if (!mapping_it.Status().ok()) {
      PERFETTO_DFATAL_OR_ELOG("Invalid mapping iterator: %s",
                              mapping_it.Status().message().c_str());
      return false;
    }
    if (mappings_no != seen_mappings.size()) {
      PERFETTO_DFATAL_OR_ELOG("Missing mappings.");
      return false;
    }
    return true;
  }

  void WriteStringTable() {
    for (StringId id : string_table_) {
      trace_processor::NullTermStringView s = interner_->Get(id);
      result_->add_string_table(s.data(), s.size());
    }
  }

  int64_t ToStringTableId(StringId interned_id) {
    auto it = interning_remapper_.find(interned_id);
    if (it == interning_remapper_.end()) {
      int64_t table_id = static_cast<int64_t>(string_table_.size());
      string_table_.push_back(interned_id);
      bool inserted = false;
      std::tie(it, inserted) =
          interning_remapper_.emplace(interned_id, table_id);
      PERFETTO_DCHECK(inserted);
    }
    return it->second;
  }

  // Contains all locations, lines, functions (in memory):
  const LocationTracker& locations_;

  // String interner, strings referenced by LocationTracker are already
  // interned. The new internings will come from mappings, and sample types.
  trace_processor::StringPool* interner_;

  // The profile format uses the repeated string_table field's index as an
  // implicit id, so these structures remap the interned strings into sequential
  // ids. Only the strings referenced by this GProfileBuilder instance will be
  // added to the table.
  std::unordered_map<StringId, int64_t> interning_remapper_;
  std::vector<StringId> string_table_;

  // Profile proto being serialized.
  protozero::HeapBuffered<third_party::perftools::profiles::pbzero::Profile>
      result_;

  // Set of locations referenced by the added samples.
  std::set<int64_t> seen_locations_;
};

namespace heap_profile {
struct View {
  const char* type;
  const char* unit;
  const char* aggregator;
  const char* filter;
};

const View kMallocViews[] = {
    {"Total malloc count", "count", "sum(count)", "size >= 0"},
    {"Total malloc size", "bytes", "SUM(size)", "size >= 0"},
    {"Unreleased malloc count", "count", "SUM(count)", nullptr},
    {"Unreleased malloc size", "bytes", "SUM(size)", nullptr}};

const View kGenericViews[] = {
    {"Total count", "count", "sum(count)", "size >= 0"},
    {"Total size", "bytes", "SUM(size)", "size >= 0"},
    {"Unreleased count", "count", "SUM(count)", nullptr},
    {"Unreleased size", "bytes", "SUM(size)", nullptr}};

const View kJavaSamplesViews[] = {
    {"Total allocation count", "count", "SUM(count)", nullptr},
    {"Total allocation size", "bytes", "SUM(size)", nullptr}};

static bool VerifyPIDStats(trace_processor::TraceProcessor* tp, uint64_t pid) {
  bool success = true;
  std::optional<int64_t> stat =
      GetStatsEntry(tp, "heapprofd_buffer_corrupted", std::make_optional(pid));
  if (!stat.has_value()) {
    PERFETTO_DFATAL_OR_ELOG("Failed to get heapprofd_buffer_corrupted stat");
  } else if (stat.value() > 0) {
    success = false;
    PERFETTO_ELOG("WARNING: The profile for %" PRIu64
                  " ended early due to a buffer corruption."
                  " THIS IS ALWAYS A BUG IN HEAPPROFD OR"
                  " CLIENT MEMORY CORRUPTION.",
                  pid);
  }
  stat = GetStatsEntry(tp, "heapprofd_buffer_overran", std::make_optional(pid));
  if (!stat.has_value()) {
    PERFETTO_DFATAL_OR_ELOG("Failed to get heapprofd_buffer_overran stat");
  } else if (stat.value() > 0) {
    success = false;
    PERFETTO_ELOG("WARNING: The profile for %" PRIu64
                  " ended early due to a buffer overrun.",
                  pid);
  }

  stat = GetStatsEntry(tp, "heapprofd_rejected_concurrent", pid);
  if (!stat.has_value()) {
    PERFETTO_DFATAL_OR_ELOG("Failed to get heapprofd_rejected_concurrent stat");
  } else if (stat.value() > 0) {
    success = false;
    PERFETTO_ELOG("WARNING: The profile for %" PRIu64
                  " was rejected due to a concurrent profile.",
                  pid);
  }
  return success;
}

static std::vector<Iterator> BuildViewIterators(
    trace_processor::TraceProcessor* tp,
    uint64_t upid,
    uint64_t ts,
    const char* heap_name,
    const std::vector<View>& views) {
  std::vector<Iterator> view_its;
  for (const View& v : views) {
    std::string query = "SELECT hpa.callsite_id ";
    query +=
        ", " + std::string(v.aggregator) + " FROM heap_profile_allocation hpa ";
    // TODO(fmayer): Figure out where negative callsite_id comes from.
    query += "WHERE hpa.callsite_id >= 0 ";
    query += "AND hpa.upid = " + std::to_string(upid) + " ";
    query += "AND hpa.ts <= " + std::to_string(ts) + " ";
    query += "AND hpa.heap_name = '" + std::string(heap_name) + "' ";
    if (v.filter)
      query += "AND " + std::string(v.filter) + " ";
    query += "GROUP BY hpa.callsite_id;";
    view_its.emplace_back(tp->ExecuteQuery(query));
  }
  return view_its;
}

static bool WriteAllocations(GProfileBuilder* builder,
                             std::vector<Iterator>* view_its) {
  for (;;) {
    bool all_next = true;
    bool any_next = false;
    for (size_t i = 0; i < view_its->size(); ++i) {
      Iterator& it = (*view_its)[i];
      bool next = it.Next();
      if (!it.Status().ok()) {
        PERFETTO_DFATAL_OR_ELOG("Invalid view iterator: %s",
                                it.Status().message().c_str());
        return false;
      }
      all_next = all_next && next;
      any_next = any_next || next;
    }

    if (!all_next) {
      PERFETTO_CHECK(!any_next);
      break;
    }

    protozero::PackedVarInt sample_values;
    int64_t callstack_id = -1;
    for (size_t i = 0; i < view_its->size(); ++i) {
      if (i == 0) {
        callstack_id = (*view_its)[i].Get(0).AsLong();
      } else if (callstack_id != (*view_its)[i].Get(0).AsLong()) {
        PERFETTO_DFATAL_OR_ELOG("Wrong callstack.");
        return false;
      }
      sample_values.Append((*view_its)[i].Get(1).AsLong());
    }

    if (!builder->AddSample(sample_values, callstack_id))
      return false;
  }
  return true;
}

static bool TraceToHeapPprof(trace_processor::TraceProcessor* tp,
                             std::vector<SerializedProfile>* output,
                             bool annotate_frames,
                             uint64_t target_pid,
                             const std::vector<uint64_t>& target_timestamps) {
  trace_processor::StringPool interner;
  LocationTracker locations =
      PreprocessLocations(tp, &interner, annotate_frames);

  bool any_fail = false;
  Iterator it = tp->ExecuteQuery(
      "select distinct hpa.upid, hpa.ts, p.pid, hpa.heap_name "
      "from heap_profile_allocation hpa, "
      "process p where p.upid = hpa.upid;");
  while (it.Next()) {
    GProfileBuilder builder(locations, &interner);
    uint64_t upid = static_cast<uint64_t>(it.Get(0).AsLong());
    uint64_t ts = static_cast<uint64_t>(it.Get(1).AsLong());
    uint64_t profile_pid = static_cast<uint64_t>(it.Get(2).AsLong());
    const char* heap_name = it.Get(3).AsString();
    if ((target_pid > 0 && profile_pid != target_pid) ||
        (!target_timestamps.empty() &&
         std::find(target_timestamps.begin(), target_timestamps.end(), ts) ==
             target_timestamps.end())) {
      continue;
    }

    if (!VerifyPIDStats(tp, profile_pid))
      any_fail = true;

    std::vector<View> views;
    if (base::StringView(heap_name) == "libc.malloc") {
      views.assign(std::begin(kMallocViews), std::end(kMallocViews));
    } else if (base::StringView(heap_name) == "com.android.art") {
      views.assign(std::begin(kJavaSamplesViews), std::end(kJavaSamplesViews));
    } else {
      views.assign(std::begin(kGenericViews), std::end(kGenericViews));
    }

    std::vector<std::pair<std::string, std::string>> sample_types;
    for (const View& view : views) {
      sample_types.emplace_back(view.type, view.unit);
    }
    builder.WriteSampleTypes(sample_types);

    std::vector<Iterator> view_its =
        BuildViewIterators(tp, upid, ts, heap_name, views);
    std::string profile_proto;
    if (WriteAllocations(&builder, &view_its)) {
      profile_proto = builder.CompleteProfile(tp);
    }
    output->emplace_back(
        SerializedProfile{ProfileType::kHeapProfile, profile_pid,
                          std::move(profile_proto), heap_name});
  }

  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
    return false;
  }
  if (any_fail) {
    PERFETTO_ELOG(
        "One or more of your profiles had an issue. Please consult "
        "https://perfetto.dev/docs/data-sources/"
        "native-heap-profiler#troubleshooting");
  }
  return true;
}
}  // namespace heap_profile

namespace java_heap_profile {
struct View {
  const char* type;
  const char* unit;
  const char* query;
};

constexpr View kJavaAllocationViews[] = {
    {"Total allocation count", "count", "count"},
    {"Total allocation size", "bytes", "size"}};

std::string CreateHeapDumpFlameGraphQuery(const std::string& columns,
                                          const uint64_t upid,
                                          const uint64_t ts) {
  std::string query = "SELECT " + columns + " ";
  query += "FROM experimental_flamegraph(";

  const std::vector<std::string> query_params = {
      // The type of the profile from which the flamegraph is being generated
      // Always 'graph' for Java heap graphs.
      "'graph'",
      // Heapdump timestamp
      std::to_string(ts),
      // Timestamp constraints: not relevant and always null for Java heap
      // graphs.
      "NULL",
      // The upid of the heap graph sample
      std::to_string(upid),
      // The upid group: not relevant and always null for Java heap graphs
      "NULL",
      // A regex for focusing on a particular node in the heapgraph
      "NULL"};

  query += base::Join(query_params, ", ");
  query += ")";

  return query;
}

bool WriteAllocations(
    GProfileBuilder* builder,
    const std::unordered_map<int64_t, std::vector<int64_t>>& view_values) {
  for (const auto& [id, values] : view_values) {
    protozero::PackedVarInt sample_values;
    for (const int64_t value : values) {
      sample_values.Append(value);
    }
    if (!builder->AddSample(sample_values, id)) {
      return false;
    }
  }
  return true;
}

// Extracts and interns the unique locations from the heap dump SQL tables.
//
// It uses experimental_flamegraph table to get normalized representation of
// the heap graph as a tree, which always takes the shortest path to the root.
//
// Approach:
//   * First we iterate over all heap dump flamegraph rows and create a map
//     of flamegraph item id -> flamegraph item parent_id, each flamechart
//     item is converted to a Location where we populate Function name using
//     the name of the class (as opposed to using actual call function as
//     allocation call stack is not available for java heap dumps).
//     Also populate view_values straightaway here to not iterate over the data
//     again in the future.
//   * For each location we iterate over all its parents until we find
//     the root and use this list of locations as a 'callstack' (which is
//     actually list of class names)
LocationTracker PreprocessLocationsForJavaHeap(
    trace_processor::TraceProcessor* tp,
    trace_processor::StringPool* interner,
    const std::vector<View>& views,
    std::unordered_map<int64_t, std::vector<int64_t>>& view_values_out,
    uint64_t upid,
    uint64_t ts) {
  LocationTracker tracker;

  std::string columns;
  for (const auto& view : views) {
    columns += std::string(view.query) + ", ";
  }

  const auto data_columns_count = static_cast<uint32_t>(views.size());
  columns += "id, parent_id, name";

  const std::string query = CreateHeapDumpFlameGraphQuery(columns, upid, ts);
  Iterator it = tp->ExecuteQuery(query);

  // flamegraph id -> flamegraph parent_id
  std::unordered_map<int64_t, int64_t> parents;
  // flamegraph id -> interned location id
  std::unordered_map<int64_t, int64_t> interned_ids;

  // Create locations
  while (it.Next()) {
    const int64_t id = it.Get(data_columns_count).AsLong();

    const int64_t parent_id = it.Get(data_columns_count + 1).is_null()
                                  ? -1
                                  : it.Get(data_columns_count + 1).AsLong();

    auto name = it.Get(data_columns_count + 2).is_null()
                    ? ""
                    : it.Get(data_columns_count + 2).AsString();

    parents.emplace(id, parent_id);

    StringId func_name_id = interner->InternString(name);
    Function func(func_name_id, StringId::Null(), StringId::Null());
    auto interned_function_id = tracker.InternFunction(func);

    Location loc(/*map=*/0, /*func=*/interned_function_id, /*inlines=*/{});
    auto interned_location_id = tracker.InternLocation(std::move(loc));

    interned_ids.emplace(id, interned_location_id);

    std::vector<int64_t> view_values_vector;
    for (uint32_t i = 0; i < views.size(); ++i) {
      view_values_vector.push_back(it.Get(i).AsLong());
    }

    view_values_out.emplace(id, view_values_vector);
  }

  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
    return {};
  }

  // Iterate over all known locations again and build root-first paths
  // for every location
  for (auto& parent : parents) {
    std::vector<int64_t> path;

    int64_t current_parent_id = parent.first;
    while (current_parent_id != -1) {
      auto id_it = interned_ids.find(current_parent_id);
      PERFETTO_CHECK(id_it != interned_ids.end());

      auto parent_location_id = id_it->second;
      path.push_back(parent_location_id);

      // Find parent of the parent
      auto parent_id_it = parents.find(current_parent_id);
      PERFETTO_CHECK(parent_id_it != parents.end());

      current_parent_id = parent_id_it->second;
    }

    // Reverse to make it root-first list
    std::reverse(path.begin(), path.end());

    tracker.MaybeSetCallsiteLocations(parent.first, path);
  }

  return tracker;
}

bool TraceToHeapPprof(trace_processor::TraceProcessor* tp,
                      std::vector<SerializedProfile>* output,
                      uint64_t target_pid,
                      const std::vector<uint64_t>& target_timestamps) {
  trace_processor::StringPool interner;

  // Find all heap graphs available in the trace and iterate over them
  Iterator it = tp->ExecuteQuery(
      "select distinct hgo.graph_sample_ts, hgo.upid, p.pid from "
      "heap_graph_object hgo join process p using (upid)");

  while (it.Next()) {
    uint64_t ts = static_cast<uint64_t>(it.Get(0).AsLong());
    uint64_t upid = static_cast<uint64_t>(it.Get(1).AsLong());
    uint64_t profile_pid = static_cast<uint64_t>(it.Get(2).AsLong());

    if ((target_pid > 0 && profile_pid != target_pid) ||
        (!target_timestamps.empty() &&
         std::find(target_timestamps.begin(), target_timestamps.end(), ts) ==
             target_timestamps.end())) {
      continue;
    }

    // flamegraph id -> view values
    std::unordered_map<int64_t, std::vector<int64_t>> view_values;

    std::vector<View> views;
    views.assign(std::begin(kJavaAllocationViews),
                 std::end(kJavaAllocationViews));

    LocationTracker locations = PreprocessLocationsForJavaHeap(
        tp, &interner, views, view_values, upid, ts);

    GProfileBuilder builder(locations, &interner);

    std::vector<std::pair<std::string, std::string>> sample_types;
    for (const auto& view : views) {
      sample_types.emplace_back(view.type, view.unit);
    }
    builder.WriteSampleTypes(sample_types);

    std::string profile_proto;
    if (WriteAllocations(&builder, view_values)) {
      profile_proto = builder.CompleteProfile(tp, /*write_mappings=*/false);
    }

    output->emplace_back(SerializedProfile{ProfileType::kJavaHeapProfile,
                                           profile_pid,
                                           std::move(profile_proto), ""});
  }

  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
    return false;
  }

  return true;
}
}  // namespace java_heap_profile

namespace perf_profile {
struct ProcessInfo {
  uint64_t pid;
  std::vector<uint64_t> utids;
};

// Returns a map of upid -> {pid, utids[]} for sampled processes.
static std::map<uint64_t, ProcessInfo> GetProcessMap(
    trace_processor::TraceProcessor* tp) {
  Iterator it = tp->ExecuteQuery(
      "select distinct process.upid, process.pid, thread.utid from perf_sample "
      "join thread using (utid) join process using (upid) where callsite_id is "
      "not null order by process.upid asc");
  std::map<uint64_t, ProcessInfo> process_map;
  while (it.Next()) {
    uint64_t upid = static_cast<uint64_t>(it.Get(0).AsLong());
    uint64_t pid = static_cast<uint64_t>(it.Get(1).AsLong());
    uint64_t utid = static_cast<uint64_t>(it.Get(2).AsLong());
    process_map[upid].pid = pid;
    process_map[upid].utids.push_back(utid);
  }
  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
    return {};
  }
  return process_map;
}

static void LogTracePerfEventIssues(trace_processor::TraceProcessor* tp) {
  std::optional<int64_t> stat = GetStatsEntry(tp, "perf_samples_skipped");
  if (!stat.has_value()) {
    PERFETTO_DFATAL_OR_ELOG("Failed to look up perf_samples_skipped stat");
  } else if (stat.value() > 0) {
    PERFETTO_ELOG(
        "Warning: the trace recorded %" PRIi64
        " skipped samples, which otherwise matched the tracing config. This "
        "would cause a process to be completely absent from the trace, but "
        "does *not* imply data loss in any of the output profiles.",
        stat.value());
  }

  stat = GetStatsEntry(tp, "perf_samples_skipped_dataloss");
  if (!stat.has_value()) {
    PERFETTO_DFATAL_OR_ELOG(
        "Failed to look up perf_samples_skipped_dataloss stat");
  } else if (stat.value() > 0) {
    PERFETTO_ELOG("DATA LOSS: the trace recorded %" PRIi64
                  " lost perf samples (within traced_perf). This means that "
                  "the trace is missing information, but it is not known "
                  "which profile that affected.",
                  stat.value());
  }

  // Check if any per-cpu ringbuffers encountered dataloss (as recorded by the
  // kernel).
  Iterator it = tp->ExecuteQuery(
      "select idx, value from stats where name == 'perf_cpu_lost_records' and "
      "value > 0 order by idx asc");
  while (it.Next()) {
    PERFETTO_ELOG(
        "DATA LOSS: during the trace, the per-cpu kernel ring buffer for cpu "
        "%" PRIi64 " recorded %" PRIi64
        " lost samples. This means that the trace is missing information, "
        "but it is not known which profile that affected.",
        static_cast<int64_t>(it.Get(0).AsLong()),
        static_cast<int64_t>(it.Get(1).AsLong()));
  }
  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
  }
}

// TODO(rsavitski): decide whether errors in |AddSample| should result in an
// empty profile (and/or whether they should make the overall conversion
// unsuccessful). Furthermore, clarify the return value's semantics for both
// perf and heap profiles.
static bool TraceToPerfPprof(trace_processor::TraceProcessor* tp,
                             std::vector<SerializedProfile>* output,
                             bool annotate_frames,
                             uint64_t target_pid) {
  trace_processor::StringPool interner;
  LocationTracker locations =
      PreprocessLocations(tp, &interner, annotate_frames);

  LogTracePerfEventIssues(tp);

  // Aggregate samples by upid when building profiles.
  std::map<uint64_t, ProcessInfo> process_map = GetProcessMap(tp);
  for (const auto& p : process_map) {
    const ProcessInfo& process = p.second;

    if (target_pid != 0 && process.pid != target_pid)
      continue;

    GProfileBuilder builder(locations, &interner);
    builder.WriteSampleTypes({{"samples", "count"}});

    std::string query = "select callsite_id from perf_sample where utid in (" +
                        AsCsvString(process.utids) +
                        ") and callsite_id is not null order by ts asc;";

    protozero::PackedVarInt single_count_value;
    single_count_value.Append(1);

    Iterator it = tp->ExecuteQuery(query);
    while (it.Next()) {
      int64_t callsite_id = static_cast<int64_t>(it.Get(0).AsLong());
      builder.AddSample(single_count_value, callsite_id);
    }
    if (!it.Status().ok()) {
      PERFETTO_DFATAL_OR_ELOG("Failed to iterate over samples: %s",
                              it.Status().c_message());
      return false;
    }

    std::string profile_proto = builder.CompleteProfile(tp);
    output->emplace_back(SerializedProfile{
        ProfileType::kPerfProfile, process.pid, std::move(profile_proto), ""});
  }
  return true;
}
}  // namespace perf_profile
}  // namespace

bool TraceToPprof(trace_processor::TraceProcessor* tp,
                  std::vector<SerializedProfile>* output,
                  ConversionMode mode,
                  uint64_t flags,
                  uint64_t pid,
                  const std::vector<uint64_t>& timestamps) {
  bool annotate_frames =
      flags & static_cast<uint64_t>(ConversionFlags::kAnnotateFrames);
  switch (mode) {
    case (ConversionMode::kHeapProfile):
      return heap_profile::TraceToHeapPprof(tp, output, annotate_frames, pid,
                                            timestamps);
    case (ConversionMode::kPerfProfile):
      return perf_profile::TraceToPerfPprof(tp, output, annotate_frames, pid);
    case (ConversionMode::kJavaHeapProfile):
      return java_heap_profile::TraceToHeapPprof(tp, output, pid, timestamps);
  }
  PERFETTO_FATAL("unknown conversion option");  // for gcc
}

}  // namespace trace_to_text
}  // namespace perfetto
