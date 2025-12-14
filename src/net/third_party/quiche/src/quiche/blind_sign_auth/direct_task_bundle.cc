// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/blind_sign_auth/direct_task_bundle.h"

#include <utility>

#include "absl/status/status.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

DirectTaskBundle::~DirectTaskBundle() {
  QUICHE_DCHECK(tasks_.empty())
      << "DirectTaskBundle destroyed with pending tasks";
}

void DirectTaskBundle::Add(SingleUseCallback<absl::Status()> task) {
  tasks_.push_back(std::move(task));
}

absl::Status DirectTaskBundle::Join() {
  absl::Status status = absl::OkStatus();
  for (auto& task : tasks_) {
    status = std::move(task)();
    if (!status.ok()) {
      break;
    }
  }
  tasks_.clear();
  return status;
}

}  // namespace quiche
