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

#include "src/trace_processor/util/symbolizer/symbolize_database.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/util/build_id.h"
#include "src/trace_processor/util/symbolizer/breakpad_symbolizer.h"
#include "src/trace_processor/util/symbolizer/local_symbolizer.h"
#include "src/trace_processor/util/symbolizer/symbolizer.h"

#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM) && \
    !PERFETTO_BUILDFLAG(PERFETTO_CHROMIUM_BUILD)
#include <unistd.h>  // For isatty()
#endif

namespace perfetto::profiling {

namespace {
using trace_processor::Iterator;

constexpr const char* kQueryUnsymbolized =
    R"(
      select
        spm.name,
        spm.build_id,
        spf.rel_pc,
        spm.load_bias
      from __intrinsic_stack_profile_frame spf
      join __intrinsic_stack_profile_mapping spm on spf.mapping = spm.id
      where (
          spm.build_id != ''
          -- The [[] is *not* a typo: that's how you escape [ inside a glob.
          or spm.name GLOB '[[]kernel.kallsyms]*'
        )
        and spf.symbol_set_id IS NULL
    )";

// Query to get mappings with empty build IDs and their frame counts.
// These frames cannot be symbolized because we cannot look up symbols without
// a build ID.
constexpr const char* kQueryMappingsWithoutBuildId =
    R"(
      select iif(spm.name = '', '[empty mapping name]', spm.name), count(*)
      from __intrinsic_stack_profile_frame spf
      join __intrinsic_stack_profile_mapping spm on spf.mapping = spm.id
      where spm.build_id = ''
        and spm.name NOT GLOB '[[]kernel.kallsyms]*'
        and spf.symbol_set_id IS NULL
      group by spm.name
    )";

struct UnsymbolizedMapping {
  std::string name;
  std::string build_id;
  uint64_t load_bias;
  bool operator<(const UnsymbolizedMapping& o) const {
    return std::tie(name, build_id, load_bias) <
           std::tie(o.name, o.build_id, o.load_bias);
  }
};

std::map<UnsymbolizedMapping, std::vector<uint64_t>> GetUnsymbolizedFrames(
    trace_processor::TraceProcessor* tp) {
  std::map<UnsymbolizedMapping, std::vector<uint64_t>> res;
  Iterator it = tp->ExecuteQuery(kQueryUnsymbolized);
  while (it.Next()) {
    int64_t load_bias = it.Get(3).AsLong();
    PERFETTO_CHECK(load_bias >= 0);
    trace_processor::BuildId build_id =
        trace_processor::BuildId::FromHex(it.Get(1).AsString());
    UnsymbolizedMapping unsymbolized_mapping{
        it.Get(0).AsString(), build_id.raw(), static_cast<uint64_t>(load_bias)};
    int64_t rel_pc = it.Get(2).AsLong();
    res[unsymbolized_mapping].emplace_back(rel_pc);
  }
  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Invalid iterator: %s",
                            it.Status().message().c_str());
    return {};
  }
  return res;
}

std::vector<std::pair<std::string, uint32_t>> GetMappingsWithoutBuildId(
    trace_processor::TraceProcessor* tp) {
  std::vector<std::pair<std::string, uint32_t>> result;
  Iterator it = tp->ExecuteQuery(kQueryMappingsWithoutBuildId);
  while (it.Next()) {
    std::string name = it.Get(0).AsString();
    int64_t count = it.Get(1).AsLong();
    PERFETTO_CHECK(count >= 0);
    result.emplace_back(std::move(name), static_cast<uint32_t>(count));
  }
  if (!it.Status().ok()) {
    PERFETTO_DFATAL_OR_ELOG("Failed to query mappings without build ID: %s",
                            it.Status().message().c_str());
  }
  return result;
}

