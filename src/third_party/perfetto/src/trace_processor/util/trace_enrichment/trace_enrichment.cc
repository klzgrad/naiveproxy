/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/util/trace_enrichment/trace_enrichment.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/util/deobfuscation/deobfuscator.h"
#include "src/trace_processor/util/symbolizer/symbolize_database.h"

namespace perfetto::trace_processor::util {

namespace {

// Returns binary paths from mappings that might contain embedded symbols.
std::vector<std::string> GetSymbolFilesFromMappings(TraceProcessor* tp) {
  std::vector<std::string> files;
  auto it = tp->ExecuteQuery(R"(
    SELECT DISTINCT name
    FROM stack_profile_mapping
    WHERE build_id != '' AND name != ''
  )");
  while (it.Next()) {
    std::string name = it.Get(0).AsString();
    if (!name.empty() && name[0] == '/') {
      files.push_back(name);
    }
  }
  return files;
}

// Adds path to result if it exists.
void AddIfExists(std::vector<std::string>& result, const std::string& path) {
  if (!path.empty() && base::FileExists(path)) {
    result.push_back(path);
  }
}

// Joins two path components, avoiding double slashes.
std::string JoinPath(const std::string& base, const std::string& suffix) {
  if (base.empty()) {
    return suffix;
  }
  if (base.back() == '/') {
    return base + suffix.substr(suffix[0] == '/' ? 1 : 0);
  }
  return base + suffix;
}

// Discovers ProGuard/R8 mapping files in an Android Gradle project structure.
// Scans app/build/outputs/mapping/{buildVariant}/mapping.txt for all variants.
std::vector<std::string> DiscoverGradleMappings(
    const std::string& working_dir) {
  std::vector<std::string> mappings;

  std::string mapping_base = working_dir + "/app/build/outputs/mapping";
  if (!base::FileExists(mapping_base)) {
    return mappings;
  }

  std::vector<std::string> variants;
  if (!base::ListDirectories(mapping_base, variants).ok()) {
    return mappings;
  }

  for (const auto& variant : variants) {
    std::string mapping_file = mapping_base + "/" + variant + "/mapping.txt";
    if (base::FileExists(mapping_file)) {
      mappings.push_back(mapping_file);
    }
  }

  return mappings;
}

// Discovers native symbol paths from well-known locations.
std::vector<std::string> DiscoverSymbolPaths(
    const std::string& android_product_out,
    const std::string& working_dir,
    const std::string& home_dir,
    const std::string& root_dir) {
  std::vector<std::string> paths;

  // Default system debug directories.
  if (!root_dir.empty()) {
    AddIfExists(paths, JoinPath(root_dir, "/usr/lib/debug"));
  }
  if (!home_dir.empty()) {
    AddIfExists(paths, JoinPath(home_dir, "/.debug"));
  }

  // ANDROID_PRODUCT_OUT/symbols (AOSP builds).
  if (!android_product_out.empty()) {
    AddIfExists(paths, JoinPath(android_product_out, "/symbols"));
  }

  // Gradle project paths (only if working_dir is provided).
  if (!working_dir.empty()) {
    // Gradle CMake output.
    AddIfExists(paths, JoinPath(working_dir, "/app/build/intermediates/cmake"));

    // Gradle merged native libs.
    AddIfExists(paths, JoinPath(working_dir,
                                "/app/build/intermediates/merged_native_libs"));

    // Local .build-id cache.
    AddIfExists(paths, JoinPath(working_dir, "/.build-id"));
  }

  return paths;
}

}  // namespace

EnrichmentResult EnrichTrace(TraceProcessor* tp,
                             const EnrichmentConfig& config) {
  EnrichmentResult result;

  // === Native Symbolization ===
  {
    profiling::SymbolizerConfig sym_config;

    // Start with explicit paths from config.
    sym_config.index_symbol_paths = config.symbol_paths;

    // Add paths from PERFETTO_BINARY_PATH environment variable.
    for (auto& p : profiling::GetPerfettoBinaryPath()) {
      sym_config.index_symbol_paths.push_back(std::move(p));
    }

    // Add discovered paths if auto-discovery is enabled.
    if (!config.no_auto_symbol_paths) {
      for (auto& p :
           DiscoverSymbolPaths(config.android_product_out, config.working_dir,
                               config.home_dir, config.root_dir)) {
        sym_config.index_symbol_paths.push_back(std::move(p));
      }

      // Add binary paths from mappings (they might contain embedded symbols).
      sym_config.symbol_files = GetSymbolFilesFromMappings(tp);
    }

    // Also search the same paths for breakpad symbol files.
    sym_config.breakpad_paths = sym_config.index_symbol_paths;

    auto sym_result = profiling::SymbolizeDatabase(tp, sym_config);
    if (sym_result.error == profiling::SymbolizerError::kOk) {
      result.native_symbols = std::move(sym_result.symbols);
      std::string sym_summary = profiling::FormatSymbolizationSummary(
          sym_result, config.verbose, config.colorize);
      if (!sym_summary.empty()) {
        result.details += "Symbolization: " + sym_summary;
      }
    } else {
      result.details += "Symbolization: " + sym_result.error_details + "\n";
    }
  }

  // === Java Deobfuscation ===
  bool explicit_maps_failed = false;
  {
    // Collect all ProGuard maps: explicit ones first, then auto-discovered.
    size_t explicit_count = config.proguard_maps.size();

    std::vector<profiling::ProguardMap> maps;
    maps.reserve(config.proguard_maps.size());
    for (const auto& m : config.proguard_maps) {
      maps.push_back({m.package, m.path});
    }

    if (!config.no_auto_proguard_maps) {
      for (auto& m : profiling::GetPerfettoProguardMapPath()) {
        maps.push_back(std::move(m));
      }
      for (auto& filename : DiscoverGradleMappings(config.working_dir)) {
        maps.push_back({"", std::move(filename)});
      }
    }

    // Process all maps, tracking whether explicit ones succeeded.
    std::vector<std::string> failed_explicit_maps;
    for (size_t i = 0; i < maps.size(); ++i) {
      bool is_explicit = i < explicit_count;
      bool success = profiling::ReadProguardMapsToDeobfuscationPackets(
          {maps[i]}, [&result](const std::string& packet) {
            result.deobfuscation_data += packet;
          });
      if (!success && is_explicit) {
        explicit_maps_failed = true;
        failed_explicit_maps.push_back(maps[i].filename);
      }
    }

    // Add deobfuscation failures to details if any explicit maps failed.
    if (!failed_explicit_maps.empty()) {
      result.details += "Deobfuscation: failed to read ProGuard map(s):\n";
      for (const auto& path : failed_explicit_maps) {
        result.details += "  - " + path + "\n";
      }
    }
  }

  // Determine overall status.
  if (explicit_maps_failed) {
    result.error = EnrichmentError::kExplicitMapsFailed;
  } else if (result.native_symbols.empty() &&
             result.deobfuscation_data.empty()) {
    result.error = EnrichmentError::kAllFailed;
  } else {
    result.error = EnrichmentError::kOk;
  }

  return result;
}

}  // namespace perfetto::trace_processor::util
