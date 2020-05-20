// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_hostname_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// static
bool QuicHostnameUtils::IsValidSNI(quiche::QuicheStringPiece sni) {
  return QuicHostnameUtilsImpl::IsValidSNI(sni);
}

// static
std::string QuicHostnameUtils::NormalizeHostname(
    quiche::QuicheStringPiece hostname) {
  return QuicHostnameUtilsImpl::NormalizeHostname(hostname);
}

}  // namespace quic
