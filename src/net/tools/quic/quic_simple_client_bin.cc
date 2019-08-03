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
//   quic_client http://www.google.com
//   quic_client http://www.google.com --quiet
//   quic_client https://www.google.com --port=443
//
// Use a specific version:
//   quic_client http://www.google.com --quic_version=23
//
// Send a POST instead of a GET:
//   quic_client http://www.google.com --body="this is a POST body"
//
// Append additional headers to the request:
//   quic_client http://www.google.com  --host=${IP}
//               --headers="Header-A: 1234; Header-B: 5678"
//
// Connect to a host different to the URL being requested:
//   quic_client mail.google.com --host=www.google.com
//
// Connect to a specific IP:
//   IP=`dig www.google.com +short | head -1`
//   quic_client www.google.com --host=${IP}
//
// Try to connect to a host which does not speak QUIC:
//   quic_client http://www.example.com

#include <iostream>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/task/thread_pool/thread_pool.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/ct_log_verifier.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/multi_log_ct_verifier.h"
#include "net/http/transport_security_state.h"
#include "net/quic/crypto/proof_verifier_chromium.h"
#include "net/spdy/spdy_http_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_system_event_loop.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/quic/tools/quic_toy_client.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"
#include "net/tools/quic/quic_simple_client.h"
#include "net/tools/quic/synchronous_host_resolver.h"
#include "url/gurl.h"

using net::CertVerifier;
using net::CTVerifier;
using net::MultiLogCTVerifier;
using quic::ProofVerifier;
using net::ProofVerifierChromium;
using quic::QuicStringPiece;
using quic::QuicTextUtils;
using net::TransportSecurityState;
using spdy::SpdyHeaderBlock;
using std::cout;
using std::cerr;
using std::endl;

namespace {

class QuicSimpleClientFactory : public quic::QuicToyClient::ClientFactory {
 public:
  std::unique_ptr<quic::QuicSpdyClientBase> CreateClient(
      std::string host,
      uint16_t port,
      quic::ParsedQuicVersionVector versions,
      std::unique_ptr<quic::ProofVerifier> verifier) override {
    net::AddressList addresses;
    int rv = net::SynchronousHostResolver::Resolve(host, &addresses);
    if (rv != net::OK) {
      LOG(ERROR) << "Unable to resolve '" << host
                 << "' : " << net::ErrorToShortString(rv);
      return nullptr;
    }
    // Determine IP address to connect to from supplied hostname.
    quic::QuicIpAddress ip_addr;
    if (!ip_addr.FromString(host)) {
      net::AddressList addresses;
      int rv = net::SynchronousHostResolver::Resolve(host, &addresses);
      if (rv != net::OK) {
        LOG(ERROR) << "Unable to resolve '" << host
                   << "' : " << net::ErrorToShortString(rv);
        return nullptr;
      }
      ip_addr =
          quic::QuicIpAddress(quic::QuicIpAddressImpl(addresses[0].address()));
    }

    quic::QuicServerId server_id(host, port, false);
    return quic::QuicMakeUnique<net::QuicSimpleClient>(
        quic::QuicSocketAddress(ip_addr, port), server_id, versions,
        std::move(verifier));
  }
};

}  // namespace

int main(int argc, char* argv[]) {
  QuicSystemEventLoop event_loop("quic_client");
  const char* usage = "Usage: quic_client [options] <url>";

  // All non-flag arguments should be interpreted as URLs to fetch.
  std::vector<std::string> urls =
      quic::QuicParseCommandLineFlags(usage, argc, argv);
  if (urls.size() != 1) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    exit(0);
  }

  QuicSimpleClientFactory factory;
  quic::QuicToyClient client(&factory);
  return client.SendRequestsAndPrintResponses(urls);
}
