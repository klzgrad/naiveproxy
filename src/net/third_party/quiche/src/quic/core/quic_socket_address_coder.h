// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_SOCKET_ADDRESS_CODER_H_
#define QUICHE_QUIC_CORE_QUIC_SOCKET_ADDRESS_CODER_H_

#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

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

  std::string Encode() const;

  bool Decode(const char* data, size_t length);

  QuicIpAddress ip() const { return address_.host(); }

  uint16_t port() const { return address_.port(); }

 private:
  QuicSocketAddress address_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SOCKET_ADDRESS_CODER_H_
