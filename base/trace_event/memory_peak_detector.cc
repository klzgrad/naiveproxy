// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/memory_peak_detector.h"

#include <algorithm>

#include "base/bind.h"
#include "base/logging.h"
#include "base/sys_info.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/memory_dump_provider_info.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"

namespace base {
namespace trace_event {

// static
MemoryPeakDetector* MemoryPeakDetector::GetInstance() {
  static MemoryPeakDetector* instance = new MemoryPeakDetector();
  return instance;
}

MemoryPeakDetector::MemoryPeakDetector()
    : generation_(0),
      state_(NOT_INITIALIZED),
      poll_tasks_count_for_testing_(0) {}

MemoryPeakDetector::~MemoryPeakDetector() {
  // This is hit only in tests, in which case the test is expected to TearDown()
  // cleanly and not leave the peak detector running.
  DCHECK_EQ(NOT_INITIALIZED, state_);
}

void MemoryPeakDetector::Setup(
    const GetDumpProvidersFunction& get_dump_providers_function,
    const scoped_refptr<SequencedTaskRunner>& task_runner,
    const OnPeakDetectedCallback& on_peak_detected_callback) {
  DCHECK(!get_dump_providers_function.is_null());
  DCHECK(task_runner);
  DCHECK(!on_peak_detected_callback.is_null());
  DCHECK(state_ == NOT_INITIALIZED || state_ == DISABLED);
  DCHECK(dump_providers_.empty());
  get_dump_providers_function_ = get_dump_providers_function;
  task_runner_ = task_runner;
  on_peak_detected_callback_ = on_peak_detected_callback;
  state_ = DISABLED;
  config_ = {};
  ResetPollHistory();

  static_threshold_bytes_ = 0;
#if !defined(OS_NACL)
  // Set threshold to 1% of total system memory.
  static_threshold_bytes_ =
      static_cast<uint64_t>(SysInfo::AmountOfPhysicalMemory()) / 100;
#endif
  // Fallback, mostly for test environments where AmountOfPhysicalMemory() is
  // broken.
  static_threshold_bytes_ =
      std::max(static_threshold_bytes_, static_cast<uint64_t>(5 * 1024 * 1024));
}

void MemoryPeakDetector::TearDown() {
  if (task_runner_) {
    task_runner_->PostTask(
        FROM_HERE,
        BindOnce(&MemoryPeakDetector::TearDownInternal, Unretained(this)));
  }
  task_runner_ = nullptr;
}

void MemoryPeakDetector::Start(MemoryPeakDetector::Config config) {
  if (!config.polling_interval_ms) {
    NOTREACHED();
    return;
  }
  task_runner_->PostTask(FROM_HERE, BindOnce(&MemoryPeakDetector::StartInternal,
                                             Unretained(this), config));
}

void MemoryPeakDetector::Stop() {
  task_runner_->PostTask(
      FROM_HERE, BindOnce(&MemoryPeakDetector::StopInternal, Unretained(this)));
}

void MemoryPeakDetector::Throttle() {
  if (!task_runner_)
    return;  // Can be called before Setup().
  task_runner_->PostTask(
      FROM_HERE, BindOnce(&MemoryPeakDetector::ResetPollHistory,
                          Unretained(this), true /* keep_last_sample */));
}

void MemoryPeakDetector::NotifyMemoryDumpProvidersChanged() {
  if (!task_runner_)
    return;  // Can be called before Setup().
  task_runner_->PostTask(
      FROM_HERE,
      BindOnce(&MemoryPeakDetector::ReloadDumpProvidersAndStartPollingIfNeeded,
               Unretained(this)));
}

void MemoryPeakDetector::StartInternal(MemoryPeakDetector::Config config) {
  DCHECK_EQ(DISABLED, state_);
  state_ = ENABLED;
  config_ = config;
  ResetPollHistory();

  // If there are any dump providers available,
  // NotifyMemoryDumpProvidersChanged will fetch them and start the polling.
  // Otherwise this will remain in the ENABLED state and the actual polling
  // will start on the next call to
  // ReloadDumpProvidersAndStartPollingIfNeeded().
  // Depending on the sandbox model, it is possible that no polling-capable
  // dump providers will be ever available.
  ReloadDumpProvidersAndStartPollingIfNeeded();
}

void MemoryPeakDetector::StopInternal() {
  DCHECK_NE(NOT_INITIALIZED, state_);
  state_ = DISABLED;
  ++generation_;
  for (const scoped_refptr<MemoryDumpProviderInfo>& mdp_info : dump_providers_)
    mdp_info->dump_provider->SuspendFastMemoryPolling();
  dump_providers_.clear();
}

void MemoryPeakDetector::TearDownInternal() {
  StopInternal();
  get_dump_providers_function_.Reset();
  on_peak_detected_callback_.Reset();
  state_ = NOT_INITIALIZED;
}

void MemoryPeakDetector::ReloadDumpProvidersAndStartPollingIfNeeded() {
  if (state_ == DISABLED || state_ == NOT_INITIALIZED)
    return;  // Start() will re-fetch the MDP list later.

  DCHECK((state_ == RUNNING && !dump_providers_.empty()) ||
         (state_ == ENABLED && dump_providers_.empty()));

  dump_providers_.clear();

  // This is really MemoryDumpManager::GetDumpProvidersForPolling, % testing.
  get_dump_providers_function_.Run(&dump_providers_);

  if (state_ == ENABLED && !dump_providers_.empty()) {
    // It's now time to start polling for realz.
    state_ = RUNNING;
    task_runner_->PostTask(
        FROM_HERE, BindOnce(&MemoryPeakDetector::PollMemoryAndDetectPeak,
                            Unretained(this), ++generation_));
  } else if (state_ == RUNNING && dump_providers_.empty()) {
    // Will cause the next PollMemoryAndDetectPeak() task to early return.
    state_ = ENABLED;
    ++generation_;
  }
}

void MemoryPeakDetector::PollMemoryAndDetectPeak(uint32_t expected_generation) {
  if (state_ != RUNNING || generation_ != expected_generation)
    return;

  // We should never end up in a situation where state_ == RUNNING but all dump
  // providers are gone.
  DCHECK(!dump_providers_.empty());

  poll_tasks_count_for_testing_++;
  uint64_t polled_mem_bytes = 0;
  for (const scoped_refptr<MemoryDumpProviderInfo>& mdp_info :
       dump_providers_) {
    DCHECK(mdp_info->options.is_fast_polling_supported);
    uint64_t value = 0;
    mdp_info->dump_provider->PollFastMemoryTotal(&value);
    polled_mem_bytes += value;
  }
  if (config_.enable_verbose_poll_tracing) {
    TRACE_COUNTER1(MemoryDumpManager::kTraceCategory, "PolledMemoryMB",
                   polled_mem_bytes / 1024 / 1024);
  }

  // Peak detection logic. Design doc: https://goo.gl/0kOU4A .
  bool is_peak = false;
  if (skip_polls_ > 0) {
    skip_polls_--;
  } else if (last_dump_memory_total_ == 0) {
    last_dump_memory_total_ = polled_mem_bytes;
  } else if (polled_mem_bytes > 0) {
    int64_t diff_from_last_dump = polled_mem_bytes - last_dump_memory_total_;

    DCHECK_GT(static_threshold_bytes_, 0u);
    is_peak =
        diff_from_last_dump > static_cast<int64_t>(static_threshold_bytes_);

    if (!is_peak)
      is_peak = DetectPeakUsingSlidingWindowStddev(polled_mem_bytes);
  }

  DCHECK_GT(config_.polling_interval_ms, 0u);
  SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      BindOnce(&MemoryPeakDetector::PollMemoryAndDetectPeak, Unretained(this),
               expected_generation),
      TimeDelta::FromMilliseconds(config_.polling_interval_ms));

