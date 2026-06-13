// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A toy client, which connects to a specified port and sends QUIC
// requests to that endpoint.

#ifndef QUICHE_QUIC_TOOLS_QUIC_TOY_CLIENT_H_
#define QUICHE_QUIC_TOOLS_QUIC_TOY_CLIENT_H_

#include "quiche/quic/tools/quic_client_factory.h"

namespace quic {

class QuicToyClient {
 public:
  // Constructs a new toy client that will use |client_factory| to create the
  // actual QuicSpdyClientBase instance.
  QuicToyClient(ClientFactoryInterface* client_factory);

  // Connects to the QUIC server based on the various flags defined in the
  // .cc file, sends requests and prints the responses. Returns 0 on success
  // and non-zero otherwise.
  int SendRequestsAndPrintResponses(std::vector<std::string> urls);

  // Compatibility alias
  using ClientFactory = ClientFactoryInterface;

 private:
  ClientFactoryInterface* client_factory_;  // Unowned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_TOY_CLIENT_H_
