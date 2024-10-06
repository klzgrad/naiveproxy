// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_NAME_LOOKUP_H_
#define QUICHE_QUIC_TOOLS_QUIC_NAME_LOOKUP_H_

#include <string>

#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

class QuicServerId;

namespace tools {

quic::QuicSocketAddress LookupAddress(int address_family_for_lookup,
                                      std::string host, std::string port);

quic::QuicSocketAddress LookupAddress(int address_family_for_lookup,
                                      const QuicServerId& server_id);

inline QuicSocketAddress LookupAddress(std::string host, std::string port) {
  return LookupAddress(0, host, port);
}

inline QuicSocketAddress LookupAddress(const QuicServerId& server_id) {
  return LookupAddress(0, server_id);
}

}  // namespace tools
}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_NAME_LOOKUP_H_
