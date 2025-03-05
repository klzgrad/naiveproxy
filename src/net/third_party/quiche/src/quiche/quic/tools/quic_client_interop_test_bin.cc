// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "quiche/quic/core/crypto/quic_client_session_cache.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/test_tools/quic_connection_peer.h"
#include "quiche/quic/test_tools/quic_session_peer.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_default_client.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, host, "",
                                "The IP or hostname to connect to.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, quic_version, "",
    "The QUIC version to use. Defaults to most recent IETF QUIC version.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, port, 0, "The port to connect to.");

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
  // 0-RTT data is being sent and acted on.
  kZeroRtt,
  // A RETRY packet was successfully processed.
  kRetry,
  // A handshake using a ClientHello that spans multiple packets completed
  // successfully.
  kQuantum,

  // Second row of features (anything else protocol-related)
  // We switched to a different port and the server migrated to it.
  kRebinding,
  // One endpoint can update keys and its peer responds correctly.
  kKeyUpdate,

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
    case Feature::kZeroRtt:
      return 'Z';
    case Feature::kRetry:
      return 'S';
    case Feature::kQuantum:
      return 'Q';
    case Feature::kRebinding:
      return 'B';
    case Feature::kKeyUpdate:
      return 'U';
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
  void AttemptResumption(QuicDefaultClient* client,
                         const std::string& authority);

  void AttemptRequest(QuicSocketAddress addr, std::string authority,
                      QuicServerId server_id, ParsedQuicVersion version,
                      bool test_version_negotiation, bool attempt_rebind,
                      bool attempt_multi_packet_chlo, bool attempt_key_update);

  // Constructs a Http2HeaderBlock containing the pseudo-headers needed to make
  // a GET request to "/" on the hostname |authority|.
  quiche::HttpHeaderBlock ConstructHeaderBlock(const std::string& authority);

  // Sends an HTTP request represented by |header_block| using |client|.
  void SendRequest(QuicDefaultClient* client,
                   const quiche::HttpHeaderBlock& header_block);

  void OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override {
    switch (frame.close_type) {
      case GOOGLE_QUIC_CONNECTION_CLOSE:
        QUIC_LOG(ERROR) << "Received unexpected GoogleQUIC connection close";
        break;
      case IETF_QUIC_TRANSPORT_CONNECTION_CLOSE:
        if (frame.wire_error_code == NO_IETF_QUIC_ERROR) {
          InsertFeature(Feature::kConnectionClose);
        } else {
          QUIC_LOG(ERROR) << "Received transport connection close "
                          << QuicIetfTransportErrorCodeString(
                                 static_cast<QuicIetfTransportErrorCodes>(
                                     frame.wire_error_code));
        }
        break;
      case IETF_QUIC_APPLICATION_CONNECTION_CLOSE:
        if (frame.wire_error_code == 0) {
          InsertFeature(Feature::kConnectionClose);
        } else {
          QUIC_LOG(ERROR) << "Received application connection close "
                          << frame.wire_error_code;
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

void QuicClientInteropRunner::AttemptResumption(QuicDefaultClient* client,
                                                const std::string& authority) {
  client->Disconnect();
  if (!client->Initialize()) {
    QUIC_LOG(ERROR) << "Failed to reinitialize client";
    return;
  }
  if (!client->Connect()) {
    return;
  }

  bool zero_rtt_attempt = !client->session()->OneRttKeysAvailable();

  quiche::HttpHeaderBlock header_block = ConstructHeaderBlock(authority);
  SendRequest(client, header_block);

  if (!client->session()->OneRttKeysAvailable()) {
    return;
  }

  if (static_cast<QuicCryptoClientStream*>(
          test::QuicSessionPeer::GetMutableCryptoStream(client->session()))
          ->IsResumption()) {
    InsertFeature(Feature::kResumption);
  }
  if (static_cast<QuicCryptoClientStream*>(
          test::QuicSessionPeer::GetMutableCryptoStream(client->session()))
          ->EarlyDataAccepted() &&
      zero_rtt_attempt && client->latest_response_code() != -1) {
    InsertFeature(Feature::kZeroRtt);
  }
}

void QuicClientInteropRunner::AttemptRequest(
    QuicSocketAddress addr, std::string authority, QuicServerId server_id,
    ParsedQuicVersion version, bool test_version_negotiation,
    bool attempt_rebind, bool attempt_multi_packet_chlo,
    bool attempt_key_update) {
  ParsedQuicVersionVector versions = {version};
  if (test_version_negotiation) {
    versions.insert(versions.begin(), QuicVersionReservedForNegotiation());
  }

  auto proof_verifier = std::make_unique<FakeProofVerifier>();
  auto session_cache = std::make_unique<QuicClientSessionCache>();
  QuicConfig config;
  QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(20);
  config.SetIdleNetworkTimeout(timeout);
  if (attempt_multi_packet_chlo) {
    // Make the ClientHello span multiple packets by adding a custom transport
    // parameter.
    config.SetDiscardLengthToSend(2000);
  }
  std::unique_ptr<QuicEventLoop> event_loop =
      GetDefaultEventLoop()->Create(QuicDefaultClock::Get());
  auto client = std::make_unique<QuicDefaultClient>(
      addr, server_id, versions, config, event_loop.get(),
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
                   /*test_version_negotiation=*/false, attempt_rebind,
                   attempt_multi_packet_chlo, attempt_key_update);
    return;
  }
  if (!client->session()->OneRttKeysAvailable()) {
    if (attempt_multi_packet_chlo) {
      // Failed to handshake with multi-packet client hello, retry without it.
      AttemptRequest(addr, authority, server_id, version,
                     test_version_negotiation, attempt_rebind,
                     /*attempt_multi_packet_chlo=*/false, attempt_key_update);
      return;
    }
    return;
  }
  InsertFeature(Feature::kHandshake);
  if (attempt_multi_packet_chlo) {
    InsertFeature(Feature::kQuantum);
  }

  quiche::HttpHeaderBlock header_block = ConstructHeaderBlock(authority);
  SendRequest(client.get(), header_block);

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
                         test_version_negotiation, /*attempt_rebind=*/false,
                         attempt_multi_packet_chlo, attempt_key_update);
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

    if (attempt_key_update) {
      if (connection->IsKeyUpdateAllowed()) {
        if (connection->InitiateKeyUpdate(
                KeyUpdateReason::kLocalForInteropRunner)) {
          client->SendRequestAndWaitForResponse(header_block, "", /*fin=*/true);
          if (!client->connected()) {
            // Key update does not work, retry without attempting it.
            AttemptRequest(addr, authority, server_id, version,
                           test_version_negotiation, attempt_rebind,
                           attempt_multi_packet_chlo,
                           /*attempt_key_update=*/false);
            return;
          }
          InsertFeature(Feature::kKeyUpdate);
        } else {
          QUIC_LOG(ERROR) << "Failed to initiate key update";
        }
      } else {
        QUIC_LOG(ERROR) << "Key update not allowed";
      }
    }
  }

  if (connection->connected()) {
    connection->CloseConnection(
        QUIC_NO_ERROR, "Graceful close",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    InsertFeature(Feature::kConnectionClose);
  }

  AttemptResumption(client.get(), authority);
}

quiche::HttpHeaderBlock QuicClientInteropRunner::ConstructHeaderBlock(
    const std::string& authority) {
  // Construct and send a request.
  quiche::HttpHeaderBlock header_block;
  header_block[":method"] = "GET";
  header_block[":scheme"] = "https";
  header_block[":authority"] = authority;
  header_block[":path"] = "/";
  return header_block;
}

void QuicClientInteropRunner::SendRequest(
    QuicDefaultClient* client, const quiche::HttpHeaderBlock& header_block) {
  client->set_store_response(true);
  client->SendRequestAndWaitForResponse(header_block, "", /*fin=*/true);

  QuicConnection* connection = client->session()->connection();
  if (connection == nullptr) {
    QUIC_LOG(ERROR) << "No QuicConnection object";
    return;
  }
  QuicConnectionStats client_stats = connection->GetStats();
  QuicSentPacketManager* sent_packet_manager =
      test::QuicConnectionPeer::GetSentPacketManager(connection);
  const bool received_forward_secure_ack =
      sent_packet_manager != nullptr &&
      sent_packet_manager->GetLargestAckedPacket(ENCRYPTION_FORWARD_SECURE)
          .IsInitialized();
  if (client_stats.stream_bytes_received > 0 && received_forward_secure_ack) {
    InsertFeature(Feature::kStreamData);
  }
}

std::set<Feature> ServerSupport(std::string dns_host, std::string url_host,
                                int port, ParsedQuicVersion version) {
  std::cout << "Attempting interop with version " << version << std::endl;

  // Build the client, and try to connect.
  QuicSocketAddress addr = tools::LookupAddress(dns_host, absl::StrCat(port));
  if (!addr.IsInitialized()) {
    QUIC_LOG(ERROR) << "Failed to resolve " << dns_host;
    return std::set<Feature>();
  }
  QuicServerId server_id(url_host, port);
  std::string authority = absl::StrCat(url_host, ":", port);

  QuicClientInteropRunner runner;

  runner.AttemptRequest(addr, authority, server_id, version,
                        /*test_version_negotiation=*/true,
                        /*attempt_rebind=*/true,
                        /*attempt_multi_packet_chlo=*/true,
                        /*attempt_key_update=*/true);

  return runner.features();
}

}  // namespace quic