std::optional<std::string> GetOsRelease(trace_processor::TraceProcessor* tp) {
  Iterator it = tp->ExecuteQuery(
      "select str_value from metadata where name = 'system_release'");
  if (it.Next() && it.ColumnCount() > 0 &&
      it.Get(0).type == trace_processor::SqlValue::kString) {
    return it.Get(0).AsString();
  }
  return std::nullopt;
}

// Creates a local symbolizer for "index" mode.
std::unique_ptr<Symbolizer> CreateIndexSymbolizer(
    const SymbolizerConfig& config) {
  if (config.index_symbol_paths.empty() && config.symbol_files.empty()) {
    return nullptr;
  }
  return MaybeLocalSymbolizer(config.index_symbol_paths, config.symbol_files,
                              "index");
}

// Creates a local symbolizer for "find" mode.
std::unique_ptr<Symbolizer> CreateFindSymbolizer(
    const SymbolizerConfig& config) {
  if (config.find_symbol_paths.empty()) {
    return nullptr;
  }
  return MaybeLocalSymbolizer(config.find_symbol_paths, {}, "find");
}

struct SymbolizationOutput {
  std::string symbols_proto;
  std::vector<SuccessfulMapping> successful_mappings;
  std::vector<FailedMapping> failed_mappings;
};

SymbolizationOutput SymbolizeDatabaseWithSymbolizer(
    trace_processor::TraceProcessor* tp,
    Symbolizer* symbolizer) {
  PERFETTO_CHECK(symbolizer);
  auto unsymbolized = GetUnsymbolizedFrames(tp);
  Symbolizer::Environment env = {GetOsRelease(tp)};

  SymbolizationOutput output;
  for (const auto& [unsymbolized_mapping, rel_pcs] : unsymbolized) {
    uint32_t frame_count = static_cast<uint32_t>(rel_pcs.size());
    SymbolizeResult res = symbolizer->Symbolize(
        env, unsymbolized_mapping.name, unsymbolized_mapping.build_id,
        unsymbolized_mapping.load_bias, rel_pcs);
    if (res.frames.empty()) {
      // Record the failed mapping with all attempted paths.
      if (!res.attempts.empty()) {
        output.failed_mappings.push_back(
            {unsymbolized_mapping.name, unsymbolized_mapping.build_id,
             std::move(res.attempts), frame_count});
      }
      continue;
    }

    // Find the successful path from attempts (the one with kOk).
    std::string symbol_path;
    for (const auto& attempt : res.attempts) {
      if (attempt.error == SymbolPathError::kOk) {
        symbol_path = attempt.path;
        break;
      }
    }
    output.successful_mappings.push_back({unsymbolized_mapping.name,
                                          unsymbolized_mapping.build_id,
                                          symbol_path, frame_count});

    protozero::HeapBuffered<perfetto::protos::pbzero::Trace> trace;
    auto* packet = trace->add_packet();
    auto* module_symbols = packet->set_module_symbols();
    module_symbols->set_path(unsymbolized_mapping.name);
    module_symbols->set_build_id(unsymbolized_mapping.build_id);
    PERFETTO_DCHECK(res.frames.size() == rel_pcs.size());
    for (size_t i = 0; i < res.frames.size(); ++i) {
      auto* address_symbols = module_symbols->add_address_symbols();
      address_symbols->set_address(rel_pcs[i]);
      for (const SymbolizedFrame& frame : res.frames[i]) {
        auto* line = address_symbols->add_lines();
        line->set_function_name(frame.function_name);
        line->set_source_file_name(frame.file_name);
        line->set_line_number(frame.line);
      }
    }
    output.symbols_proto += trace.SerializeAsString();
  }
  return output;
}

// ANSI color codes for terminal output.
const char kReset[] = "\x1b[0m";
const char kRed[] = "\x1b[31m";
const char kYellow[] = "\x1b[33m";
const char kCyan[] = "\x1b[36m";

