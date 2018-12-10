// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/sequence_manager.h"

namespace base {
namespace sequence_manager {

SequenceManager::MetricRecordingSettings::MetricRecordingSettings() {}

SequenceManager::MetricRecordingSettings::MetricRecordingSettings(
    bool cpu_time_for_each_task,
    double task_thread_time_sampling_rate)
    : records_cpu_time_for_each_task(base::ThreadTicks::IsSupported() &&
                                     cpu_time_for_each_task),
      task_sampling_rate_for_recording_cpu_time(
          task_thread_time_sampling_rate) {
  if (records_cpu_time_for_each_task)
    task_sampling_rate_for_recording_cpu_time = 1;
  if (!base::ThreadTicks::IsSupported())
    task_sampling_rate_for_recording_cpu_time = 0;
}

}  // namespace sequence_manager
}  // namespace base
