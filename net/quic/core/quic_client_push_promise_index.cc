// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_client_push_promise_index.h"

#include <string>

#include "net/quic/core/quic_client_promised_info.h"
#include "net/quic/core/spdy_utils.h"

using net::SpdyHeaderBlock;
using std::string;

namespace net {

QuicClientPushPromiseIndex::QuicClientPushPromiseIndex() {}

QuicClientPushPromiseIndex::~QuicClientPushPromiseIndex() {}

QuicClientPushPromiseIndex::TryHandle::~TryHandle() {}

QuicClientPromisedInfo* QuicClientPushPromiseIndex::GetPromised(
    const string& url) {
  QuicPromisedByUrlMap::iterator it = promised_by_url_.find(url);
  if (it == promised_by_url_.end()) {
    return nullptr;
  }
  return it->second;
}

QuicAsyncStatus QuicClientPushPromiseIndex::Try(
    const SpdyHeaderBlock& request,
    QuicClientPushPromiseIndex::Delegate* delegate,
    TryHandle** handle) {
  string url(SpdyUtils::GetUrlFromHeaderBlock(request));
  QuicPromisedByUrlMap::iterator it = promised_by_url_.find(url);
  if (it != promised_by_url_.end()) {
    QuicClientPromisedInfo* promised = it->second;
    QuicAsyncStatus rv = promised->HandleClientRequest(request, delegate);
    if (rv == QUIC_PENDING) {
      *handle = promised;
    }
    return rv;
  }
  return QUIC_FAILURE;
}

}  // namespace net