// Helper to wrap text in color codes if colorize is true.
std::string Colorize(bool colorize,
                     const char* color,
                     const std::string& text) {
  if (!colorize) {
    return text;
  }
  return std::string(color) + text + kReset;
}

const char* SymbolPathErrorToString(SymbolPathError error) {
  switch (error) {
    case SymbolPathError::kOk:
      return "ok";
    case SymbolPathError::kFileNotFound:
      return "file not found";
    case SymbolPathError::kBuildIdMismatch:
      return "build ID mismatch";
    case SymbolPathError::kParseError:
      return "failed to parse";
    case SymbolPathError::kBuildIdNotInIndex:
      return "no matching build ID";
  }
  return "unknown";
}

std::string Plural(size_t count, const char* singular, const char* plural) {
  return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

// Formats a hint with optional coloring.
std::string FormatHint(bool colorize, const std::string& text) {
  return Colorize(colorize, kCyan, "hint: " + text);
}

// Hint text for symbol path issues.
std::string SymbolPathHint(bool colorize) {
  return FormatHint(
             colorize,
             "use --symbol-paths to specify symbol files or directories") +
         "\n";
}

// Hint text for missing build IDs.
std::string MissingBuildIdHint(bool colorize) {
  return FormatHint(colorize,
                    "rebuild binaries with build IDs (linker flag "
                    "-Wl,--build-id) and re-record the trace") +
         "\n";
}

// Formats kernel debug symbol installation hint.
void FormatKernelHint(bool colorize, std::string* out, const char* indent) {
  *out += indent;
  *out +=
      FormatHint(colorize, "install kernel debug symbols (vmlinux):") + "\n";
  *out += indent;
  *out +=
      "  Linux (Debian/Ubuntu): sudo apt install "
      "linux-image-$(uname -r)-dbg\n";
  *out += indent;
  *out += "  Linux (Fedora): sudo dnf debuginfo-install kernel\n";
  *out += indent;
  *out += "  Android: obtain vmlinux from your kernel build tree\n";
}

void FormatSuccessfulMappings(const std::vector<SuccessfulMapping>& mappings,
                              std::string* out) {
  uint32_t frame_count = 0;
  for (const auto& mapping : mappings) {
    frame_count += mapping.frame_count;
  }
  if (frame_count == 0) {
    return;
  }
  *out += "\n  Symbolized " + Plural(frame_count, "frame", "frames") +
          " from " + Plural(mappings.size(), "mapping", "mappings") + ":\n";
  for (const auto& mapping : mappings) {
    *out += "    " + mapping.mapping_name + " (" +
            Plural(mapping.frame_count, "frame", "frames") + ")";
    if (!mapping.symbol_path.empty()) {
      *out += " -> " + mapping.symbol_path;
    }
    *out += "\n";
  }
}

bool IsKernelMapping(const std::string& name) {
  return base::StartsWith(name, "[kernel.kallsyms]");
}

void FormatFailedMappings(bool colorize,
                          const std::vector<FailedMapping>& mappings,
                          std::string* out) {
  uint32_t frame_count = 0;
  for (const auto& mapping : mappings) {
    frame_count += mapping.frame_count;
  }
  if (frame_count == 0) {
    return;
  }
  *out += "\n  No matching symbols in searched paths for " +
          Plural(mappings.size(), "mapping", "mappings") + " (" +
          Plural(frame_count, "frame", "frames") + "):\n";
  for (const auto& mapping : mappings) {
    bool is_kernel = IsKernelMapping(mapping.mapping_name);
    *out += "    " + mapping.mapping_name + " (" +
            Plural(mapping.frame_count, "frame", "frames") + ")\n";
    if (!is_kernel) {
      *out += "      build ID: " + base::ToHex(mapping.build_id) + "\n";
    }
    if (!mapping.attempts.empty()) {
      *out += "      paths searched:\n";
      for (const auto& attempt : mapping.attempts) {
        *out += "        " + attempt.path;
        if (attempt.error != SymbolPathError::kOk) {
          *out +=
              " " +
              Colorize(colorize, kRed,
                       "(" +
                           std::string(SymbolPathErrorToString(attempt.error)) +
                           ")");
        }
        *out += "\n";
      }
    } else {
      *out += "      no paths were configured to search\n";
    }
    if (is_kernel) {
      FormatKernelHint(colorize, out, "      ");
    } else {
      *out += "      ";
      *out += SymbolPathHint(colorize);
    }
  }
}

void FormatSkippedMappings(
    bool colorize,
    const std::vector<std::pair<std::string, uint32_t>>& mappings,
    std::string* out) {
  uint32_t frame_count = 0;
  for (const auto& [name, count] : mappings) {
    frame_count += count;
  }
  if (frame_count == 0) {
    return;
  }
  *out += "\n  No build IDs in trace for " +
          Plural(mappings.size(), "mapping", "mappings") + " (" +
          Plural(frame_count, "frame", "frames") +
          "), symbol lookup requires build IDs:\n";
  for (const auto& [name, count] : mappings) {
    *out += "    " + name + " (" + Plural(count, "frame", "frames") + ")\n";
  }
  *out += "  ";
  *out += MissingBuildIdHint(colorize);
}

}  // namespace

