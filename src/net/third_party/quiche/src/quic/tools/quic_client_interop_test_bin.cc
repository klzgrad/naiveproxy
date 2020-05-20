// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_epoll.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_system_event_loop.h"
#include "net/quic/platform/impl/quic_epoll_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/simple_session_cache.h"
#include "net/third_party/quiche/src/quic/tools/fake_proof_verifier.h"
#include "net/third_party/quiche/src/quic/tools/quic_client.h"
#include "net/third_party/quiche/src/quic/tools/quic_url.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

DEFINE_QUIC_COMMAND_LINE_FLAG(std::string,
                              host,
                              "",
                              "The IP or hostname to connect to.");

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t, port, 0, "The port to connect to.");

namespace quic {

enum class Feature {
  // First row of features ("table stakes")
  // A version negotiation response is elicited and acted on.
  kVersionNegotiation,
  // The handshake completes successfully.
  kHandshake,
  // Stream data is being exchanged and ACK'ed.
  kStreamData,
  // The connection close procedcure completes with a zero error code.
  kConnectionClose,
  // The connection was established using TLS resumption.
  kResumption,
  // A RETRY packet was successfully processed.
  kRetry,

  // Second row of features (anything else protocol-related)
  // We switched to a different port and the server migrated to it.
  kRebinding,

  // Third row of features (H3 tests)
  // An H3 transaction succeeded.
  kHttp3,
  // One or both endpoints insert entries into dynamic table and subsequenly
  // reference them from header blocks.
  kDynamicEntryReferenced,
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
    case Feature::kResumption:
      return 'R';
    case Feature::kRetry:
      return 'S';
    case Feature::kRebinding:
      return 'B';
    case Feature::kHttp3:
      return '3';
    case Feature::kDynamicEntryReferenced:
      return 'd';
  }
}

class QuicClientInteropRunner : QuicConnectionDebugVisitor {
 public:
  QuicClientInteropRunner() {}

  void InsertFeature(Feature feature) { features_.insert(feature); }

  std::set<Feature> features() const { return features_; }

  // Attempts a resumption using |client| by disconnecting and reconnecting. If
  // resumption is successful, |features_| is modified to add
  // Feature::kResumption to it, otherwise it is left unmodified.
  void AttemptResumption(QuicClient* client);

  void AttemptRequest(QuicSocketAddress addr,
                      std::string authority,
                      QuicServerId server_id,
                      ParsedQuicVersion version,
                      bool test_version_negotiation,
                      bool attempt_rebind);

  void OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override {
    switch (frame.close_type) {
      case GOOGLE_QUIC_CONNECTION_CLOSE:
        QUIC_LOG(ERROR) << "Received unexpected GoogleQUIC connection close";
        break;
      case IETF_QUIC_TRANSPORT_CONNECTION_CLOSE:
        if (frame.transport_error_code == NO_IETF_QUIC_ERROR) {
          InsertFeature(Feature::kConnectionClose);
        } else {
          QUIC_LOG(ERROR) << "Received transport connection close "
                          << QuicIetfTransportErrorCodeString(
                                 frame.transport_error_code);
        }
        break;
      case IETF_QUIC_APPLICATION_CONNECTION_CLOSE:
        if (frame.application_error_code == 0) {
          InsertFeature(Feature::kConnectionClose);
        } else {
          QUIC_LOG(ERROR) << "Received application connection close "
                          << frame.application_error_code;
        }
        break;
    }
  }

  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& /*packet*/) override {
    InsertFeature(Feature::kVersionNegotiation);
  }

 private:
  std::set<Feature> features_;
};

void QuicClientInteropRunner::AttemptResumption(QuicClient* client) {
  client->Disconnect();
  if (!client->Initialize()) {
    QUIC_LOG(ERROR) << "Failed to reinitialize client";
    return;
  }
  if (!client->Connect() || !client->session()->OneRttKeysAvailable()) {
    return;
  }
  if (static_cast<QuicCryptoClientStream*>(
          test::QuicSessionPeer::GetMutableCryptoStream(client->session()))
          ->IsResumption()) {
    InsertFeature(Feature::kResumption);
  }
}

