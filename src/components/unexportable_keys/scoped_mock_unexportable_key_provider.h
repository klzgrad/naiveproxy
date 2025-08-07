// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_UNEXPORTABLE_KEYS_SCOPED_MOCK_UNEXPORTABLE_KEY_PROVIDER_H_
#define COMPONENTS_UNEXPORTABLE_KEYS_SCOPED_MOCK_UNEXPORTABLE_KEY_PROVIDER_H_

#include "base/containers/queue.h"
#include "crypto/unexportable_key.h"

namespace unexportable_keys {

// Causes `GetUnexportableKeyProvider()` to return fully mockable
// `MockUnexportableKey`s while it is in scope.
// The mock provider will return mock keys previously added via
// `AddNextGeneratedKey()` in the queue order.
class ScopedMockUnexportableKeyProvider {
 public:
  ScopedMockUnexportableKeyProvider();
  ScopedMockUnexportableKeyProvider(const ScopedMockUnexportableKeyProvider&) =
      delete;
  ScopedMockUnexportableKeyProvider& operator=(
      const ScopedMockUnexportableKeyProvider&) = delete;
  ~ScopedMockUnexportableKeyProvider();

  void AddNextGeneratedKey(std::unique_ptr<crypto::UnexportableSigningKey> key);

  std::unique_ptr<crypto::UnexportableSigningKey> GetNextGeneratedKey();

 private:
  base::queue<std::unique_ptr<crypto::UnexportableSigningKey>>
      next_generated_keys_;
};

}  // namespace unexportable_keys

#endif  // COMPONENTS_UNEXPORTABLE_KEYS_SCOPED_MOCK_UNEXPORTABLE_KEY_PROVIDER_H_
