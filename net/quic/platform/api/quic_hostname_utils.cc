// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_hostname_utils.h"

using std::string;

namespace net {

// static
bool QuicHostnameUtils::IsValidSNI(QuicStringPiece sni) {
  return QuicHostnameUtilsImpl::IsValidSNI(sni);
}

// static
char* QuicHostnameUtils::NormalizeHostname(char* hostname) {
  return QuicHostnameUtilsImpl::NormalizeHostname(hostname);
}

// static
void QuicHostnameUtils::StringToQuicServerId(const string& str,
                                             QuicServerId* out) {
  QuicHostnameUtilsImpl::StringToQuicServerId(str, out);
}

}  // namespace net
