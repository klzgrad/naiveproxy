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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACE_DIAGNOSTICS_TRACKER_HELPER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACE_DIAGNOSTICS_TRACKER_HELPER_H_

#include <cstdint>
#include <optional>
#include <utility>

#include "perfetto/ext/base/string_view.h"

#include "protos/perfetto/config/data_source_config.pbzero.h"
#include "protos/perfetto/config/ftrace/ftrace_config.pbzero.h"
#include "protos/perfetto/config/trace_config.pbzero.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Shared machinery used by the trace-config diagnostic rules (which live in
// trace_diagnostics_tracker.cc). Keeps the rules terse: they iterate ftrace
// configs, derive values from the (trace, machine) context, and emit
// diagnostics through this helper, so the rule file stays focused on the
// detection logic itself.
class TraceDiagnosticsHelper {
 public:
  using FtraceConfigDecoder = protos::pbzero::FtraceConfig::Decoder;

  explicit TraceDiagnosticsHelper(TraceProcessorContext* context)
      : context_(context) {}

  // Emits one diagnostic row, implicitly scoped to the current (trace,
  // machine). `key` is a stable identifier, `title` a short human-friendly
  // label, and `remediation` a suggested fix for the problem.
  void AddTraceDiagnostic(base::StringView key,
                          base::StringView title,
                          base::StringView description,
                          base::StringView remediation,
                          double confidence);

  // Returns the moment tracing started, expressed as nanoseconds since boot.
  // Returns nullopt if the trace has no tracing_started_ns metadata or the
  // clock graph can't bridge trace-time to BOOTTIME.
  std::optional<int64_t> TracingStartedSinceBootNs() const;

  // Returns true if the trace recorded any ftrace per-cpu data loss (overrun,
  // commit overrun, or an explicit has_data_loss marker) for this (trace,
  // machine). Rules use this as strong evidence a buffer/bandwidth problem
  // actually bit.
  bool HasFtraceCpuDataLoss() const;

  // Returns true if any traced_* stat of severity kDataLoss is non-zero for
  // this (trace, machine), i.e. the tracing service's own buffers lost data.
  bool HasTracedDataLoss() const;

  // Returns true if any heapprofd_* stat of severity kError is non-zero for
  // this (trace, machine).
  bool HasHeapprofdErrorStats() const;

  // Returns true if the trace looks like it came from an Android `user`
  // (production) build, derived from the build type token in the
  // android_build_fingerprint metadata. Returns false if the fingerprint is
  // absent or the build type is anything else (userdebug/eng) or unparseable.
  bool IsAndroidUserBuild() const;

  // True if the display.video importer emitted any frames.
  bool HasVideoFrames() const;

  // Returns true if any android_video_* stat of severity kError/kDataLoss is
  // non-zero for this (trace, machine), i.e. the producer reported a failure
  // (codec error, no encoder, size cap, ...) rather than silently emitting
  // nothing.
  bool HasVideoErrorStats() const;

  // Invokes `fn` with the decoded DataSourceConfig of every data source in the
  // config.
  template <typename Fn>
  void ForEachDataSourceConfig(
      const protos::pbzero::TraceConfig::Decoder& config,
      Fn&& fn) {
    for (auto ds = config.data_sources(); ds; ++ds) {
      protos::pbzero::TraceConfig::DataSource::Decoder ds_dec(*ds);
      if (!ds_dec.has_config())
        continue;
      protos::pbzero::DataSourceConfig::Decoder ds_cfg(ds_dec.config());
      fn(ds_cfg);
    }
  }

  // Invokes `fn` with the decoded FtraceConfig of *every* ftrace data source in
  // the config (a trace may legitimately have more than one), so rules bark on
  // any offending data source rather than just the first.
  template <typename Fn>
  void ForEachFtraceConfig(const protos::pbzero::TraceConfig::Decoder& config,
                           Fn&& fn) {
    ForEachDataSourceConfig(
        config, [&](const protos::pbzero::DataSourceConfig::Decoder& ds_cfg) {
          if (!ds_cfg.has_ftrace_config())
            return;
          FtraceConfigDecoder ftrace(ds_cfg.ftrace_config());
          fn(ftrace);
        });
  }

 private:
  TraceProcessorContext* const context_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACE_DIAGNOSTICS_TRACKER_HELPER_H_
