// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"

namespace quic {

ProofSource::Chain::Chain(const std::vector<std::string>& certs)
    : certs(certs) {}

ProofSource::Chain::~Chain() {}

}  // namespace quic
