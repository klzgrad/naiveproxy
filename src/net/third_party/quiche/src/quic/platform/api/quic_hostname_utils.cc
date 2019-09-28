// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_hostname_utils.h"

namespace quic {

// static
bool QuicHostnameUtils::IsValidSNI(QuicStringPiece sni) {
  return QuicHostnameUtilsImpl::IsValidSNI(sni);
}

// static
std::string QuicHostnameUtils::NormalizeHostname(QuicStringPiece hostname) {
  return QuicHostnameUtilsImpl::NormalizeHostname(hostname);
}

}  // namespace quic