int main(int argc, char* argv[]) {
  quiche::QuicheSystemEventLoop event_loop("quic_client");
  const char* usage = "Usage: quic_client_interop_test [options] [url]";

  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (args.size() > 1) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    exit(1);
  }
  std::string dns_host = quiche::GetQuicheCommandLineFlag(FLAGS_host);
  std::string url_host = "";
  int port = quiche::GetQuicheCommandLineFlag(FLAGS_port);

  if (!args.empty()) {
    quic::QuicUrl url(args[0], "https");
    url_host = url.host();
    if (dns_host.empty()) {
      dns_host = url_host;
    }
    if (port == 0) {
      port = url.port();
    }
  }
  if (port == 0) {
    port = 443;
  }
  if (dns_host.empty()) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    exit(1);
  }
  if (url_host.empty()) {
    url_host = dns_host;
  }

  // Pick QUIC version to use.
  quic::ParsedQuicVersion version = quic::UnsupportedQuicVersion();
  std::string quic_version_string =
      quiche::GetQuicheCommandLineFlag(FLAGS_quic_version);
  if (!quic_version_string.empty()) {
    version = quic::ParseQuicVersionString(quic_version_string);
  } else {
    for (const quic::ParsedQuicVersion& vers : quic::AllSupportedVersions()) {
      // Use the most recent IETF QUIC version.
      if (vers.HasIetfQuicFrames() && vers.UsesHttp3() && vers.UsesTls()) {
        version = vers;
        break;
      }
    }
  }
  QUICHE_CHECK(version.IsKnown());
  QuicEnableVersion(version);

  auto supported_features =
      quic::ServerSupport(dns_host, url_host, port, version);
  std::cout << "Results for " << url_host << ":" << port << std::endl;
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
