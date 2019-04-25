// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/sequence_manager.h"

namespace base {
namespace sequence_manager {

SequenceManager::MetricRecordingSettings::MetricRecordingSettings(
    double task_thread_time_sampling_rate)
    : task_sampling_rate_for_recording_cpu_time(
          base::ThreadTicks::IsSupported() ? task_thread_time_sampling_rate
                                           : 0) {}

}  // namespace sequence_manager
}  // namespace base
