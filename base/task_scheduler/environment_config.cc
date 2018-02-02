// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/environment_config.h"

namespace base {
namespace internal {

size_t GetEnvironmentIndexForTraits(const TaskTraits& traits) {
  const bool is_background =
      traits.priority() == base::TaskPriority::BACKGROUND;
  if (traits.may_block() || traits.with_base_sync_primitives())
    return is_background ? BACKGROUND_BLOCKING : FOREGROUND_BLOCKING;
  return is_background ? BACKGROUND : FOREGROUND;
}

}  // namespace internal
}  // namespace base
