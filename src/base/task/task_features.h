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

// Under this feature, unused threads in SchedulerWorkerPool are only detached
// if the total number of threads in the pool is above the initial capacity.
extern const BASE_EXPORT Feature kNoDetachBelowInitialCapacity;

// Under this feature, workers blocked with MayBlock are replaced immediately
// instead of waiting for a threshold.
extern const BASE_EXPORT Feature kMayBlockWithoutDelay;

}  // namespace base

#endif  // BASE_TASK_TASK_FEATURES_H_
