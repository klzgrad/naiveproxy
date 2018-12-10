// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_url_utils.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

// static
QuicString QuicUrlUtils::HostName(QuicStringPiece url) {
  return QuicUrlUtilsImpl::HostName(url);
}

// static
bool QuicUrlUtils::IsValidUrl(QuicStringPiece url) {
  return QuicUrlUtilsImpl::IsValidUrl(url);
}

// static
QuicString QuicUrlUtils::GetPushPromiseUrl(QuicStringPiece scheme,
                                           QuicStringPiece authority,
                                           QuicStringPiece path) {
  return QuicUrlUtilsImpl::GetPushPromiseUrl(scheme, authority, path);
}

}  // namespace quic
