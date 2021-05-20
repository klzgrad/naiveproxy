// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is reponsible for the masque_client binary. It allows testing
// our MASQUE client code by connecting to a MASQUE proxy and then sending
// HTTP/3 requests to web servers tunnelled over that MASQUE connection.
// e.g.: masque_client $PROXY_HOST:$PROXY_PORT $URL1 $URL2

#include <memory>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quic/core/quic_server_id.h"
#include "quic/masque/masque_client_tools.h"
#include "quic/masque/masque_encapsulated_epoll_client.h"
#include "quic/masque/masque_epoll_client.h"
#include "quic/masque/masque_utils.h"
#include "quic/platform/api/quic_default_proof_providers.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_system_event_loop.h"
#include "quic/tools/fake_proof_verifier.h"
#include "quic/tools/quic_url.h"
#include "common/platform/api/quiche_text_utils.h"

DEFINE_QUIC_COMMAND_LINE_FLAG(bool,
                              disable_certificate_verification,
                              false,
                              "If true, don't verify the server certificate.");

namespace quic {

namespace {

int RunMasqueClient(int argc, char* argv[]) {
  QuicSystemEventLoop event_loop("masque_client");
  const char* usage = "Usage: masque_client [options] <url>";

  // The first non-flag argument is the MASQUE server. All subsequent ones are
  // interpreted as URLs to fetch via the MASQUE server.
  std::vector<std::string> urls = QuicParseCommandLineFlags(usage, argc, argv);
  if (urls.empty()) {
    QuicPrintCommandLineFlagHelp(usage);
    return 1;
  }

  const bool disable_certificate_verification =
      GetQuicFlag(FLAGS_disable_certificate_verification);
  QuicEpollServer epoll_server;

  QuicUrl masque_url(urls[0], "https");
  if (masque_url.host().empty()) {
    masque_url = QuicUrl(absl::StrCat("https://", urls[0]), "https");
  }
  if (masque_url.host().empty()) {
    QUIC_LOG(ERROR) << "Failed to parse MASQUE server address " << urls[0];
    return 1;
  }
  std::unique_ptr<ProofVerifier> proof_verifier;
  if (disable_certificate_verification) {
    proof_verifier = std::make_unique<FakeProofVerifier>();
  } else {
    proof_verifier = CreateDefaultProofVerifier(masque_url.host());
  }
  std::unique_ptr<MasqueEpollClient> masque_client =
      MasqueEpollClient::Create(masque_url.host(), masque_url.port(),
                                &epoll_server, std::move(proof_verifier));
  if (masque_client == nullptr) {
    return 1;
  }

  std::cerr << "MASQUE is connected " << masque_client->connection_id()
            << std::endl;

  for (size_t i = 1; i < urls.size(); ++i) {
    if (!tools::SendEncapsulatedMasqueRequest(
            masque_client.get(), &epoll_server, urls[i],
            disable_certificate_verification)) {
      return 1;
    }
  }

  return 0;
}

}  // namespace

}  // namespace quic

int main(int argc, char* argv[]) {
  return quic::RunMasqueClient(argc, argv);
}