SymbolizerResult SymbolizeDatabase(trace_processor::TraceProcessor* tp,
                                   const SymbolizerConfig& config) {
  SymbolizerResult result;

  // Get mappings and frame count for frames with empty build IDs.
  result.mappings_without_build_id = GetMappingsWithoutBuildId(tp);

  bool has_any_paths =
      !config.index_symbol_paths.empty() || !config.symbol_files.empty() ||
      !config.find_symbol_paths.empty() || !config.breakpad_paths.empty();
  if (!has_any_paths) {
    result.error = SymbolizerError::kSymbolizerNotAvailable;
    result.error_details = "No symbol paths or breakpad paths provided";
    return result;
  }

  // Track successful and failed mappings by (mapping_name, build_id).
  std::set<std::pair<std::string, std::string>> successful_mapping_keys;
  std::map<std::pair<std::string, std::string>, size_t> failed_mapping_index;

  auto collect_output = [&result, &successful_mapping_keys,
                         &failed_mapping_index](SymbolizationOutput output) {
    result.symbols += output.symbols_proto;
    for (auto& success : output.successful_mappings) {
      auto key = std::make_pair(success.mapping_name, success.build_id);
      successful_mapping_keys.insert(key);
      result.successful_mappings.push_back(std::move(success));
    }
    // Merge failed mappings - skip if already successful, merge attempts if
    // already failed.
    for (auto& failed : output.failed_mappings) {
      auto key = std::make_pair(failed.mapping_name, failed.build_id);
      // Skip if this mapping was already successfully symbolized.
      if (successful_mapping_keys.count(key)) {
        continue;
      }
      auto it = failed_mapping_index.find(key);
      if (it != failed_mapping_index.end()) {
        // Merge attempts into existing entry.
        FailedMapping& existing = result.failed_mappings[it->second];
        for (auto& attempt : failed.attempts) {
          existing.attempts.push_back(std::move(attempt));
        }
      } else {
        failed_mapping_index[key] = result.failed_mappings.size();
        result.failed_mappings.push_back(std::move(failed));
      }
    }
  };

  // Run "index" mode symbolizer if paths are provided.
  if (auto symbolizer = CreateIndexSymbolizer(config); symbolizer) {
    collect_output(SymbolizeDatabaseWithSymbolizer(tp, symbolizer.get()));
  }

  // Run "find" mode symbolizer if paths are provided.
  if (auto symbolizer = CreateFindSymbolizer(config); symbolizer) {
    collect_output(SymbolizeDatabaseWithSymbolizer(tp, symbolizer.get()));
  }

  // Run breakpad symbolizers for each breakpad path.
  for (const std::string& breakpad_path : config.breakpad_paths) {
    BreakpadSymbolizer symbolizer(breakpad_path);
    collect_output(SymbolizeDatabaseWithSymbolizer(tp, &symbolizer));
  }

  result.error = SymbolizerError::kOk;
  return result;
}

