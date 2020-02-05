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

// Attempts a resumption using |client| by disconnecting and reconnecting. If
// resumption is successful, |features| is modified to add Feature::kResumption
// to it, otherwise it is left unmodified.
void AttemptResumption(QuicClient* client, std::set<Feature>* features) {
  client->Disconnect();
  if (!client->Initialize()) {
    QUIC_LOG(ERROR) << "Failed to reinitialize client";
    return;
  }
  if (!client->Connect() || !client->session()->IsCryptoHandshakeConfirmed()) {
    return;
  }
  if (static_cast<QuicCryptoClientStream*>(
          test::QuicSessionPeer::GetMutableCryptoStream(client->session()))
          ->IsResumption()) {
    features->insert(Feature::kResumption);
  }
}

std::set<Feature> AttemptRequest(QuicSocketAddress addr,
                                 std::string authority,
                                 QuicServerId server_id,
                                 bool test_version_negotiation,
                                 bool attempt_rebind) {
  ParsedQuicVersion version(PROTOCOL_TLS1_3, QUIC_VERSION_99);
  ParsedQuicVersionVector versions = {version};
  if (test_version_negotiation) {
    versions.insert(versions.begin(), QuicVersionReservedForNegotiation());
  }

  std::set<Feature> features;
  auto proof_verifier = std::make_unique<FakeProofVerifier>();
  auto session_cache = std::make_unique<test::SimpleSessionCache>();
  QuicEpollServer epoll_server;
  QuicEpollClock epoll_clock(&epoll_server);
  auto client = std::make_unique<QuicClient>(
      addr, server_id, versions, &epoll_server, std::move(proof_verifier),
      std::move(session_cache));
  if (!client->Initialize()) {
    QUIC_LOG(ERROR) << "Failed to initialize client";
    return features;
  }
  const bool connect_result = client->Connect();
  QuicConnection* connection = client->session()->connection();
  if (connection != nullptr) {
    QuicConnectionStats client_stats = connection->GetStats();
    if (client_stats.retry_packet_processed) {
      features.insert(Feature::kRetry);
    }
    if (test_version_negotiation && connection->version() == version) {
      features.insert(Feature::kVersionNegotiation);
    }
  }
  if (test_version_negotiation && !connect_result) {
    // Failed to negotiate version, retry without version negotiation.
    std::set<Feature> features_without_version_negotiation =
        AttemptRequest(addr, authority, server_id,
                       /*test_version_negotiation=*/false, attempt_rebind);

    features.insert(features_without_version_negotiation.begin(),
                    features_without_version_negotiation.end());
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

  const QuicTime request_start_time = epoll_clock.Now();
  static const auto request_timeout = QuicTime::Delta::FromSeconds(20);
  bool request_timed_out = false;
  while (client->WaitForEvents()) {
    if (epoll_clock.Now() - request_start_time >= request_timeout) {
      QUIC_LOG(ERROR) << "Timed out waiting for HTTP response";
      request_timed_out = true;
      break;
    }
  }

  if (connection != nullptr) {
    QuicConnectionStats client_stats = connection->GetStats();
    QuicSentPacketManager* sent_packet_manager =
        test::QuicConnectionPeer::GetSentPacketManager(connection);
    const bool received_forward_secure_ack =
        sent_packet_manager != nullptr &&
        sent_packet_manager->GetLargestAckedPacket(ENCRYPTION_FORWARD_SECURE)
            .IsInitialized();
    if (client_stats.stream_bytes_received > 0 && received_forward_secure_ack) {
      features.insert(Feature::kStreamData);
    }
  }

  if (request_timed_out || !client->connected()) {
    return features;
  }

  if (client->latest_response_code() != -1) {
    features.insert(Feature::kHttp3);

    if (client->client_session()->dynamic_table_entry_referenced()) {
      features.insert(Feature::kDynamicEntryReferenced);
    }

    if (attempt_rebind) {
      // Now make a second request after switching to a different client port.
      if (client->ChangeEphemeralPort()) {
        client->SendRequest(header_block, "", /*fin=*/true);

        const QuicTime second_request_start_time = epoll_clock.Now();
        while (client->WaitForEvents()) {
          if (epoll_clock.Now() - second_request_start_time >=
              request_timeout) {
            // Rebinding does not work, retry without attempting it.
            std::set<Feature> features_without_rebind = AttemptRequest(
                addr, authority, server_id, test_version_negotiation,
                /*attempt_rebind=*/false);
            features.insert(features_without_rebind.begin(),
                            features_without_rebind.end());
            return features;
          }
        }
        features.insert(Feature::kRebinding);

        if (client->client_session()->dynamic_table_entry_referenced()) {
          features.insert(Feature::kDynamicEntryReferenced);
        }
      } else {
        QUIC_LOG(ERROR) << "Failed to change ephemeral port";
      }
    }
  }

  if (connection != nullptr && connection->connected()) {
    test::QuicConnectionPeer::SendConnectionClosePacket(
        connection, QUIC_NO_ERROR, "Graceful close");
    const QuicTime close_start_time = epoll_clock.Now();
    static const auto close_timeout = QuicTime::Delta::FromSeconds(10);
    while (client->connected()) {
      client->epoll_network_helper()->RunEventLoop();
      if (epoll_clock.Now() - close_start_time >= close_timeout) {
        QUIC_LOG(ERROR) << "Timed out waiting for connection close";
        AttemptResumption(client.get(), &features);
        return features;
      }
    }
    const QuicErrorCode received_error = client->session()->error();
    if (received_error == QUIC_NO_ERROR ||
        received_error == QUIC_PUBLIC_RESET) {
      features.insert(Feature::kConnectionClose);
    } else {
      QUIC_LOG(ERROR) << "Received error " << client->session()->error() << " "
                      << client->session()->error_details();
    }
  }

  AttemptResumption(client.get(), &features);
  return features;
}

std::set<Feature> ServerSupport(std::string host, int port) {
  // Enable IETF version support.
  QuicVersionInitializeSupportForIetfDraft();
  QuicEnableVersion(ParsedQuicVersion(PROTOCOL_TLS1_3, QUIC_VERSION_99));

  // Build the client, and try to connect.
  QuicSocketAddress addr = tools::LookupAddress(host, QuicStrCat(port));
  if (!addr.IsInitialized()) {
    QUIC_LOG(ERROR) << "Failed to resolve " << host;
    return std::set<Feature>();
  }
  QuicServerId server_id(host, port, false);
  std::string authority = QuicStrCat(host, ":", port);

  return AttemptRequest(addr, authority, server_id,
                        /*test_version_negotiation=*/true,
                        /*attempt_rebind=*/true);
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