void QuicClientInteropRunner::AttemptRequest(QuicSocketAddress addr,
                                             std::string authority,
                                             QuicServerId server_id,
                                             ParsedQuicVersion version,
                                             bool test_version_negotiation,
                                             bool attempt_rebind) {
  ParsedQuicVersionVector versions = {version};
  if (test_version_negotiation) {
    versions.insert(versions.begin(), QuicVersionReservedForNegotiation());
  }

  auto proof_verifier = std::make_unique<FakeProofVerifier>();
  auto session_cache = std::make_unique<test::SimpleSessionCache>();
  QuicEpollServer epoll_server;
  QuicEpollClock epoll_clock(&epoll_server);
  QuicConfig config;
  QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(20);
  config.SetIdleNetworkTimeout(timeout, timeout);
  auto client = std::make_unique<QuicClient>(
      addr, server_id, versions, config, &epoll_server,
      std::move(proof_verifier), std::move(session_cache));
  client->set_connection_debug_visitor(this);
  if (!client->Initialize()) {
    QUIC_LOG(ERROR) << "Failed to initialize client";
    return;
  }
  const bool connect_result = client->Connect();
  QuicConnection* connection = client->session()->connection();
  if (connection == nullptr) {
    QUIC_LOG(ERROR) << "No QuicConnection object";
    return;
  }
  QuicConnectionStats client_stats = connection->GetStats();
  if (client_stats.retry_packet_processed) {
    InsertFeature(Feature::kRetry);
  }
  if (test_version_negotiation && connection->version() == version) {
    InsertFeature(Feature::kVersionNegotiation);
  }
  if (test_version_negotiation && !connect_result) {
    // Failed to negotiate version, retry without version negotiation.
    AttemptRequest(addr, authority, server_id, version,
                   /*test_version_negotiation=*/false, attempt_rebind);
    return;
  }
  if (!client->session()->OneRttKeysAvailable()) {
    return;
  }
  InsertFeature(Feature::kHandshake);

  // Construct and send a request.
  spdy::SpdyHeaderBlock header_block;
  header_block[":method"] = "GET";
  header_block[":scheme"] = "https";
  header_block[":authority"] = authority;
  header_block[":path"] = "/";
  client->set_store_response(true);
  client->SendRequestAndWaitForResponse(header_block, "", /*fin=*/true);

  client_stats = connection->GetStats();
  QuicSentPacketManager* sent_packet_manager =
      test::QuicConnectionPeer::GetSentPacketManager(connection);
  const bool received_forward_secure_ack =
      sent_packet_manager != nullptr &&
      sent_packet_manager->GetLargestAckedPacket(ENCRYPTION_FORWARD_SECURE)
          .IsInitialized();
  if (client_stats.stream_bytes_received > 0 && received_forward_secure_ack) {
    InsertFeature(Feature::kStreamData);
  }

  if (!client->connected()) {
    return;
  }

  if (client->latest_response_code() != -1) {
    InsertFeature(Feature::kHttp3);

    if (client->client_session()->dynamic_table_entry_referenced()) {
      InsertFeature(Feature::kDynamicEntryReferenced);
    }

    if (attempt_rebind) {
      // Now make a second request after switching to a different client port.
      if (client->ChangeEphemeralPort()) {
        client->SendRequestAndWaitForResponse(header_block, "", /*fin=*/true);
        if (!client->connected()) {
          // Rebinding does not work, retry without attempting it.
          AttemptRequest(addr, authority, server_id, version,
                         test_version_negotiation, /*attempt_rebind=*/false);
          return;
        }
        InsertFeature(Feature::kRebinding);

        if (client->client_session()->dynamic_table_entry_referenced()) {
          InsertFeature(Feature::kDynamicEntryReferenced);
        }
      } else {
        QUIC_LOG(ERROR) << "Failed to change ephemeral port";
      }
    }
  }

  if (connection->connected()) {
    connection->CloseConnection(
        QUIC_NO_ERROR, "Graceful close",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    InsertFeature(Feature::kConnectionClose);
  }

  AttemptResumption(client.get());
}

std::set<Feature> ServerSupport(std::string host, int port) {
  // Enable IETF version support.
  QuicVersionInitializeSupportForIetfDraft();
  ParsedQuicVersion version = UnsupportedQuicVersion();
  for (const ParsedQuicVersion& vers : AllSupportedVersions()) {
    // Find the first version that supports IETF QUIC.
    if (vers.HasIetfQuicFrames() &&
        vers.handshake_protocol == quic::PROTOCOL_TLS1_3) {
      version = vers;
      break;
    }
  }
  CHECK_NE(version.transport_version, QUIC_VERSION_UNSUPPORTED);
  QuicEnableVersion(version);

  // Build the client, and try to connect.
  QuicSocketAddress addr =
      tools::LookupAddress(host, quiche::QuicheStrCat(port));
  if (!addr.IsInitialized()) {
    QUIC_LOG(ERROR) << "Failed to resolve " << host;
    return std::set<Feature>();
  }
  QuicServerId server_id(host, port, false);
  std::string authority = quiche::QuicheStrCat(host, ":", port);

  QuicClientInteropRunner runner;

  runner.AttemptRequest(addr, authority, server_id, version,
                        /*test_version_negotiation=*/true,
                        /*attempt_rebind=*/true);

  return runner.features();
}

}  // namespace quic

int main(int argc, char* argv[]) {
  QuicSystemEventLoop event_loop("quic_client");
  const char* usage = "Usage: quic_client_interop_test [options] [url]";

  std::vector<std::string> args =
      quic::QuicParseCommandLineFlags(usage, argc, argv);
  if (args.size() > 1) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    exit(1);
  }
  std::string host = GetQuicFlag(FLAGS_host);
  int port = GetQuicFlag(FLAGS_port);

  if (!args.empty()) {
    quic::QuicUrl url(args[0], "https");
    if (host.empty()) {
      host = url.host();
    }
    if (port == 0) {
      port = url.port();
    }
  }
  if (port == 0) {
    port = 443;
  }
  if (host.empty()) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    exit(1);
  }

  auto supported_features = quic::ServerSupport(host, port);
  std::cout << "Results for " << host << ":" << port << std::endl;
  int current_row = 1;
  for (auto feature : supported_features) {
    if (current_row < 2 && feature >= quic::Feature::kRebinding) {
      std::cout << std::endl;
      current_row = 2;
    }
    if (current_row < 3 && feature >= quic::Feature::kHttp3) {
      std::cout << std::endl;
      current_row = 3;
    }
    std::cout << MatrixLetter(feature);
  }
  std::cout << std::endl;
}
