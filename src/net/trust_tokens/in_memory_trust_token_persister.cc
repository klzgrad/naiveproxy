// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/trust_tokens/in_memory_trust_token_persister.h"

namespace net {

InMemoryTrustTokenPersister::InMemoryTrustTokenPersister() = default;
InMemoryTrustTokenPersister::~InMemoryTrustTokenPersister() = default;

std::unique_ptr<TrustTokenToplevelConfig>
InMemoryTrustTokenPersister::GetToplevelConfig(const url::Origin& toplevel) {
  auto it = toplevel_configs_.find(toplevel);
  if (it == toplevel_configs_.end())
    return nullptr;
  return std::make_unique<TrustTokenToplevelConfig>(*it->second);
}

std::unique_ptr<TrustTokenIssuerConfig>
InMemoryTrustTokenPersister::GetIssuerConfig(const url::Origin& issuer) {
  auto it = issuer_configs_.find(issuer);
  if (it == issuer_configs_.end())
    return nullptr;
  return std::make_unique<TrustTokenIssuerConfig>(*it->second);
}

std::unique_ptr<TrustTokenIssuerToplevelPairConfig>
InMemoryTrustTokenPersister::GetIssuerToplevelPairConfig(
    const url::Origin& issuer,
    const url::Origin& toplevel) {
  auto it =
      issuer_toplevel_pair_configs_.find(std::make_pair(issuer, toplevel));
  if (it == issuer_toplevel_pair_configs_.end())
    return nullptr;
  return std::make_unique<TrustTokenIssuerToplevelPairConfig>(*it->second);
}

void InMemoryTrustTokenPersister::SetToplevelConfig(
    const url::Origin& toplevel,
    std::unique_ptr<TrustTokenToplevelConfig> config) {
  toplevel_configs_[toplevel] = std::move(config);
}

void InMemoryTrustTokenPersister::SetIssuerConfig(
    const url::Origin& issuer,
    std::unique_ptr<TrustTokenIssuerConfig> config) {
  issuer_configs_[issuer] = std::move(config);
}

void InMemoryTrustTokenPersister::SetIssuerToplevelPairConfig(
    const url::Origin& issuer,
    const url::Origin& toplevel,
    std::unique_ptr<TrustTokenIssuerToplevelPairConfig> config) {
  issuer_toplevel_pair_configs_[std::make_pair(issuer, toplevel)] =
      std::move(config);
}

}  // namespace net
