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

#include "src/trace_processor/importers/common/trace_diagnostics_tracker.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/trace_diagnostics_tracker_helper.h"

#include "protos/perfetto/config/data_source_config.pbzero.h"
#include "protos/perfetto/config/ftrace/ftrace_config.pbzero.h"
#include "protos/perfetto/config/profiling/heapprofd_config.pbzero.h"
#include "protos/perfetto/config/trace_config.pbzero.h"

namespace perfetto::trace_processor {
namespace {

using TraceConfigDecoder = protos::pbzero::TraceConfig::Decoder;
using FtraceConfigDecoder = protos::pbzero::FtraceConfig::Decoder;

// A detection rule reads the decoded config and, when a known-bad condition is
// met, emits a diagnostic via `helper`. All shared machinery (iterating ftrace
// configs, deriving context values, emitting rows) lives on the helper so these
// rules stay focused on the detection logic.
using RuleFn = void (*)(const TraceConfigDecoder& config,
                        TraceDiagnosticsHelper* helper);

// preserve_ftrace_buffer is set but tracing started long after boot, so
// the preserved kernel buffer may contain very old, misleading events.
void RulePreserveFtraceBufferLateStart(const TraceConfigDecoder& config,
                                       TraceDiagnosticsHelper* helper) {
  helper->ForEachFtraceConfig(config, [&](const FtraceConfigDecoder& ftrace) {
    if (!ftrace.preserve_ftrace_buffer())
      return;
    std::optional<int64_t> boot_ns = helper->TracingStartedSinceBootNs();
    if (!boot_ns.has_value())
      return;
    const int64_t kThresholdNs = int64_t(5) * 60 * 1000 * 1000 * 1000;  // 5m
    if (*boot_ns <= kThresholdNs)
      return;
    helper->AddTraceDiagnostic(
        "preserve_ftrace_buffer_late_start",
        "Unnecessary preserve_ftrace_buffer",
        "preserve_ftrace_buffer is set but tracing started more than a few "
        "minutes after boot; the preserved ftrace buffer may contain very old, "
        "misleading events.",
        "Unset preserve_ftrace_buffer, or only use it when starting tracing "
        "shortly after boot.",
        0.7);
  });
}

// Ftrace buffer_size_kb is small, risking buffer overruns and dropped
// events. Confidence scales with how small the buffer is, and jumps to 0.9 if
// the trace actually recorded ftrace data loss.
void RuleTinyFtraceBuffer(const TraceConfigDecoder& config,
                          TraceDiagnosticsHelper* helper) {
  helper->ForEachFtraceConfig(config, [&](const FtraceConfigDecoder& ftrace) {
    if (!ftrace.has_buffer_size_kb())
      return;
    uint32_t kb = ftrace.buffer_size_kb();
    if (kb == 0)
      return;  // 0 means "use the default", not a tiny buffer.

    constexpr uint32_t kOkKb = 1024;    // >= 1 MB is fine.
    constexpr uint32_t kSevereKb = 64;  // <= 64 KB is definitely too small.
    if (kb >= kOkKb)
      return;

    // 0.9 at/below 64 KB, ramping down linearly to 0.2 as it approaches 1 MB.
    double confidence;
    if (kb <= kSevereKb) {
      confidence = 0.9;
    } else {
      double t = static_cast<double>(kb - kSevereKb) / (kOkKb - kSevereKb);
      confidence = 0.9 + t * (0.2 - 0.9);
    }
    // Actual recorded data loss is strong evidence the buffer was too small.
    if (helper->HasFtraceCpuDataLoss())
      confidence = 0.9;

    std::string desc = "ftrace buffer_size_kb is only " + std::to_string(kb) +
                       " KB (< 1 MB); the kernel ftrace buffer may overrun and "
                       "drop events under load.";
    helper->AddTraceDiagnostic(
        "tiny_ftrace_buffer", "Ftrace buffer too small", base::StringView(desc),
        "Avoid setting buffer_size_kb explicitly; leave it unset to use the "
        "default, which is tuned for typical workloads.",
        confidence);
  });
}

// The ftrace drain bandwidth (buffer_size_kb / drain_period_ms) is too
// low, so a burst of events can overflow the buffer before it is drained.
// Confidence ramps up as the bandwidth drops towards 1 MB/s, and jumps to 0.9
// if the trace actually recorded ftrace data loss.
void RuleLowFtraceDrainBandwidth(const TraceConfigDecoder& config,
                                 TraceDiagnosticsHelper* helper) {
  helper->ForEachFtraceConfig(config, [&](const FtraceConfigDecoder& ftrace) {
    if (!ftrace.has_buffer_size_kb() || !ftrace.has_drain_period_ms())
      return;
    uint32_t kb = ftrace.buffer_size_kb();
    uint32_t period_ms = ftrace.drain_period_ms();
    if (kb == 0 || period_ms == 0)
      return;

    // Sustainable drain rate before the buffer overflows, in MB/s.
    double mb_per_s = (static_cast<double>(kb) / 1024.0) /
                      (static_cast<double>(period_ms) / 1000.0);
    constexpr double kOkMbPerS = 5.0;      // >= 5 MB/s is fine.
    constexpr double kSevereMbPerS = 1.0;  // <= 1 MB/s is definitely too low.
    if (mb_per_s >= kOkMbPerS)
      return;

    // ~0 just under 5 MB/s, ramping up linearly to 0.9 at/below 1 MB/s.
    double confidence;
    if (mb_per_s <= kSevereMbPerS) {
      confidence = 0.9;
    } else {
      double t = (kOkMbPerS - mb_per_s) / (kOkMbPerS - kSevereMbPerS);
      confidence = 0.9 * t;
    }
    if (helper->HasFtraceCpuDataLoss())
      confidence = 0.9;

    std::string desc =
        "ftrace drain bandwidth is low (buffer_size_kb=" + std::to_string(kb) +
        ", drain_period_ms=" + std::to_string(period_ms) +
        " gives under 5 MB/s); a burst of events can overflow the buffer "
        "before it is drained.";
    helper->AddTraceDiagnostic(
        "low_ftrace_drain_bandwidth", "Low ftrace drain bandwidth",
        base::StringView(desc),
        "Leave buffer_size_kb and drain_period_ms unset to use the defaults, "
        "or raise buffer_size_kb / lower drain_period_ms to increase the drain "
        "bandwidth above 5 MB/s.",
        confidence);
  });
}

// ftrace drain_period_ms is outside the sane range.
void RuleExtremeFtraceDrainPeriod(const TraceConfigDecoder& config,
                                  TraceDiagnosticsHelper* helper) {
  helper->ForEachFtraceConfig(config, [&](const FtraceConfigDecoder& ftrace) {
    if (!ftrace.has_drain_period_ms())
      return;
    uint32_t ms = ftrace.drain_period_ms();
    constexpr uint32_t kMinMs = 100;
    constexpr uint32_t kMaxMs = 60000;
    if (ms >= kMinMs && ms <= kMaxMs)
      return;
    std::string desc =
        "ftrace drain_period_ms is " + std::to_string(ms) +
        " ms, outside the sane range [100, 60000]; this can cause "
        "excessive CPU wakeups or buffer overruns.";
    helper->AddTraceDiagnostic(
        "extreme_ftrace_drain_period", "Extreme ftrace drain period",
        base::StringView(desc),
        "Set drain_period_ms within the advised range or, better, leave it "
        "unset to the default values.",
        0.6);
  });
}

// Syscall tracing is enabled but no syscall filter is set, which records
// every syscall and can produce huge, expensive traces.
void RuleSyscallsWithoutFilter(const TraceConfigDecoder& config,
                               TraceDiagnosticsHelper* helper) {
  helper->ForEachFtraceConfig(config, [&](const FtraceConfigDecoder& ftrace) {
    bool has_syscalls = false;
    for (auto it = ftrace.ftrace_events(); it; ++it) {
      std::string ev = it->as_std_string();
      if (ev == "raw_syscalls/sys_enter" || ev == "raw_syscalls/sys_exit") {
        has_syscalls = true;
        break;
      }
    }
    if (!has_syscalls)
      return;
    // The repeated-field iterator is truthy iff syscall_events has >= 1 entry.
    if (static_cast<bool>(ftrace.syscall_events()))
      return;

    // Baseline 0.3; recorded ftrace data loss strongly suggests the unfiltered
    // syscall firehose overwhelmed the buffer.
    double confidence = helper->HasFtraceCpuDataLoss() ? 0.9 : 0.3;

    helper->AddTraceDiagnostic(
        "syscalls_without_filter", "Unfiltered syscall tracing",
        "raw_syscalls (sys_enter/sys_exit) are enabled without any "
        "syscall_events filter; every syscall is recorded, which can produce "
        "huge, expensive traces.",
        "Restrict syscall_events to the specific syscalls you need (e.g. "
        "\"sys_read\", \"sys_write\") to cut trace size and overhead.",
        confidence);
  });
}

// An ftrace event whose payload is a kernel symbol address is enabled but
// symbolize_ksyms is not set, so those addresses can never be resolved to
// names. These symbols cannot be added after the fact.
void RuleEventsRequireSymbolizeKsyms(const TraceConfigDecoder& config,
                                     TraceDiagnosticsHelper* helper) {
  // Events whose useful fields are raw kernel symbol addresses.
  static constexpr const char* kEventsNeedingKsyms[] = {
      "workqueue/workqueue_execute_start",
      "sched/sched_blocked_reason",
  };
  helper->ForEachFtraceConfig(config, [&](const FtraceConfigDecoder& ftrace) {
    if (ftrace.symbolize_ksyms())
      return;

    // Function graph tracing is entirely useless without symbolization: every
    // frame is a raw kernel address. This is a hard misconfiguration (1.0).
    if (ftrace.enable_function_graph()) {
      helper->AddTraceDiagnostic(
          "function_graph_requires_symbolize_ksyms",
          "Function graph needs symbolize_ksyms",
          "enable_function_graph is set but symbolize_ksyms is not; function "
          "graph tracing records kernel symbol addresses that are unusable "
          "without symbolization and cannot be resolved after the fact.",
          "Set symbolize_ksyms: true in the ftrace config.", 1.0);
    }

    std::string offending;
    for (auto it = ftrace.ftrace_events(); it; ++it) {
      std::string ev = it->as_std_string();
      for (const char* need : kEventsNeedingKsyms) {
        if (ev == need) {
          offending = std::move(ev);
          break;
        }
      }
      if (!offending.empty())
        break;
    }
    if (offending.empty())
      return;

    std::string desc =
        "ftrace event " + offending +
        " is enabled but symbolize_ksyms is not set; its kernel symbol fields "
        "will be raw addresses that cannot be resolved after the fact.";
    helper->AddTraceDiagnostic(
        "events_require_symbolize_ksyms", "Events need symbolize_ksyms",
        base::StringView(desc),
        "Set symbolize_ksyms: true in the ftrace config.", 0.6);
  });
}

// A DISCARD buffer backs ftrace or track_event while the trace streams
// into a file periodically. DISCARD stops recording once the buffer first
// fills, so long/streaming traces silently truncate; these high-volume data
// sources should use the default RING_BUFFER mode. Restricted to ftrace and
// track_event to stay conservative: one-shot sources (package list, etc.) are
// legitimately fine with DISCARD.
void RuleDiscardBufferForStreaming(const TraceConfigDecoder& config,
                                   TraceDiagnosticsHelper* helper) {
  if (!config.write_into_file())
    return;

  // 604800000 ms = 7 days. This is used by Traceur to do implement
  // "write_into_file, but really this is a standard ring buffer".
  // Only actual periodic file writes are a problem.
  // an effectively one-shot final write (a huge period) is fine. An unset
  // period defaults to 5s, i.e. periodic, so it correctly falls below this.
  constexpr uint32_t kTraceurLongPeriod = 604800000;
  if (config.file_write_period_ms() >= kTraceurLongPeriod)
    return;

  // Record which target buffers use the DISCARD fill policy.
  std::vector<bool> buffer_is_discard;
  for (auto it = config.buffers(); it; ++it) {
    protos::pbzero::TraceConfig::BufferConfig::Decoder buffer(*it);
    buffer_is_discard.push_back(
        buffer.fill_policy() ==
        protos::pbzero::TraceConfig::BufferConfig::DISCARD);
  }

  for (auto ds = config.data_sources(); ds; ++ds) {
    protos::pbzero::TraceConfig::DataSource::Decoder ds_dec(*ds);
    if (!ds_dec.has_config())
      continue;
    protos::pbzero::DataSourceConfig::Decoder ds_cfg(ds_dec.config());
    std::string name = ds_cfg.name().ToStdString();
    if (name != "linux.ftrace" && name != "track_event")
      continue;
    uint32_t target = ds_cfg.target_buffer();
    if (target >= buffer_is_discard.size() || !buffer_is_discard[target])
      continue;

    std::string desc =
        "data source \"" + name +
        "\" targets a DISCARD buffer while the trace streams into a file; "
        "DISCARD stops recording once the buffer first fills, silently "
        "truncating long traces.";
    helper->AddTraceDiagnostic(
        "discard_buffer_for_streaming", "DISCARD buffer for streaming",
        base::StringView(desc),
        "Use the default RING_BUFFER fill_policy for buffers backing ftrace or "
        "track_event in long/streaming traces.",
        0.8);
  }
}

// Heapprofd sampling_interval_bytes is very small. Below ~100 KB it
// rarely improves accuracy while adding overhead. Baseline 0.3, bumped to 0.9
// if the trace recorded any heapprofd error.
void RuleHeapprofdSamplingIntervalTooLow(const TraceConfigDecoder& config,
                                         TraceDiagnosticsHelper* helper) {
  helper->ForEachDataSourceConfig(
      config, [&](const protos::pbzero::DataSourceConfig::Decoder& ds_cfg) {
        if (!ds_cfg.has_heapprofd_config())
          return;
        protos::pbzero::HeapprofdConfig::Decoder heapprofd(
            ds_cfg.heapprofd_config());
        if (!heapprofd.has_sampling_interval_bytes())
          return;
        uint64_t interval = heapprofd.sampling_interval_bytes();
        constexpr uint64_t k100Kb = 100 * 1024;
        if (interval == 0 || interval >= k100Kb)
          return;

        double confidence = 0.3;
        if (helper->HasHeapprofdErrorStats())
          confidence = 0.9;

        std::string desc =
            "heapprofd sampling_interval_bytes is " + std::to_string(interval) +
            " (< 100 KB); such a small sampling interval rarely improves "
            "accuracy and increases profiling overhead.";
        helper->AddTraceDiagnostic(
            "heapprofd_sampling_interval_too_low",
            "Heapprofd sampling interval too low", base::StringView(desc),
            "Increase sampling_interval_bytes to at least 100 KB; smaller "
            "intervals add overhead without meaningfully improving accuracy.",
            confidence);
      });
}

// android.display.video was configured but produced no frames, on a user
// build with no producer error — the most likely cause is that the
// debug.tracing_video_allowed system property was not enabled (it gates
// display-video capture on user builds and resets on reboot).
void RuleDisplayVideoNotEnabled(const TraceConfigDecoder& config,
                                TraceDiagnosticsHelper* helper) {
  bool has_display_video = false;
  helper->ForEachDataSourceConfig(
      config, [&](const protos::pbzero::DataSourceConfig::Decoder& ds_cfg) {
        if (ds_cfg.name().ToStdStringView() == "android.display.video")
          has_display_video = true;
      });
  if (!has_display_video)
    return;
  // userdebug/eng builds capture out of the box; only fire on user builds.
  if (!helper->IsAndroidUserBuild())
    return;
  if (helper->HasVideoFrames())
    return;
  // Producer reported a failure: video ran but failed for another reason.
  if (helper->HasVideoErrorStats())
    return;
  helper->AddTraceDiagnostic(
      "display_video_not_enabled", "Display video not captured",
      "The trace config requested android.display.video, but no frames were "
      "captured and the producer reported no errors. On user (production) "
      "builds display video is disabled until the debug.tracing_video_allowed "
      "system property is set; it resets on reboot.",
      "Enable it over ADB before recording, then record again (re-run after "
      "each reboot): adb shell setprop debug.tracing_video_allowed true",
      0.7);
}

constexpr RuleFn kRules[] = {
    &RulePreserveFtraceBufferLateStart,    //
    &RuleTinyFtraceBuffer,                 //
    &RuleLowFtraceDrainBandwidth,          //
    &RuleExtremeFtraceDrainPeriod,         //
    &RuleSyscallsWithoutFilter,            //
    &RuleEventsRequireSymbolizeKsyms,      //
    &RuleDiscardBufferForStreaming,        //
    &RuleHeapprofdSamplingIntervalTooLow,  //
    &RuleDisplayVideoNotEnabled,           //
};

}  // namespace

TraceDiagnosticsTracker::TraceDiagnosticsTracker(TraceProcessorContext* context)
    : helper_(std::make_unique<TraceDiagnosticsHelper>(context)) {}

TraceDiagnosticsTracker::~TraceDiagnosticsTracker() = default;

void TraceDiagnosticsTracker::SetTraceConfig(const uint8_t* data,
                                             const uint8_t* data_end) {
  raw_config_.assign(data, data_end);
}

void TraceDiagnosticsTracker::RunRules() {
  if (raw_config_.empty())
    return;
  TraceConfigDecoder config(raw_config_.data(), raw_config_.size());
  for (RuleFn rule : kRules) {
    rule(config, helper_.get());
  }
}

}  // namespace perfetto::trace_processor
