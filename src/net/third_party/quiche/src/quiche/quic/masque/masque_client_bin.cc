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

DEFINE_QUICHE_COMMAND_LINE_FLAG(int, address_family, 0,
                                "IP address family to use. Must be 0, 4 or 6. "
                                "Defaults to 0 which means any.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, masque_mode, "",
    "Allows setting MASQUE mode, currently only valid value is \"open\".");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, bring_up_tun, false,
    "If set to true, no URLs need to be specified and instead a TUN device "
    "is brought up with the assigned IP from the MASQUE CONNECT-IP server");

namespace quic {

namespace {

using ::quiche::AddressAssignCapsule;
using ::quiche::AddressRequestCapsule;
using ::quiche::RouteAdvertisementCapsule;

class MasqueTunSession : public MasqueClientSession::EncapsulatedIpSession,
                         public QuicSocketEventListener {
 public:
  MasqueTunSession(QuicEventLoop* event_loop, MasqueClientSession* session)
      : event_loop_(event_loop), session_(session) {}
  ~MasqueTunSession() override = default;
  // MasqueClientSession::EncapsulatedIpSession
  void ProcessIpPacket(absl::string_view packet) override {
    QUIC_LOG(INFO) << " Received IP packets of length " << packet.length();
    if (fd_ == -1) {
      // TUN not open, early return
      return;
    }
    if (write(fd_, packet.data(), packet.size()) == -1) {
      QUIC_LOG(FATAL) << "Failed to write";
    }
  }
  void CloseIpSession(const std::string& details) override {
    QUIC_LOG(ERROR) << "Was asked to close IP session: " << details;
  }
  bool OnAddressAssignCapsule(const AddressAssignCapsule& capsule) override {
    for (auto assigned_address : capsule.assigned_addresses) {
      if (assigned_address.ip_prefix.address().IsIPv4()) {
        QUIC_LOG(INFO) << "MasqueTunSession saving local IPv4 address "
                       << assigned_address.ip_prefix.address();
        local_address_ = assigned_address.ip_prefix.address();
        break;
      }
    }
    // Bring up the TUN
    QUIC_LOG(ERROR) << "Bringing up tun with address " << local_address_;
    fd_ = CreateTunInterface(local_address_, false);
    if (fd_ < 0) {
      QUIC_LOG(FATAL) << "Failed to create TUN interface";
    }
    if (!event_loop_->RegisterSocket(fd_, kSocketEventReadable, this)) {
      QUIC_LOG(FATAL) << "Failed to register TUN fd with the event loop";
    }
    return true;
  }
  bool OnAddressRequestCapsule(
      const AddressRequestCapsule& /*capsule*/) override {
    // Always ignore the address request capsule from the server.
    return true;
  }
  bool OnRouteAdvertisementCapsule(
      const RouteAdvertisementCapsule& /*capsule*/) override {
    // Consider installing routes.
    return true;
  }

  // QuicSocketEventListener
  void OnSocketEvent(QuicEventLoop* /*event_loop*/, QuicUdpSocketFd fd,
                     QuicSocketEventMask events) override {
    if ((events & kSocketEventReadable) == 0) {
      QUIC_DVLOG(1) << "Ignoring OnEvent fd " << fd << " event mask " << events;
      return;
    }
    char datagram[1501];
    while (true) {
      ssize_t read_size = read(fd, datagram, sizeof(datagram));
      if (read_size < 0) {
        break;
      }
      // Packet received from the TUN. Write it to the MASQUE CONNECT-IP
      // session.
      session_->SendIpPacket(absl::string_view(datagram, read_size), this);
    }
    if (!event_loop_->SupportsEdgeTriggered()) {
      if (!event_loop_->RearmSocket(fd, kSocketEventReadable)) {
        QUIC_BUG(MasqueServerSession_ConnectIp_OnSocketEvent_Rearm)
            << "Failed to re-arm socket " << fd << " for reading";
      }
    }
  }

 private:
  QuicEventLoop* event_loop_;
  MasqueClientSession* session_;
  QuicIpAddress local_address_;
  int fd_ = -1;
};

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
  bool bring_up_tun = quiche::GetQuicheCommandLineFlag(FLAGS_bring_up_tun);
  if (urls.empty() && !bring_up_tun) {
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
  if (!mode_string.empty()) {
    if (mode_string == "open") {
      masque_mode = MasqueMode::kOpen;
    } else if (mode_string == "connectip" || mode_string == "connect-ip") {
      masque_mode = MasqueMode::kConnectIp;
    } else {
      std::cerr << "Invalid masque_mode \"" << mode_string << "\"" << std::endl;
      return 1;
    }
  }
  const int address_family =
      quiche::GetQuicheCommandLineFlag(FLAGS_address_family);
  int address_family_for_lookup;
  if (address_family == 0) {
    address_family_for_lookup = AF_UNSPEC;
  } else if (address_family == 4) {
    address_family_for_lookup = AF_INET;
  } else if (address_family == 6) {
    address_family_for_lookup = AF_INET6;
  } else {
    std::cerr << "Invalid address_family " << address_family << std::endl;
    return 1;
  }
  std::unique_ptr<MasqueClient> masque_client = MasqueClient::Create(
      uri_template, masque_mode, event_loop.get(), std::move(proof_verifier));
  if (masque_client == nullptr) {
    return 1;
  }

  std::cerr << "MASQUE is connected " << masque_client->connection_id()
            << " in " << masque_mode << " mode" << std::endl;

  if (bring_up_tun) {
    std::cerr << "Bringing up tun" << std::endl;
    MasqueTunSession tun_session(event_loop.get(),
                                 masque_client->masque_client_session());
    masque_client->masque_client_session()->SendIpPacket(
        absl::string_view("asdf"), &tun_session);
    while (true) {
      event_loop->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
    }
    QUICHE_NOTREACHED();
  }

  for (size_t i = 1; i < urls.size(); ++i) {
    if (!tools::SendEncapsulatedMasqueRequest(
            masque_client.get(), event_loop.get(), urls[i],
            disable_certificate_verification, address_family_for_lookup)) {
      return 1;
    }
  }

  return 0;
}

}  // namespace

}  // namespace quic

int main(int argc, char* argv[]) { return quic::RunMasqueClient(argc, argv); }
