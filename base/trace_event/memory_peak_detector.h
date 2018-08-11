// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_MEMORY_PEAK_DETECTOR_H_
#define BASE_TRACE_EVENT_MEMORY_PEAK_DETECTOR_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/base_export.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"

namespace base {

class SequencedTaskRunner;

namespace trace_event {

struct MemoryDumpProviderInfo;

// Detects temporally local memory peaks. Peak detection is based on
// continuously querying memory usage using MemoryDumpprovider(s) that support
// fast polling (e.g., ProcessMetricsDumpProvider which under the hoods reads
// /proc/PID/statm on Linux) and using a combination of:
// - An static threshold (currently 1% of total system memory).
// - Sliding window stddev analysis.
// Design doc: https://goo.gl/0kOU4A .
// This class is NOT thread-safe, the caller has to ensure linearization of
// the calls to the public methods. In any case, the public methods do NOT have
// to be called from the |task_runner| on which the polling tasks run.
class BASE_EXPORT MemoryPeakDetector {
 public:
  using OnPeakDetectedCallback = RepeatingClosure;
  using DumpProvidersList = std::vector<scoped_refptr<MemoryDumpProviderInfo>>;
  using GetDumpProvidersFunction = RepeatingCallback<void(DumpProvidersList*)>;

  enum State {
    NOT_INITIALIZED = 0,  // Before Setup()
    DISABLED,             // Before Start() or after Stop().
    ENABLED,              // After Start() but no dump_providers_ are available.
    RUNNING  // After Start(). The PollMemoryAndDetectPeak() task is scheduled.
  };

  // Peak detector configuration, passed to Start().
  struct BASE_EXPORT Config {
    Config();
    Config(uint32_t polling_interval_ms,
           uint32_t min_time_between_peaks_ms,
           bool enable_verbose_poll_tracing);

    // The rate at which memory will be polled. Polls will happen on the task
    // runner passed to Setup().
    uint32_t polling_interval_ms;

    // Two consecutive peak detection callbacks will happen at least
    // |min_time_between_peaks_ms| apart from each other.
    uint32_t min_time_between_peaks_ms;

    // When enabled causes a TRACE_COUNTER event to be injected in the trace
    // for each poll (if tracing is enabled).
    bool enable_verbose_poll_tracing;
  };

  static MemoryPeakDetector* GetInstance();

  // Configures the peak detector, binding the polling tasks on the given
  // thread. Setup() can be called several times, provided that: (1) Stop()
  // is called; (2a) the previous task_runner is flushed or (2b) the task_runner
  // remains the same.
  // GetDumpProvidersFunction: is the function that will be invoked to get
  //   an updated list of polling-capable dump providers. This is really just
  //   MemoryDumpManager::GetDumpProvidersForPolling, but this extra level of
  //   indirection allows easier testing.
  // SequencedTaskRunner: the task runner where PollMemoryAndDetectPeak() will
  //  be periodically called.
  // OnPeakDetectedCallback: a callback that will be invoked on the
  //   given task runner when a memory peak is detected.
  void Setup(const GetDumpProvidersFunction&,
             const scoped_refptr<SequencedTaskRunner>&,
             const OnPeakDetectedCallback&);

  // Releases the |task_runner_| and the bound callbacks.
  void TearDown();

  // This posts a task onto the passed task runner which refreshes the list of
  // dump providers via the GetDumpProvidersFunction. If at least one dump
  // provider is available, this starts immediately polling on the task runner.
  // If not, the detector remains in the ENABLED state and will start polling
  // automatically (i.e. without requiring another call to Start()) on the
  // next call to NotifyMemoryDumpProvidersChanged().
  void Start(Config);

  // Stops the polling on the task runner (if was active at all). This doesn't
  // wait for the task runner to drain pending tasks, so it is possible that
  // a polling will happen concurrently (or in the immediate future) with the
  // Stop() call. It is responsibility of the caller to drain or synchronize
  // with the task runner.
  void Stop();

  // If Start()-ed, prevents that a peak callback is triggered before the next
  // |min_time_between_peaks_ms|. No-op if the peak detector is not enabled.
  void Throttle();

  // Used by MemoryDumpManager to notify that the list of polling-capable dump
  // providers has changed. The peak detector will reload the list on the next
  // polling task. This function can be called before Setup(), in which
  // case will be just a no-op.
  void NotifyMemoryDumpProvidersChanged();

  void SetStaticThresholdForTesting(uint64_t static_threshold_bytes);

 private:
  friend class MemoryPeakDetectorTest;

  static constexpr uint32_t kSlidingWindowNumSamples = 50;

  MemoryPeakDetector();
  ~MemoryPeakDetector();

  // All these methods are always called on the |task_runner_|.
  void StartInternal(Config);
  void StopInternal();
  void TearDownInternal();
  void ReloadDumpProvidersAndStartPollingIfNeeded();
  void PollMemoryAndDetectPeak(uint32_t expected_generation);
  bool DetectPeakUsingSlidingWindowStddev(uint64_t last_sample_bytes);
  void ResetPollHistory(bool keep_last_sample = false);

  // It is safe to call these testing methods only on the |task_runner_|.
  State state_for_testing() const { return state_; }
  uint32_t poll_tasks_count_for_testing() const {
    return poll_tasks_count_for_testing_;
  }

  // The task runner where all the internal calls are posted onto. This field
  // must be NOT be accessed by the tasks posted on the |task_runner_| because
  // there might still be outstanding tasks on the |task_runner_| while this
  // refptr is reset. This can only be safely accessed by the public methods
  // above, which the client of this class is supposed to call sequentially.
  scoped_refptr<SequencedTaskRunner> task_runner_;

  // After the Setup() call, the fields below, must be accessed only from
  // the |task_runner_|.

  // Bound function to get an updated list of polling-capable dump providers.
  GetDumpProvidersFunction get_dump_providers_function_;

  // The callback to invoke when peaks are detected.
  OnPeakDetectedCallback on_peak_detected_callback_;

  // List of polling-aware dump providers to invoke upon each poll.
  DumpProvidersList dump_providers_;

  // The generation is incremented every time the |state_| is changed and causes
  // PollMemoryAndDetectPeak() to early out if the posted task doesn't match the
  // most recent |generation_|. This allows to drop on the floor outstanding
  // PostDelayedTask that refer to an old sequence that was later Stop()-ed or
  // disabled because of NotifyMemoryDumpProvidersChanged().
  uint32_t generation_;

  State state_;

  // Config passed to Start(), only valid when |state_| = {ENABLED, RUNNING}.
  Config config_;

  uint64_t static_threshold_bytes_;
  uint32_t skip_polls_;
  uint64_t last_dump_memory_total_;
  uint64_t samples_bytes_[kSlidingWindowNumSamples];
  uint32_t samples_index_;
  uint32_t poll_tasks_count_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(MemoryPeakDetector);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_MEMORY_PEAK_DETECTOR_H_
