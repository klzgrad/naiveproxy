// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_coordinator_client_registry.h"

namespace base {

// static
MemoryCoordinatorClientRegistry*
MemoryCoordinatorClientRegistry::GetInstance() {
  return Singleton<
      MemoryCoordinatorClientRegistry,
      LeakySingletonTraits<MemoryCoordinatorClientRegistry>>::get();
}

MemoryCoordinatorClientRegistry::MemoryCoordinatorClientRegistry()
    : clients_(new ClientList) {}

MemoryCoordinatorClientRegistry::~MemoryCoordinatorClientRegistry() = default;

void MemoryCoordinatorClientRegistry::Register(
    MemoryCoordinatorClient* client) {
  clients_->AddObserver(client);
}

void MemoryCoordinatorClientRegistry::Unregister(
    MemoryCoordinatorClient* client) {
  clients_->RemoveObserver(client);
}

void MemoryCoordinatorClientRegistry::Notify(MemoryState state) {
  clients_->Notify(FROM_HERE,
                   &base::MemoryCoordinatorClient::OnMemoryStateChange, state);
}

void MemoryCoordinatorClientRegistry::PurgeMemory() {
  clients_->Notify(FROM_HERE, &base::MemoryCoordinatorClient::OnPurgeMemory);
}

}  // namespace base
