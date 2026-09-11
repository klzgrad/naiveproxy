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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACE_DIAGNOSTICS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACE_DIAGNOSTICS_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace perfetto::trace_processor {

class TraceProcessorContext;
class TraceDiagnosticsHelper;

// Detects poorly-written trace configs and other problems that can affect trace
// quality and records them in the `trace_diagnostics` table. One instance per
// (trace, machine) context: diagnostics are implicitly scoped to the current
// trace and machine.
//
// The detection rules themselves live in trace_diagnostics_tracker.cc; the
// shared machinery they use (emitting diagnostics, deriving values from the
// context, iterating ftrace configs) lives in TraceDiagnosticsHelper.
//
// Lifecycle:
//   * SetTraceConfig() is called at parse time (from MetadataModule) with the
//     raw TraceConfig bytes; they are copied and stashed.
//   * RunRules() is called once at finalization from
//     TraceProcessorImpl::NotifyEndOfFile; it re-decodes the config and runs
//     every detection rule.
class TraceDiagnosticsTracker {
 public:
  explicit TraceDiagnosticsTracker(TraceProcessorContext* context);
  ~TraceDiagnosticsTracker();

  // Stashes a copy of the raw serialized TraceConfig for later analysis.
  void SetTraceConfig(const uint8_t* data, const uint8_t* end);

  // Runs every detection rule against the captured config. No-op if no config
  // was captured.
  void RunRules();

 private:
  std::unique_ptr<TraceDiagnosticsHelper> helper_;
  std::vector<uint8_t> raw_config_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACE_DIAGNOSTICS_TRACKER_H_
