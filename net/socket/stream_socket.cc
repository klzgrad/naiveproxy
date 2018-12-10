// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/stream_socket.h"

#include "base/logging.h"

namespace net {

void StreamSocket::GetSSLCertRequestInfo(
    SSLCertRequestInfo* cert_request_info) const {
  NOTREACHED();
}

ChannelIDService* StreamSocket::GetChannelIDService() const {
  NOTREACHED();
  return nullptr;
}

Error StreamSocket::GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                             TokenBindingType tb_type,
                                             std::vector<uint8_t>* out) {
  NOTREACHED();
  return ERR_NOT_IMPLEMENTED;
}

crypto::ECPrivateKey* StreamSocket::GetChannelIDKey() const {
  NOTREACHED();
  return nullptr;
}

StreamSocket::SocketMemoryStats::SocketMemoryStats()
    : total_size(0), buffer_size(0), cert_count(0), cert_size(0) {}

StreamSocket::SocketMemoryStats::~SocketMemoryStats() = default;

int StreamSocket::ConfirmHandshake(CompletionOnceCallback callback) {
  return OK;
}

}  // namespace net
