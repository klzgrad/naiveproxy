// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_epoll.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_system_event_loop.h"
#include "net/third_party/quiche/src/quic/tools/fake_proof_verifier.h"
#include "net/third_party/quiche/src/quic/tools/quic_client.h"

DEFINE_QUIC_COMMAND_LINE_FLAG(std::string,
                              host,
                              "",
                              "The IP or hostname to connect to.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t, port, 0, "The port to connect to.");

namespace quic {

enum class Feature {
  // A version negotiation response is elicited and acted on.
  kVersionNegotiation,
  // The handshake completes successfully.
  kHandshake,
  // Stream data is being exchanged and ACK'ed.
  kStreamData,
  // The connection close procedcure completes with a zero error code.
  kConnectionClose,
  // An H3 transaction succeeded.
  kHttp3,
  // TODO(nharper): Add Retry to list of tested features.
};

char MatrixLetter(Feature f) {
  switch (f) {
    case Feature::kVersionNegotiation:
      return 'V';
    case Feature::kHandshake:
      return 'H';
    case Feature::kStreamData:
      return 'D';
    case Feature::kConnectionClose:
      return 'C';
    case Feature::kHttp3:
      return '3';
  }
}

std::set<Feature> AttemptRequest(QuicSocketAddress addr,
                                 std::string authority,
                                 QuicServerId server_id,
                                 ParsedQuicVersionVector versions) {
  std::set<Feature> features;
  auto proof_verifier = QuicMakeUnique<FakeProofVerifier>();
  QuicEpollServer epoll_server;
  auto client = QuicMakeUnique<QuicClient>(
      addr, server_id, versions, &epoll_server, std::move(proof_verifier));
  if (!client->Initialize()) {
    return features;
  }
  if (!client->Connect()) {
    QuicErrorCode error = client->session()->error();
    if (error == QUIC_INVALID_VERSION) {
      // QuicFramer::ProcessPacket returns RaiseError(QUIC_INVALID_VERSION) if
      // it receives a packet containing a version in the header that is not our
      // version. It might be possible that we didn't actually process a VN
      // packet here.
      features.insert(Feature::kVersionNegotiation);
      return features;
    }
    return features;
  }
  if (!client->session()->IsCryptoHandshakeConfirmed()) {
    return features;
  }
  features.insert(Feature::kHandshake);

  // Construct and send a request.
  spdy::SpdyHeaderBlock header_block;
  header_block[":method"] = "GET";
  header_block[":scheme"] = "https";
  header_block[":authority"] = authority;
  header_block[":path"] = "/";
  client->set_store_response(true);
  client->SendRequest(header_block, "", /*fin=*/true);

  // TODO(nharper): After some period of time, time out and don't report
  // success.
  while (client->WaitForEvents()) {
  }

  if (!client->connected()) {
    return features;
  }

  if (client->latest_response_code() != -1) {
    features.insert(Feature::kHttp3);
  }

  // TODO(nharper): Properly check that we actually sent stream data and
  // received ACKs for it.
  features.insert(Feature::kStreamData);
  // TODO(nharper): Check that we sent/received (which one?) a CONNECTION_CLOSE
  // with error code 0.
  features.insert(Feature::kConnectionClose);
  return features;
}

std::set<Feature> ServerSupport(std::string host, int port) {
  // Configure version list.
  QuicVersionInitializeSupportForIetfDraft();
  ParsedQuicVersion version =
      ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_99);
  ParsedQuicVersionVector versions = {version};
  QuicEnableVersion(version);

  // Build the client, and try to connect.
  QuicSocketAddress addr = tools::LookupAddress(host, QuicStrCat(port));
  QuicServerId server_id(host, port, false);
  std::string authority = QuicStrCat(host, ":", port);

  ParsedQuicVersionVector versions_with_negotiation = versions;
  versions_with_negotiation.insert(versions_with_negotiation.begin(),
                                   QuicVersionReservedForNegotiation());
  auto supported_features =
      AttemptRequest(addr, authority, server_id, versions_with_negotiation);
  if (!supported_features.empty()) {
    supported_features.insert(Feature::kVersionNegotiation);
  } else {
    supported_features = AttemptRequest(addr, authority, server_id, versions);
  }
  return supported_features;
}

}  // namespace quic

int main(int argc, char* argv[]) {
  QuicSystemEventLoop event_loop("quic_client");
  const char* usage = "Usage: quic_client_interop_test [options]";

  std::vector<std::string> args =
      quic::QuicParseCommandLineFlags(usage, argc, argv);
  if (!args.empty()) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    exit(1);
  }
  std::string host = GetQuicFlag(FLAGS_host);
  int port = GetQuicFlag(FLAGS_port);
  if (host.empty() || port == 0) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    exit(1);
  }

  auto supported_features = quic::ServerSupport(host, port);
  std::cout << "Supported features: ";
  for (auto feature : supported_features) {
    std::cout << MatrixLetter(feature);
  }
  std::cout << std::endl;
}
