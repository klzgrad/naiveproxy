// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_DEFAULT_PROOF_PROVIDERS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_DEFAULT_PROOF_PROVIDERS_IMPL_H_

#include <memory>

#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/proof_verifier.h"

namespace quiche {

std::unique_ptr<quic::ProofVerifier> CreateDefaultProofVerifierImpl(
    const std::string& host);
std::unique_ptr<quic::ProofSource> CreateDefaultProofSourceImpl();

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_DEFAULT_PROOF_PROVIDERS_IMPL_H_
