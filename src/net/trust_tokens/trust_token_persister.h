// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TRUST_TOKENS_TRUST_TOKEN_PERSISTER_H_
#define NET_TRUST_TOKENS_TRUST_TOKEN_PERSISTER_H_

#include <memory>

#include "url/origin.h"

namespace net {

class TrustTokenIssuerConfig;
class TrustTokenToplevelConfig;
class TrustTokenIssuerToplevelPairConfig;

// Interface TrustTokenPersister defines interaction with a backing store for
// Trust Tokens state. The most-frequently-used implementation will
// be on top of SQLite; there is also an ephemeral implementation for
// tests and environments not built with SQLite.
class TrustTokenPersister {
 public:
  TrustTokenPersister() = default;
  virtual ~TrustTokenPersister() = default;

  TrustTokenPersister(const TrustTokenPersister&) = delete;
  TrustTokenPersister& operator=(const TrustTokenPersister&) = delete;

  virtual std::unique_ptr<TrustTokenIssuerConfig> GetIssuerConfig(
      const url::Origin& issuer) = 0;
  virtual std::unique_ptr<TrustTokenToplevelConfig> GetToplevelConfig(
      const url::Origin& toplevel) = 0;
  virtual std::unique_ptr<TrustTokenIssuerToplevelPairConfig>
  GetIssuerToplevelPairConfig(const url::Origin& issuer,
                              const url::Origin& toplevel) = 0;

  virtual void SetIssuerConfig(
      const url::Origin& issuer,
      std::unique_ptr<TrustTokenIssuerConfig> config) = 0;
  virtual void SetToplevelConfig(
      const url::Origin& toplevel,
      std::unique_ptr<TrustTokenToplevelConfig> config) = 0;
  virtual void SetIssuerToplevelPairConfig(
      const url::Origin& issuer,
      const url::Origin& toplevel,
      std::unique_ptr<TrustTokenIssuerToplevelPairConfig> config) = 0;
};

}  // namespace net

#endif  // NET_TRUST_TOKENS_TRUST_TOKEN_PERSISTER_H_
