// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_

#include <memory>
#include <string>

#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/masque/masque_client.h"
#include "quiche/quic/masque/masque_encapsulated_client.h"
#include "quiche/quic/masque/masque_utils.h"

namespace quic {
namespace tools {

// Establishes an encapsulated MASQUE session over the underlying
// |masque_client|.
std::unique_ptr<MasqueEncapsulatedClient>
CreateAndConnectMasqueEncapsulatedClient(
    MasqueClient* masque_client, MasqueMode masque_mode,
    QuicEventLoop* event_loop, std::string url_string,
    bool disable_certificate_verification, int address_family_for_lookup,
    bool dns_on_client, bool is_also_underlying);

// Sends an HTTP GET request for |url_string|, proxied over the encapsulated
// MASQUE connection represented by |client|.
bool SendRequestOnMasqueEncapsulatedClient(MasqueEncapsulatedClient& client,
                                           std::string url_string);

}  // namespace tools
}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CLIENT_TOOLS_H_
