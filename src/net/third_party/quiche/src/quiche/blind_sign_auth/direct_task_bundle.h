// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BLIND_SIGN_AUTH_DIRECT_TASK_BUNDLE_H_
#define QUICHE_BLIND_SIGN_AUTH_DIRECT_TASK_BUNDLE_H_

#include <vector>

#include "absl/status/status.h"
#include "quiche/blind_sign_auth/task_bundle.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// A TaskBundle that executes tasks directly on the calling thread in the order
// they were added.
class QUICHE_EXPORT DirectTaskBundle : public TaskBundle {
 public:
  ~DirectTaskBundle() override;

  void Add(SingleUseCallback<absl::Status()> task) override;
  absl::Status Join() override;

 private:
  std::vector<SingleUseCallback<absl::Status()>> tasks_;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_DIRECT_TASK_BUNDLE_H_
