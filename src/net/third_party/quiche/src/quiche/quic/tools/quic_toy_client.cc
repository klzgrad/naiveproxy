// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A binary wrapper for QuicClient.
// Connects to a host using QUIC, sends a request to the provided URL, and
// displays the response.
//
// Some usage examples:
//
// Standard request/response:
//   quic_client www.google.com
//   quic_client www.google.com --quiet
//   quic_client www.google.com --port=443
//
// Use a specific version:
//   quic_client www.google.com --quic_version=23
//
// Send a POST instead of a GET:
//   quic_client www.google.com --body="this is a POST body"
//
// Append additional headers to the request:
//   quic_client www.google.com --headers="Header-A: 1234; Header-B: 5678"
//
// Connect to a host different to the URL being requested:
//   quic_client mail.google.com --host=www.google.com
//
// Connect to a specific IP:
//   IP=`dig www.google.com +short | head -1`
//   quic_client www.google.com --host=${IP}
//
// Send repeated requests and change ephemeral port between requests
//   quic_client www.google.com --num_requests=10
//
// Try to connect to a host which does not speak QUIC:
//   quic_client www.example.com
//
// This tool is available as a built binary at:
// /google/data/ro/teams/quic/tools/quic_client
// After submitting changes to this file, you will need to follow the
// instructions at go/quic_client_binary_update

#include "quiche/quic/tools/quic_toy_client.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_client_session_cache.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace {

using quiche::QuicheTextUtils;

}  // namespace

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, host, "",
    "The IP or hostname to connect to. If not provided, the host "
    "will be derived from the provided URL.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, port, 0, "The port to connect to.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, ip_version_for_host_lookup, "",
                                "Only used if host address lookup is needed. "
                                "4=ipv4; 6=ipv6; otherwise=don't care.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, body, "",
                                "If set, send a POST with this body.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, body_hex, "",
    "If set, contents are converted from hex to ascii, before "
    "sending as body of a POST. e.g. --body_hex=\"68656c6c6f\"");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, headers, "",
    "A semicolon separated list of key:value pairs to "
    "add to request headers.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(bool, quiet, false,
                                "Set to true for a quieter output experience.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, output_resolved_server_address, false,
    "Set to true to print the resolved IP of the server.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, quic_version, "",
    "QUIC version to speak, e.g. 21. If not set, then all available "
    "versions are offered in the handshake. Also supports wire versions "
    "such as Q043 or T099.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, connection_options, "",
    "Connection options as ASCII tags separated by commas, "
    "e.g. \"ABCD,EFGH\"");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, client_connection_options, "",
    "Client connection options as ASCII tags separated by commas, "
    "e.g. \"ABCD,EFGH\"");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, version_mismatch_ok, false,
    "If true, a version mismatch in the handshake is not considered a "
    "failure. Useful for probing a server to determine if it speaks "
    "any version of QUIC.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, force_version_negotiation, false,
    "If true, start by proposing a version that is reserved for version "
    "negotiation.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, multi_packet_chlo, false,
    "If true, add a transport parameter to make the ClientHello span two "
    "packets. Only works with QUIC+TLS.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, redirect_is_success, true,
    "If true, an HTTP response code of 3xx is considered to be a "
    "successful response, otherwise a failure.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, initial_mtu, 0,
                                "Initial MTU of the connection.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    int32_t, num_requests, 1,
    "How many sequential requests to make on a single connection.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(bool, ignore_errors, false,
                                "If true, ignore connection/response errors "
                                "and send all num_requests anyway.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, default_client_cert, "",
    "The path to the file containing PEM-encoded client default certificate to "
    "be sent to the server, if server requested client certs.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, default_client_cert_key, "",
    "The path to the file containing PEM-encoded private key of the client's "
    "default certificate for signing, if server requested client certs.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, drop_response_body, false,
    "If true, drop response body immediately after it is received.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_port_changes, false,
    "If true, do not change local port after each request.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(bool, one_connection_per_request, false,
                                "If true, close the connection after each "
                                "request. This allows testing 0-RTT.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, server_connection_id, "",
    "If non-empty, the client will use the given server connection id for all "
    "connections. The flag value is the hex-string of the on-wire connection id"
    " bytes, e.g. '--server_connection_id=0123456789abcdef'.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    int32_t, server_connection_id_length, -1,
    "Length of the server connection ID used. This flag has no effects if "
    "--server_connection_id is non-empty.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, client_connection_id_length, -1,
                                "Length of the client connection ID used.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(int32_t, max_time_before_crypto_handshake_ms,
                                10000,
                                "Max time to wait before handshake completes.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    int32_t, max_inbound_header_list_size, 128 * 1024,
    "Max inbound header list size. 0 means default.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, interface_name, "",
                                "Interface name to bind QUIC UDP sockets to.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, signing_algorithms_pref, "",
    "A textual specification of a set of signature algorithms that can be "
    "accepted by boring SSL SSL_set1_sigalgs_list()");

