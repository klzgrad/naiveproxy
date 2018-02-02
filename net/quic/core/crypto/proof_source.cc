// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/proof_source.h"

using std::string;

namespace net {

ProofSource::Chain::Chain(const std::vector<string>& certs) : certs(certs) {}

ProofSource::Chain::~Chain() {}

}  // namespace net