std::vector<std::string> GetPerfettoBinaryPath() {
  const char* root = getenv("PERFETTO_BINARY_PATH");
  if (root != nullptr) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    const char* delimiter = ";";
#else
    const char* delimiter = ":";
#endif
    return base::SplitString(root, delimiter);
  }
  return {};
}

std::string FormatSymbolizationSummary(const SymbolizerResult& result,
                                       bool verbose,
                                       bool colorize) {
  std::string summary;

  size_t failed_count = result.failed_mappings.size();
  size_t skipped_count = result.mappings_without_build_id.size();

  // Count total frames.
  uint32_t failed_frames = 0;
  for (const auto& mapping : result.failed_mappings) {
    failed_frames += mapping.frame_count;
  }
  uint32_t skipped_frames = 0;
  for (const auto& [name, count] : result.mappings_without_build_id) {
    skipped_frames += count;
  }
  uint32_t unsymbolized_frames = failed_frames + skipped_frames;

  // If everything succeeded, don't log anything.
  if (failed_count == 0 && skipped_count == 0) {
    return summary;
  }

  // Header showing the problem.
  summary += Colorize(colorize, kYellow,
                      Plural(unsymbolized_frames, "frame", "frames") +
                          " could not be symbolized") +
             " and will appear as \"unknown\".\n";

  if (!verbose) {
    // Non-verbose: show breakdown summary with hints nested under each.
    if (failed_frames > 0) {
      summary += "  - " + Plural(failed_frames, "frame", "frames") + " from " +
                 Plural(failed_count, "mapping", "mappings") +
                 ": no matching symbols in searched paths\n";

      // Add hints nested under "no matching symbols".
      bool has_kernel_failure = false;
      bool has_non_kernel_failure = false;
      for (const auto& mapping : result.failed_mappings) {
        if (IsKernelMapping(mapping.mapping_name)) {
          has_kernel_failure = true;
        } else {
          has_non_kernel_failure = true;
        }
      }
      if (has_non_kernel_failure) {
        summary += "    ";
        summary += SymbolPathHint(colorize);
      }
      if (has_kernel_failure) {
        FormatKernelHint(colorize, &summary, "    ");
      }
    }
    if (skipped_frames > 0) {
      summary += "  - " + Plural(skipped_frames, "frame", "frames") + " from " +
                 Plural(skipped_count, "mapping", "mappings") +
                 ": no build IDs in trace, symbol lookup requires build IDs\n";
      summary += "    ";
      summary += MissingBuildIdHint(colorize);
    }

    summary += "Use --verbose to see the full details.\n";
    return summary;
  }

  // Verbose output - show everything.
  FormatSuccessfulMappings(result.successful_mappings, &summary);
  FormatFailedMappings(colorize, result.failed_mappings, &summary);
  FormatSkippedMappings(colorize, result.mappings_without_build_id, &summary);

  return summary;
}

SymbolizerResult SymbolizeDatabaseAndLog(trace_processor::TraceProcessor* tp,
                                         const SymbolizerConfig& config,
                                         bool verbose) {
  SymbolizerResult result = SymbolizeDatabase(tp, config);
  bool colorize = false;
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM) && \
    !PERFETTO_BUILDFLAG(PERFETTO_CHROMIUM_BUILD)
  colorize = isatty(STDERR_FILENO);
#endif
  std::string summary = FormatSymbolizationSummary(result, verbose, colorize);
  if (!summary.empty()) {
    fprintf(stderr, "%s", summary.c_str());
  }
  return result;
}

}  // namespace perfetto::profiling
