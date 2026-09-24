// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_TASK_BUNDLE_H_
#define QUICHE_BLIND_SIGN_AUTH_TASK_BUNDLE_H_

#include "absl/status/status.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// Interface for executing multiple independent tasks, possibly in parallel.
class QUICHE_EXPORT TaskBundle {
 public:
  virtual ~TaskBundle() = default;

  // Enqueues a task to be executed.
  virtual void Add(SingleUseCallback<absl::Status()> task) = 0;

  // Blocks until all added tasks have completed.
  // Returns OK if all tasks completed successfully, or the first error
  // encountered otherwise.
  virtual absl::Status Join() = 0;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_TASK_BUNDLE_H_
