// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/proof_source.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

ProofSource::Chain::Chain(const std::vector<QuicString>& certs)
    : certs(certs) {}

ProofSource::Chain::~Chain() {}

}  // namespace quic
