// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is reponsible for the masque_client binary. It allows testing
// our MASQUE client code by connecting to a MASQUE proxy and then sending
// HTTP/3 requests to web servers tunnelled over that MASQUE connection.
// e.g.: masque_client $PROXY_HOST:$PROXY_PORT $URL1 $URL2

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "url/third_party/mozilla/url_parse.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/masque/masque_client.h"
#include "quiche/quic/masque/masque_client_tools.h"
#include "quiche/quic/masque/masque_encapsulated_client.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, masque_mode, "",
    "Allows setting MASQUE mode, currently only valid value is \"open\".");

namespace quic {

namespace {

int RunMasqueClient(int argc, char* argv[]) {
  quiche::QuicheSystemEventLoop system_event_loop("masque_client");
  const char* usage = "Usage: masque_client [options] <url>";

  // The first non-flag argument is the URI template of the MASQUE server.
  // All subsequent ones are interpreted as URLs to fetch via the MASQUE server.
  // Note that the URI template expansion currently only supports string
  // replacement of {target_host} and {target_port}, not
  // {?target_host,target_port}.
  std::vector<std::string> urls =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (urls.empty()) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  const bool disable_certificate_verification =
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification);
  std::unique_ptr<QuicEventLoop> event_loop =
      GetDefaultEventLoop()->Create(QuicDefaultClock::Get());

  std::string uri_template = urls[0];
  if (!absl::StrContains(uri_template, '/')) {
    // If an authority is passed in instead of a URI template, use the default
    // URI template.
    uri_template =
        absl::StrCat("https://", uri_template,
                     "/.well-known/masque/udp/{target_host}/{target_port}/");
  }
  url::Parsed parsed_uri_template;
  url::ParseStandardURL(uri_template.c_str(), uri_template.length(),
                        &parsed_uri_template);
  if (!parsed_uri_template.scheme.is_nonempty() ||
      !parsed_uri_template.host.is_nonempty() ||
      !parsed_uri_template.path.is_nonempty()) {
    std::cerr << "Failed to parse MASQUE URI template \"" << urls[0] << "\""
              << std::endl;
    return 1;
  }
  std::string host = uri_template.substr(parsed_uri_template.host.begin,
                                         parsed_uri_template.host.len);
  std::unique_ptr<ProofVerifier> proof_verifier;
  if (disable_certificate_verification) {
    proof_verifier = std::make_unique<FakeProofVerifier>();
  } else {
    proof_verifier = CreateDefaultProofVerifier(host);
  }
  MasqueMode masque_mode = MasqueMode::kOpen;
  std::string mode_string = quiche::GetQuicheCommandLineFlag(FLAGS_masque_mode);
  if (!mode_string.empty() && mode_string != "open") {
    std::cerr << "Invalid masque_mode \"" << mode_string << "\"" << std::endl;
    return 1;
  }
  std::unique_ptr<MasqueClient> masque_client = MasqueClient::Create(
      uri_template, masque_mode, event_loop.get(), std::move(proof_verifier));
  if (masque_client == nullptr) {
    return 1;
  }

  std::cerr << "MASQUE is connected " << masque_client->connection_id()
            << " in " << masque_mode << " mode" << std::endl;

  for (size_t i = 1; i < urls.size(); ++i) {
    if (!tools::SendEncapsulatedMasqueRequest(
            masque_client.get(), event_loop.get(), urls[i],
            disable_certificate_verification)) {
      return 1;
    }
  }

  return 0;
}

}  // namespace

}  // namespace quic

int main(int argc, char* argv[]) { return quic::RunMasqueClient(argc, argv); }
