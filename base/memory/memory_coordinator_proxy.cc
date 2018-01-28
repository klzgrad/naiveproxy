// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_coordinator_proxy.h"

namespace base {

namespace {

MemoryCoordinator* g_memory_coordinator = nullptr;

}  // namespace

MemoryCoordinatorProxy::MemoryCoordinatorProxy() {
}

MemoryCoordinatorProxy::~MemoryCoordinatorProxy() {
}

// static
MemoryCoordinatorProxy* MemoryCoordinatorProxy::GetInstance() {
  return Singleton<base::MemoryCoordinatorProxy>::get();
}

// static
void MemoryCoordinatorProxy::SetMemoryCoordinator(
    MemoryCoordinator* coordinator) {
  DCHECK(!g_memory_coordinator || !coordinator);
  g_memory_coordinator = coordinator;
}

MemoryState MemoryCoordinatorProxy::GetCurrentMemoryState() const {
  if (!g_memory_coordinator)
    return MemoryState::NORMAL;
  return g_memory_coordinator->GetCurrentMemoryState();
}

}  // namespace base
