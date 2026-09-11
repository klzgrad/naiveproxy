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
#include "perfetto/base/status.h"
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

base::Status TraceToBundle(const std::string& input_file_path,
                           const std::string& output_file_path,
                           const BundleContext& context) {
  auto tp = trace_processor::TraceProcessor::CreateInstance({});

  // Report reading progress to stderr, like the interactive shell does, so
  // long-running bundles don't look frozen.
  double loaded_mb = 0;
  auto status = trace_processor::ReadTrace(
      tp.get(), input_file_path.c_str(), [&loaded_mb](uint64_t parsed_size) {
        loaded_mb = static_cast<double>(parsed_size) / 1E6;
        fprintf(stderr, "\rReading trace: %.2f MB", loaded_mb);
      });
  if (!status.ok()) {
    fprintf(stderr, "\n");
    return base::ErrStatus("failed to read trace: %s", status.c_message());
  }
  fprintf(stderr, "\rRead trace: %.2f MB.\n", loaded_mb);

  // Add original trace file directly (memory efficient). If the output path
  // cannot be opened, TarWriter fails gracefully and this propagates a
  // descriptive error instead of crashing.
  fprintf(stderr, "Adding trace to bundle...\n");
  trace_processor::util::TarWriter tar(output_file_path);
  auto add_trace_status =
      tar.AddFileFromPath("trace.perfetto", input_file_path);
  if (!add_trace_status.ok()) {
    return base::ErrStatus("failed to create bundle: %s",
                           add_trace_status.c_message());
  }

  // Build enrichment configuration from context.
  trace_processor::util::EnrichmentConfig enrich_config;
  enrich_config.symbol_paths = context.symbol_paths;
  enrich_config.no_auto_symbol_paths = context.no_auto_symbol_paths;
  enrich_config.no_auto_proguard_maps = context.no_auto_proguard_maps;
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
  fprintf(stderr, "Symbolizing and deobfuscating...\n");
  auto enrich_result =
      trace_processor::util::EnrichTrace(tp.get(), enrich_config);
  fprintf(stderr, "Enrichment done.\n");

  // Add symbols if available.
  if (!enrich_result.native_symbols.empty()) {
    auto add_status = tar.AddFile("symbols.pb", enrich_result.native_symbols);
    if (!add_status.ok()) {
      return base::ErrStatus("failed to add symbols to bundle: %s",
                             add_status.c_message());
    }
  }

  // Add deobfuscation data if available.
  if (!enrich_result.deobfuscation_data.empty()) {
    auto add_status =
        tar.AddFile("deobfuscation.pb", enrich_result.deobfuscation_data);
    if (!add_status.ok()) {
      return base::ErrStatus("failed to add deobfuscation data to bundle: %s",
                             add_status.c_message());
    }
  }

  // Log any issues to stderr (without PERFETTO_LOG noise).
  if (!enrich_result.details.empty()) {
    fprintf(stderr, "%s", enrich_result.details.c_str());
  }

  // Explicitly-provided resources that fail to load are the only hard
  // errors: the user asked for them, so silently producing a bundle without
  // them would hide the problem. Everything else (no matching symbols found,
  // kernel addresses that cannot be symbolized offline, ...) is advisory and
  // has already been printed above; the bundle is still produced with the
  // trace and whatever enrichment was possible.
  if (enrich_result.error ==
      trace_processor::util::EnrichmentError::kExplicitMapsFailed) {
    return base::ErrStatus(
        "bundle: one or more explicitly-provided ProGuard/R8 maps could not "
        "be read; see the details above. Refusing to produce a bundle without "
        "the requested deobfuscation data.");
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_to_text
