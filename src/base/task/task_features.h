// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_FEATURES_H_
#define BASE_TASK_TASK_FEATURES_H_

#include "base/base_export.h"
#include "base/metrics/field_trial_params.h"

namespace base {

struct Feature;

extern const BASE_EXPORT Feature kAllTasksUserBlocking;
extern const BASE_EXPORT Feature kMergeBlockingNonBlockingPools;
extern const BASE_EXPORT Feature kMayBlockTimings;

// Under this feature, unused threads in SchedulerWorkerPool are only detached
// if the total number of threads in the pool is above the initial capacity.
extern const BASE_EXPORT Feature kNoDetachBelowInitialCapacity;

// Threshold after which the maximum number of tasks running in a foreground
// pool can be incremented to compensate for a task that is within a MAY_BLOCK
// ScopedBlockingCall (a constant is used for background pools).
extern const BASE_EXPORT FeatureParam<int> kMayBlockThresholdMicrosecondsParam;

// Interval at which the service thread checks for workers in a foreground pool
// that have been in a MAY_BLOCK ScopedBlockingCall for more than
// |kMayBlockThresholdMicrosecondsParam| (a constant is used for background
// pools).
extern const BASE_EXPORT FeatureParam<int> kBlockedWorkersPollMicrosecondsParam;

}  // namespace base

#endif  // BASE_TASK_TASK_FEATURES_H_
