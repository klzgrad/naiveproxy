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

#ifndef SRC_TRACECONV_TRACE_TO_BUNDLE_H_
#define SRC_TRACECONV_TRACE_TO_BUNDLE_H_

#include <string>
#include <vector>

namespace perfetto::trace_to_text {

// ProGuard/R8 mapping specification.
struct ProguardMapSpec {
  std::string package;  // Java package name
  std::string path;     // Path to mapping.txt
};

// Context structure for bundle configuration
struct BundleContext {
  // Additional paths to search for symbols (beyond automatic discovery)
  std::vector<std::string> symbol_paths;

  // ProGuard/R8 mapping files for Java deobfuscation
  std::vector<ProguardMapSpec> proguard_maps;

  // If true, disables automatic symbol path discovery
  bool no_auto_symbol_paths = false;

  // If true, output verbose details (all paths tried, etc.)
  bool verbose = false;

  // Value of ANDROID_PRODUCT_OUT for AOSP builds symbol discovery
  std::string android_product_out;

  // Home directory for ~/.debug symbol discovery
  std::string home_dir;

  // Working directory for Gradle project detection
  std::string working_dir;

  // Root directory for /usr/lib/debug symbol discovery
  std::string root_dir;
};

// Creates a bundle from the input trace with symbolization,
// deobfuscation, and potentially other enhancements. Outputs a TAR file
// containing everything needed for the trace to be self-contained.
// Returns 0 on success, non-zero on failure.
int TraceToBundle(const std::string& input_file_path,
                  const std::string& output_file_path,
                  const BundleContext& context);

}  // namespace perfetto::trace_to_text

#endif  // SRC_TRACECONV_TRACE_TO_BUNDLE_H_