namespace quic {
namespace {

// Creates a ClientProofSource which only contains a default client certificate.
// Return nullptr for failure.
std::unique_ptr<ClientProofSource> CreateTestClientProofSource(
    absl::string_view default_client_cert_file,
    absl::string_view default_client_cert_key_file) {
  std::ifstream cert_stream(std::string{default_client_cert_file},
                            std::ios::binary);
  std::vector<std::string> certs =
      CertificateView::LoadPemFromStream(&cert_stream);
  if (certs.empty()) {
    std::cerr << "Failed to load client certs." << std::endl;
    return nullptr;
  }

  std::ifstream key_stream(std::string{default_client_cert_key_file},
                           std::ios::binary);
  std::unique_ptr<CertificatePrivateKey> private_key =
      CertificatePrivateKey::LoadPemFromStream(&key_stream);
  if (private_key == nullptr) {
    std::cerr << "Failed to load client cert key." << std::endl;
    return nullptr;
  }

  auto proof_source = std::make_unique<DefaultClientProofSource>();
  proof_source->AddCertAndKey(
      {"*"},
      quiche::QuicheReferenceCountedPointer<ClientProofSource::Chain>(
          new ClientProofSource::Chain(certs)),
      std::move(*private_key));

  return proof_source;
}

}  // namespace

QuicToyClient::QuicToyClient(ClientFactory* client_factory)
    : client_factory_(client_factory) {}

int QuicToyClient::SendRequestsAndPrintResponses(
    std::vector<std::string> urls) {
  QuicUrl url(urls[0], "https");
  std::string host = quiche::GetQuicheCommandLineFlag(FLAGS_host);
  if (host.empty()) {
    host = url.host();
  }
  int port = quiche::GetQuicheCommandLineFlag(FLAGS_port);
  if (port == 0) {
    port = url.port();
  }

  quic::ParsedQuicVersionVector versions = quic::CurrentSupportedVersions();

  std::string quic_version_string =
      quiche::GetQuicheCommandLineFlag(FLAGS_quic_version);
  if (!quic_version_string.empty()) {
    versions = quic::ParseQuicVersionVectorString(quic_version_string);
  }

  if (versions.empty()) {
    std::cerr << "No known version selected." << std::endl;
    return 1;
  }

  for (const quic::ParsedQuicVersion& version : versions) {
    quic::QuicEnableVersion(version);
  }

  if (quiche::GetQuicheCommandLineFlag(FLAGS_force_version_negotiation)) {
    versions.insert(versions.begin(),
                    quic::QuicVersionReservedForNegotiation());
  }

  const int32_t num_requests(
      quiche::GetQuicheCommandLineFlag(FLAGS_num_requests));
  std::unique_ptr<quic::ProofVerifier> proof_verifier;
  if (quiche::GetQuicheCommandLineFlag(
          FLAGS_disable_certificate_verification)) {
    proof_verifier = std::make_unique<FakeProofVerifier>();
  } else {
    proof_verifier = quic::CreateDefaultProofVerifier(url.host());
  }
  std::unique_ptr<quic::SessionCache> session_cache;
  if (num_requests > 1 &&
      quiche::GetQuicheCommandLineFlag(FLAGS_one_connection_per_request)) {
    session_cache = std::make_unique<QuicClientSessionCache>();
  }

  QuicConfig config;
  std::string connection_options_string =
      quiche::GetQuicheCommandLineFlag(FLAGS_connection_options);
  if (!connection_options_string.empty()) {
    config.SetConnectionOptionsToSend(
        ParseQuicTagVector(connection_options_string));
  }
  std::string client_connection_options_string =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_connection_options);
  if (!client_connection_options_string.empty()) {
    config.SetClientConnectionOptions(
        ParseQuicTagVector(client_connection_options_string));
  }
  if (quiche::GetQuicheCommandLineFlag(FLAGS_multi_packet_chlo)) {
    // Make the ClientHello span multiple packets by adding a custom transport
    // parameter.
    constexpr auto kCustomParameter =
        static_cast<TransportParameters::TransportParameterId>(0x173E);
    std::string custom_value(2000, '?');
    config.custom_transport_parameters_to_send()[kCustomParameter] =
        custom_value;
  }
  config.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromMilliseconds(quiche::GetQuicheCommandLineFlag(
          FLAGS_max_time_before_crypto_handshake_ms)));

  int address_family_for_lookup = AF_UNSPEC;
  if (quiche::GetQuicheCommandLineFlag(FLAGS_ip_version_for_host_lookup) ==
      "4") {
    address_family_for_lookup = AF_INET;
  } else if (quiche::GetQuicheCommandLineFlag(
                 FLAGS_ip_version_for_host_lookup) == "6") {
    address_family_for_lookup = AF_INET6;
  }

  // Build the client, and try to connect.
  std::unique_ptr<QuicSpdyClientBase> client = client_factory_->CreateClient(
      url.host(), host, address_family_for_lookup, port, versions, config,
      std::move(proof_verifier), std::move(session_cache));

  if (client == nullptr) {
    std::cerr << "Failed to create client." << std::endl;
    return 1;
  }

  if (!quiche::GetQuicheCommandLineFlag(FLAGS_default_client_cert).empty() &&
      !quiche::GetQuicheCommandLineFlag(FLAGS_default_client_cert_key)
           .empty()) {
    std::unique_ptr<ClientProofSource> proof_source =
        CreateTestClientProofSource(
            quiche::GetQuicheCommandLineFlag(FLAGS_default_client_cert),
            quiche::GetQuicheCommandLineFlag(FLAGS_default_client_cert_key));
    if (proof_source == nullptr) {
      std::cerr << "Failed to create client proof source." << std::endl;
      return 1;
    }
    client->crypto_config()->set_proof_source(std::move(proof_source));
  }

  int32_t initial_mtu = quiche::GetQuicheCommandLineFlag(FLAGS_initial_mtu);
  client->set_initial_max_packet_length(
      initial_mtu != 0 ? initial_mtu : quic::kDefaultMaxPacketSize);
  client->set_drop_response_body(
      quiche::GetQuicheCommandLineFlag(FLAGS_drop_response_body));
  const std::string server_connection_id_hex_string =
      quiche::GetQuicheCommandLineFlag(FLAGS_server_connection_id);
  QUICHE_CHECK(server_connection_id_hex_string.size() % 2 == 0)
      << "The length of --server_connection_id must be even. It is "
      << server_connection_id_hex_string.size() << "-byte long.";
  if (!server_connection_id_hex_string.empty()) {
    const std::string server_connection_id_bytes =
        absl::HexStringToBytes(server_connection_id_hex_string);
    client->set_server_connection_id_override(QuicConnectionId(
        server_connection_id_bytes.data(), server_connection_id_bytes.size()));
  }
  const int32_t server_connection_id_length =
      quiche::GetQuicheCommandLineFlag(FLAGS_server_connection_id_length);
  if (server_connection_id_length >= 0) {
    client->set_server_connection_id_length(server_connection_id_length);
  }
  const int32_t client_connection_id_length =
      quiche::GetQuicheCommandLineFlag(FLAGS_client_connection_id_length);
  if (client_connection_id_length >= 0) {
    client->set_client_connection_id_length(client_connection_id_length);
  }
  const size_t max_inbound_header_list_size =
      quiche::GetQuicheCommandLineFlag(FLAGS_max_inbound_header_list_size);
  if (max_inbound_header_list_size > 0) {
    client->set_max_inbound_header_list_size(max_inbound_header_list_size);
  }
  const std::string interface_name =
      quiche::GetQuicheCommandLineFlag(FLAGS_interface_name);
  if (!interface_name.empty()) {
    client->set_interface_name(interface_name);
  }
  const std::string signing_algorithms_pref =
      quiche::GetQuicheCommandLineFlag(FLAGS_signing_algorithms_pref);
  if (!signing_algorithms_pref.empty()) {
    client->SetTlsSignatureAlgorithms(signing_algorithms_pref);
  }
  if (!client->Initialize()) {
    std::cerr << "Failed to initialize client." << std::endl;
    return 1;
  }
  if (!client->Connect()) {
    quic::QuicErrorCode error = client->session()->error();
    if (error == quic::QUIC_INVALID_VERSION) {
      std::cerr << "Failed to negotiate version with " << host << ":" << port
                << ". " << client->session()->error_details() << std::endl;
      // 0: No error.
      // 20: Failed to connect due to QUIC_INVALID_VERSION.
      return quiche::GetQuicheCommandLineFlag(FLAGS_version_mismatch_ok) ? 0
                                                                         : 20;
    }
    std::cerr << "Failed to connect to " << host << ":" << port << ". "
              << quic::QuicErrorCodeToString(error) << " "
              << client->session()->error_details() << std::endl;
    return 1;
  }

  std::cout << "Connected to " << host << ":" << port;
  if (quiche::GetQuicheCommandLineFlag(FLAGS_output_resolved_server_address)) {
    std::cout << ", resolved IP " << client->server_address().host().ToString();
  }
  std::cout << std::endl;

  // Construct the string body from flags, if provided.
  std::string body = quiche::GetQuicheCommandLineFlag(FLAGS_body);
  if (!quiche::GetQuicheCommandLineFlag(FLAGS_body_hex).empty()) {
    QUICHE_DCHECK(quiche::GetQuicheCommandLineFlag(FLAGS_body).empty())
        << "Only set one of --body and --body_hex.";
    body = absl::HexStringToBytes(
        quiche::GetQuicheCommandLineFlag(FLAGS_body_hex));
  }

  // Construct a GET or POST request for supplied URL.
  spdy::Http2HeaderBlock header_block;
  header_block[":method"] = body.empty() ? "GET" : "POST";
  header_block[":scheme"] = url.scheme();
  header_block[":authority"] = url.HostPort();
  header_block[":path"] = url.PathParamsQuery();

  // Append any additional headers supplied on the command line.
  const std::string headers = quiche::GetQuicheCommandLineFlag(FLAGS_headers);
  for (absl::string_view sp : absl::StrSplit(headers, ';')) {
    QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&sp);
    if (sp.empty()) {
      continue;
    }
    std::vector<absl::string_view> kv =
        absl::StrSplit(sp, absl::MaxSplits(':', 1));
    QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&kv[0]);
    QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&kv[1]);
    header_block[kv[0]] = kv[1];
  }

  // Make sure to store the response, for later output.
  client->set_store_response(true);

  for (int i = 0; i < num_requests; ++i) {
    // Send the request.
    client->SendRequestAndWaitForResponse(header_block, body, /*fin=*/true);

    // Print request and response details.
    if (!quiche::GetQuicheCommandLineFlag(FLAGS_quiet)) {
      std::cout << "Request:" << std::endl;
      std::cout << "headers:" << header_block.DebugString();
      if (!quiche::GetQuicheCommandLineFlag(FLAGS_body_hex).empty()) {
        // Print the user provided hex, rather than binary body.
        std::cout << "body:\n"
                  << QuicheTextUtils::HexDump(absl::HexStringToBytes(
                         quiche::GetQuicheCommandLineFlag(FLAGS_body_hex)))
                  << std::endl;
      } else {
        std::cout << "body: " << body << std::endl;
      }
      std::cout << std::endl;

      if (!client->preliminary_response_headers().empty()) {
        std::cout << "Preliminary response headers: "
                  << client->preliminary_response_headers() << std::endl;
        std::cout << std::endl;
      }

      std::cout << "Response:" << std::endl;
      std::cout << "headers: " << client->latest_response_headers()
                << std::endl;
      std::string response_body = client->latest_response_body();
      if (!quiche::GetQuicheCommandLineFlag(FLAGS_body_hex).empty()) {
        // Assume response is binary data.
        std::cout << "body:\n"
                  << QuicheTextUtils::HexDump(response_body) << std::endl;
      } else {
        std::cout << "body: " << response_body << std::endl;
      }
      std::cout << "trailers: " << client->latest_response_trailers()
                << std::endl;
      std::cout << "early data accepted: " << client->EarlyDataAccepted()
                << std::endl;
      QUIC_LOG(INFO) << "Request completed with TTFB(us): "
                     << client->latest_ttfb().ToMicroseconds() << ", TTLB(us): "
                     << client->latest_ttlb().ToMicroseconds();
    }

    if (!client->connected()) {
      std::cerr << "Request caused connection failure. Error: "
                << quic::QuicErrorCodeToString(client->session()->error())
                << std::endl;
      if (!quiche::GetQuicheCommandLineFlag(FLAGS_ignore_errors)) {
        return 1;
      }
    }

    int response_code = client->latest_response_code();
    if (response_code >= 200 && response_code < 300) {
      std::cout << "Request succeeded (" << response_code << ")." << std::endl;
    } else if (response_code >= 300 && response_code < 400) {
      if (quiche::GetQuicheCommandLineFlag(FLAGS_redirect_is_success)) {
        std::cout << "Request succeeded (redirect " << response_code << ")."
                  << std::endl;
      } else {
        std::cout << "Request failed (redirect " << response_code << ")."
                  << std::endl;
        if (!quiche::GetQuicheCommandLineFlag(FLAGS_ignore_errors)) {
          return 1;
        }
      }
    } else {
      std::cout << "Request failed (" << response_code << ")." << std::endl;
      if (!quiche::GetQuicheCommandLineFlag(FLAGS_ignore_errors)) {
        return 1;
      }
    }

    if (i + 1 < num_requests) {  // There are more requests to perform.
      if (quiche::GetQuicheCommandLineFlag(FLAGS_one_connection_per_request)) {
        std::cout << "Disconnecting client between requests." << std::endl;
        client->Disconnect();
        if (!client->Initialize()) {
          std::cerr << "Failed to reinitialize client between requests."
                    << std::endl;
          return 1;
        }
        if (!client->Connect()) {
          std::cerr << "Failed to reconnect client between requests."
                    << std::endl;
          if (!quiche::GetQuicheCommandLineFlag(FLAGS_ignore_errors)) {
            return 1;
          }
        }
      } else if (!quiche::GetQuicheCommandLineFlag(
                     FLAGS_disable_port_changes)) {
        // Change the ephemeral port.
        if (!client->ChangeEphemeralPort()) {
          std::cerr << "Failed to change ephemeral port." << std::endl;
          return 1;
        }
      }
    }
  }

  return 0;
}

}  // namespace quic
