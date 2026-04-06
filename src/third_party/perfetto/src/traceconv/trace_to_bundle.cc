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

#include "src/traceconv/trace_to_bundle.h"

#include <cstdio>
#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/read_trace.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/util/tar_writer.h"
#include "src/trace_processor/util/trace_enrichment/trace_enrichment.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM) && \
    !PERFETTO_BUILDFLAG(PERFETTO_CHROMIUM_BUILD)
#include <unistd.h>  // For isatty()
#endif

namespace perfetto::trace_to_text {

int TraceToBundle(const std::string& input_file_path,
                  const std::string& output_file_path,
                  const BundleContext& context) {
  auto tp = trace_processor::TraceProcessor::CreateInstance({});
  auto status = trace_processor::ReadTrace(tp.get(), input_file_path.c_str());
  if (!status.ok()) {
    PERFETTO_ELOG("Failed to read trace: %s", status.c_message());
    return 1;
  }

  // Add original trace file directly (memory efficient).
  trace_processor::util::TarWriter tar(output_file_path);
  auto add_trace_status =
      tar.AddFileFromPath("trace.perfetto", input_file_path);
  if (!add_trace_status.ok()) {
    PERFETTO_ELOG("Failed to add trace to TAR archive: %s",
                  add_trace_status.c_message());
    return 1;
  }

  // Build enrichment configuration from context.
  trace_processor::util::EnrichmentConfig enrich_config;
  enrich_config.symbol_paths = context.symbol_paths;
  enrich_config.no_auto_symbol_paths = context.no_auto_symbol_paths;
  enrich_config.verbose = context.verbose;
  enrich_config.android_product_out = context.android_product_out;
  enrich_config.home_dir = context.home_dir;
  enrich_config.working_dir = context.working_dir;
  enrich_config.root_dir = context.root_dir;
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM) && \
    !PERFETTO_BUILDFLAG(PERFETTO_CHROMIUM_BUILD)
  enrich_config.colorize = isatty(STDERR_FILENO);
#endif

  // Add explicit ProGuard maps from context.
  for (const auto& map_spec : context.proguard_maps) {
    enrich_config.proguard_maps.push_back({map_spec.package, map_spec.path});
  }

  // Perform trace enrichment (symbolization + deobfuscation).
  auto enrich_result =
      trace_processor::util::EnrichTrace(tp.get(), enrich_config);

  // Add symbols if available.
  if (!enrich_result.native_symbols.empty()) {
    auto add_status = tar.AddFile("symbols.pb", enrich_result.native_symbols);
    if (!add_status.ok()) {
      PERFETTO_ELOG("Failed to add symbols to TAR archive: %s",
                    add_status.c_message());
      return 1;
    }
  }

  // Add deobfuscation data if available.
  if (!enrich_result.deobfuscation_data.empty()) {
    auto add_status =
        tar.AddFile("deobfuscation.pb", enrich_result.deobfuscation_data);
    if (!add_status.ok()) {
      PERFETTO_ELOG("Failed to add deobfuscation data to TAR: %s",
                    add_status.c_message());
      return 1;
    }
  }

  // Log any issues to stderr (without PERFETTO_LOG noise).
  if (!enrich_result.details.empty()) {
    fprintf(stderr, "%s", enrich_result.details.c_str());
  }

  // Explicit user-provided paths must succeed.
  if (enrich_result.error ==
          trace_processor::util::EnrichmentError::kExplicitMapsFailed ||
      enrich_result.error ==
          trace_processor::util::EnrichmentError::kAllFailed) {
    return 1;
  }

  return 0;
}

}  // namespace perfetto::trace_to_text