  if (!is_peak)
    return;
  TRACE_EVENT_INSTANT1(MemoryDumpManager::kTraceCategory,
                       "Peak memory detected", TRACE_EVENT_SCOPE_PROCESS,
                       "PolledMemoryMB", polled_mem_bytes / 1024 / 1024);
  ResetPollHistory(true /* keep_last_sample */);
  last_dump_memory_total_ = polled_mem_bytes;
  on_peak_detected_callback_.Run();
}

bool MemoryPeakDetector::DetectPeakUsingSlidingWindowStddev(
    uint64_t polled_mem_bytes) {
  DCHECK(polled_mem_bytes);
  samples_bytes_[samples_index_] = polled_mem_bytes;
  samples_index_ = (samples_index_ + 1) % kSlidingWindowNumSamples;
  float mean = 0;
  for (uint32_t i = 0; i < kSlidingWindowNumSamples; ++i) {
    if (samples_bytes_[i] == 0)
      return false;  // Not enough samples to detect peaks.
    mean += samples_bytes_[i];
  }
  mean /= kSlidingWindowNumSamples;
  float variance = 0;
  for (uint32_t i = 0; i < kSlidingWindowNumSamples; ++i) {
    const float deviation = samples_bytes_[i] - mean;
    variance += deviation * deviation;
  }
  variance /= kSlidingWindowNumSamples;

  // If stddev is less than 0.2% then we consider that the process is inactive.
  if (variance < (mean / 500) * (mean / 500))
    return false;

  // (mean + 3.69 * stddev) corresponds to a value that is higher than current
  // sample with 99.99% probability.
  const float cur_sample_deviation = polled_mem_bytes - mean;
  return cur_sample_deviation * cur_sample_deviation > (3.69 * 3.69 * variance);
}

void MemoryPeakDetector::ResetPollHistory(bool keep_last_sample) {
  // TODO(primiano,ssid): this logic should probably be revisited. In the case
  // of Android, the browser process sees the total of all processes memory in
  // the same peak detector instance. Perhaps the best thing to do here is to
  // keep the window of samples around and just bump the skip_polls_.
  last_dump_memory_total_ = 0;
  if (keep_last_sample) {
    const uint32_t prev_index =
        samples_index_ > 0 ? samples_index_ - 1 : kSlidingWindowNumSamples - 1;
    last_dump_memory_total_ = samples_bytes_[prev_index];
  }
  memset(samples_bytes_, 0, sizeof(samples_bytes_));
  samples_index_ = 0;
  skip_polls_ = 0;
  if (config_.polling_interval_ms > 0) {
    skip_polls_ =
        (config_.min_time_between_peaks_ms + config_.polling_interval_ms - 1) /
        config_.polling_interval_ms;
  }
}

void MemoryPeakDetector::SetStaticThresholdForTesting(
    uint64_t static_threshold_bytes) {
  DCHECK_EQ(DISABLED, state_);
  static_threshold_bytes_ = static_threshold_bytes;
}

MemoryPeakDetector::MemoryPeakDetector::Config::Config()
    : Config(0, 0, false) {}

MemoryPeakDetector::MemoryPeakDetector::Config::Config(
    uint32_t polling_interval_ms,
    uint32_t min_time_between_peaks_ms,
    bool enable_verbose_poll_tracing)
    : polling_interval_ms(polling_interval_ms),
      min_time_between_peaks_ms(min_time_between_peaks_ms),
      enable_verbose_poll_tracing(enable_verbose_poll_tracing) {}

}  // namespace trace_event
}  // namespace base
