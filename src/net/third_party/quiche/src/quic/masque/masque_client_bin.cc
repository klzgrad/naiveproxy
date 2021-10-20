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

DEFINE_QUIC_COMMAND_LINE_FLAG(bool,
                              disable_certificate_verification,
                              false,
                              "If true, don't verify the server certificate.");

DEFINE_QUIC_COMMAND_LINE_FLAG(std::string,
                              masque_mode,
                              "",
                              "Allows setting MASQUE mode, valid values are "
                              "open and legacy. Defaults to open.");

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
    std::cerr << "Failed to parse MASQUE server address \"" << urls[0] << "\""
              << std::endl;
    return 1;
  }
  std::unique_ptr<ProofVerifier> proof_verifier;
  if (disable_certificate_verification) {
    proof_verifier = std::make_unique<FakeProofVerifier>();
  } else {
    proof_verifier = CreateDefaultProofVerifier(masque_url.host());
  }
  MasqueMode masque_mode = MasqueMode::kOpen;
  std::string mode_string = GetQuicFlag(FLAGS_masque_mode);
  if (mode_string == "legacy") {
    masque_mode = MasqueMode::kLegacy;
  } else if (!mode_string.empty() && mode_string != "open") {
    std::cerr << "Invalid masque_mode \"" << mode_string << "\"" << std::endl;
    return 1;
  }
  std::unique_ptr<MasqueEpollClient> masque_client = MasqueEpollClient::Create(
      masque_url.host(), masque_url.port(), masque_mode, &epoll_server,
      std::move(proof_verifier));
  if (masque_client == nullptr) {
    return 1;
  }

  std::cerr << "MASQUE is connected " << masque_client->connection_id()
            << " in " << masque_mode << " mode" << std::endl;

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
