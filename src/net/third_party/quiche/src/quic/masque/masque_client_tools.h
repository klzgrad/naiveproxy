// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_

#include "net/third_party/quiche/src/quic/masque/masque_epoll_client.h"

namespace quic {
namespace tools {

// Sends an HTTP GET request for |url_string|, proxied over the MASQUE
// connection represented by |masque_client|. A valid and owned |epoll_server|
// is required. |disable_certificate_verification| allows disabling verification
// of the HTTP server's TLS certificate.
bool SendEncapsulatedMasqueRequest(MasqueEpollClient* masque_client,
                                   QuicEpollServer* epoll_server,
                                   std::string url_string,
                                   bool disable_certificate_verification);

}  // namespace tools
}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_
