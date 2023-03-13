// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_

#include "quiche/quic/masque/masque_client.h"

namespace quic {
namespace tools {

// Sends an HTTP GET request for |url_string|, proxied over the MASQUE
// connection represented by |masque_client|. A valid and owned |event_loop|
// is required. |disable_certificate_verification| allows disabling verification
// of the HTTP server's TLS certificate.
bool SendEncapsulatedMasqueRequest(MasqueClient* masque_client,
                                   QuicEventLoop* event_loop,
                                   std::string url_string,
                                   bool disable_certificate_verification,
                                   int address_family_for_lookup);

}  // namespace tools
}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_
