// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_coordinator_client.h"

#include "base/logging.h"

namespace base {

const char* MemoryStateToString(MemoryState state) {
  switch (state) {
    case MemoryState::UNKNOWN:
      return "unknown";
    case MemoryState::NORMAL:
      return "normal";
    case MemoryState::THROTTLED:
      return "throttled";
    case MemoryState::SUSPENDED:
      return "suspended";
    default:
      NOTREACHED();
  }
  return "";
}

}  // namespace base
