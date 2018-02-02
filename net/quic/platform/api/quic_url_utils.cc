// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_url_utils.h"

using std::string;

namespace net {

// static
string QuicUrlUtils::HostName(QuicStringPiece url) {
  return QuicUrlUtilsImpl::HostName(url);
}

// static
bool QuicUrlUtils::IsValidUrl(QuicStringPiece url) {
  return QuicUrlUtilsImpl::IsValidUrl(url);
}

}  // namespace net
