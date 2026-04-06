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

#ifndef SRC_TRACE_PROCESSOR_UTIL_TRACE_ENRICHMENT_TRACE_ENRICHMENT_H_
#define SRC_TRACE_PROCESSOR_UTIL_TRACE_ENRICHMENT_TRACE_ENRICHMENT_H_

#include <string>
#include <vector>

namespace perfetto::trace_processor {
class TraceProcessor;
}

namespace perfetto::trace_processor::util {

// Configuration for trace enrichment.
// Users should provide explicit paths or set environment variables.
// If auto-discovery is enabled, well-known locations are also searched.
struct EnrichmentConfig {
  // Explicit paths to search for native symbols (highest priority).
  // These paths are also searched for breakpad symbol files.
  std::vector<std::string> symbol_paths;

  // Explicit ProGuard/R8 mapping file specifications.
  // Each entry is {package_name, path_to_mapping.txt}.
  // Package name can be empty if not known.
  struct ProguardMapSpec {
    std::string package;
    std::string path;
  };
  std::vector<ProguardMapSpec> proguard_maps;

  // If true, disables automatic path discovery for symbols.
  // This includes default system paths like /usr/lib/debug and ~/.debug.
  // PERFETTO_BINARY_PATH is always respected.
  bool no_auto_symbol_paths = false;

  // If true, disables automatic ProGuard map discovery.
  // PERFETTO_PROGUARD_MAP is always respected.
  bool no_auto_proguard_maps = false;

  // If true, output verbose details (all paths tried, etc.).
  // If false, output a concise summary with hint to use --verbose for failures.
  bool verbose = false;

  // If true, include ANSI color codes in the output.
  bool colorize = false;

  // Environment values for path discovery.
  // Must be provided by caller; if empty, related paths are not discovered.
  std::string android_product_out;
  std::string home_dir;
  std::string working_dir;
  std::string root_dir;
};

// Error codes for enrichment operations.
enum class EnrichmentError {
  kOk,
  kPartialSuccess,      // Some optional enrichment failed
  kExplicitMapsFailed,  // Explicitly provided ProGuard maps couldn't be read
  kSymbolizerNotAvailable,
  kDeobfuscationFailed,
  kAllFailed,
};

// Result of enrichment operation.
struct EnrichmentResult {
  EnrichmentError error = EnrichmentError::kOk;

  // Human-readable details about the operation.
  std::string details;

  // Serialized TracePacket protos containing native symbol data.
  // Ready to be appended to the trace or included in a bundle.
  std::string native_symbols;

  // Serialized TracePacket protos containing deobfuscation mappings.
  // Ready to be appended to the trace or included in a bundle.
  std::string deobfuscation_data;

  // Returns true if any enrichment data was produced.
  bool HasData() const {
    return !native_symbols.empty() || !deobfuscation_data.empty();
  }
};

// Performs all trace enrichment in one call.
//
// This function is the "one stop shop" for trace enrichment:
// 1. Discovers paths from well-known locations (unless disabled)
// 2. Performs native symbolization
// 3. Performs Java deobfuscation
//
// Path discovery includes:
// - PERFETTO_BINARY_PATH environment variable
// - ANDROID_PRODUCT_OUT/symbols (AOSP builds)
// - Gradle project paths (cmake, merged_native_libs, .build-id)
// - System debug paths (/usr/lib/debug, ~/.debug)
// - PERFETTO_PROGUARD_MAP environment variable
// - Gradle ProGuard mapping files
//
// Args:
//   tp: TraceProcessor instance with the trace already loaded
//   config: Configuration for enrichment (paths, flags)
//
// Returns:
//   EnrichmentResult containing error info and enriched data
EnrichmentResult EnrichTrace(TraceProcessor* tp,
                             const EnrichmentConfig& config);

}  // namespace perfetto::trace_processor::util

#endif  // SRC_TRACE_PROCESSOR_UTIL_TRACE_ENRICHMENT_TRACE_ENRICHMENT_H_
