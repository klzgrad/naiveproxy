// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_SOCKET_ADDRESS_CODER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_SOCKET_ADDRESS_CODER_H_

#include <cstdint>

#include "base/macros.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

// Serializes and parses a socket address (IP address and port), to be used in
// the kCADR tag in the ServerHello handshake message and the Public Reset
// packet.
class QUIC_EXPORT_PRIVATE QuicSocketAddressCoder {
 public:
  QuicSocketAddressCoder();
  explicit QuicSocketAddressCoder(const QuicSocketAddress& address);
  QuicSocketAddressCoder(const QuicSocketAddressCoder&) = delete;
  QuicSocketAddressCoder& operator=(const QuicSocketAddressCoder&) = delete;
  ~QuicSocketAddressCoder();

  QuicString Encode() const;

  bool Decode(const char* data, size_t length);

  QuicIpAddress ip() const { return address_.host(); }

  uint16_t port() const { return address_.port(); }

 private:
  QuicSocketAddress address_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_SOCKET_ADDRESS_CODER_H_
