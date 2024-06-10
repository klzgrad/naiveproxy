// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is responsible for the masque_client binary. It allows testing
// our MASQUE client code by connecting to a MASQUE proxy and then sending
// HTTP/3 requests to web servers tunnelled over that MASQUE connection.
// e.g.: masque_client $PROXY_HOST:$PROXY_PORT $URL1 $URL2

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "openssl/curve25519.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/masque/masque_client.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_client_tools.h"
#include "quiche/quic/masque/masque_encapsulated_client.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/common/capsule.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_googleurl.h"
#include "quiche/common/platform/api/quiche_logging.h"
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
    std::string, proxy_headers, "",
    "A list of HTTP headers to add to request to the MASQUE proxy. "
    "Separated with colons and semicolons. "
    "For example: \"name1:value1;name2:value2\".");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, signature_auth, "",
    "Enables HTTP Signature Authentication. Pass in the string \"new\" to "
    "generate new keys. Otherwise, pass in the key ID in ASCII followed by a "
    "colon and the 32-byte private key as hex. For example: \"kid:0123...f\".");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, bring_up_tun, false,
    "If set to true, no URLs need to be specified and instead a TUN device "
    "is brought up with the assigned IP from the MASQUE CONNECT-IP server.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, dns_on_client, false,
    "If set to true, masque_client will perform DNS for encapsulated URLs and "
    "send the IP litteral in the CONNECT request. If set to false, "
    "masque_client send the hostname in the CONNECT request.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, bring_up_tap, false,
    "If set to true, no URLs need to be specified and instead a TAP device "
    "is brought up for a MASQUE CONNECT-ETHERNET session.");

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
    char datagram[kMasqueIpPacketBufferSize];
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

class MasqueTapSession
    : public MasqueClientSession::EncapsulatedEthernetSession,
      public QuicSocketEventListener {
 public:
  MasqueTapSession(QuicEventLoop* event_loop, MasqueClientSession* session)
      : event_loop_(event_loop), session_(session) {}
  ~MasqueTapSession() override = default;

  void CreateInterface(void) {
    QUIC_LOG(ERROR) << "Bringing up TAP";
    fd_ = CreateTapInterface();
    if (fd_ < 0) {
      QUIC_LOG(FATAL) << "Failed to create TAP interface";
    }
    if (!event_loop_->RegisterSocket(fd_, kSocketEventReadable, this)) {
      QUIC_LOG(FATAL) << "Failed to register TAP fd with the event loop";
    }
  }

  // MasqueClientSession::EncapsulatedEthernetSession
  void ProcessEthernetFrame(absl::string_view frame) override {
    QUIC_LOG(INFO) << " Received Ethernet frame of length " << frame.length();
    if (fd_ == -1) {
      // TAP not open, early return
      return;
    }
    if (write(fd_, frame.data(), frame.size()) == -1) {
      QUIC_LOG(FATAL) << "Failed to write";
    }
  }
  void CloseEthernetSession(const std::string& details) override {
    QUIC_LOG(ERROR) << "Was asked to close Ethernet session: " << details;
  }

  // QuicSocketEventListener
  void OnSocketEvent(QuicEventLoop* /*event_loop*/, QuicUdpSocketFd fd,
                     QuicSocketEventMask events) override {
    if ((events & kSocketEventReadable) == 0) {
      QUIC_DVLOG(1) << "Ignoring OnEvent fd " << fd << " event mask " << events;
      return;
    }
    char datagram[kMasqueEthernetFrameBufferSize];
    while (true) {
      ssize_t read_size = read(fd, datagram, sizeof(datagram));
      if (read_size < 0) {
        break;
      }
      // Frame received from the TAP. Write it to the MASQUE CONNECT-ETHERNET
      // session.
      session_->SendEthernetFrame(absl::string_view(datagram, read_size), this);
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
  std::string local_mac_address_;  // string, uint8_t[6], or new wrapper type?
  int fd_ = -1;
};

int RunMasqueClient(int argc, char* argv[]) {
  const char* usage =
      "Usage: masque_client [options] <proxy-url> <urls>..\n"
      "  <proxy-url> is the URI template of the MASQUE server,\n"
      "  or host:port to use the default template";

  // The first non-flag argument is the URI template of the MASQUE server.
  // All subsequent ones are interpreted as URLs to fetch via the MASQUE server.
  // Note that the URI template expansion currently only supports string
  // replacement of {target_host} and {target_port}, not
  // {?target_host,target_port}.
  std::vector<std::string> urls =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);

  std::string signature_auth_param =
      quiche::GetQuicheCommandLineFlag(FLAGS_signature_auth);
  std::string signature_auth_key_id;
  std::string signature_auth_private_key;
  std::string signature_auth_public_key;
  if (!signature_auth_param.empty()) {
    static constexpr size_t kEd25519Rfc8032PrivateKeySize = 32;
    uint8_t public_key[ED25519_PUBLIC_KEY_LEN];
    uint8_t private_key[ED25519_PRIVATE_KEY_LEN];
    const bool is_new_key_pair = signature_auth_param == "new";
    if (is_new_key_pair) {
      ED25519_keypair(public_key, private_key);
      QUIC_LOG(INFO) << "Generated new Signature Authentication key pair";
    } else {
      std::vector<absl::string_view> signature_auth_param_split =
          absl::StrSplit(signature_auth_param, absl::MaxSplits(':', 1));
      std::string private_key_seed;
      if (signature_auth_param_split.size() != 2) {
        QUIC_LOG(ERROR)
            << "Signature authentication parameter is missing a colon";
        return 1;
      }
      signature_auth_key_id = signature_auth_param_split[0];
      if (signature_auth_key_id.empty()) {
        QUIC_LOG(ERROR) << "Signature authentication key ID cannot be empty";
        return 1;
      }
      if (!absl::HexStringToBytes(signature_auth_param_split[1],
                                  &private_key_seed)) {
        QUIC_LOG(ERROR) << "Signature authentication key hex value is invalid";
        return 1;
      }

      if (private_key_seed.size() != kEd25519Rfc8032PrivateKeySize) {
        QUIC_LOG(ERROR)
            << "Invalid signature authentication private key length "
            << private_key_seed.size();
        return 1;
      }
      ED25519_keypair_from_seed(
          public_key, private_key,
          reinterpret_cast<uint8_t*>(private_key_seed.data()));
      QUIC_LOG(INFO) << "Loaded Signature Authentication key pair";
    }
    // Note that Ed25519 private keys are 32 bytes long per RFC 8032. However,
    // to reduce CPU costs, BoringSSL represents private keys in memory as the
    // concatenation of the 32-byte private key and the corresponding 32-byte
    // public key - which makes for a total of 64 bytes. The private key log
    // below relies on this BoringSSL implementation detail to extract the
    // RFC 8032 private key because BoringSSL does not provide a supported way
    // to access it. This is required to allow us to print the private key in a
    // format that can be passed back in to BoringSSL from the command-line. See
    // curve25519.h for details. The rest of our signature authentication code
    // uses the BoringSSL representation without relying on this implementation
    // detail.
    static_assert(kEd25519Rfc8032PrivateKeySize <=
                  static_cast<size_t>(ED25519_PRIVATE_KEY_LEN));

    std::string private_key_hexstr = absl::BytesToHexString(absl::string_view(
        reinterpret_cast<char*>(private_key), kEd25519Rfc8032PrivateKeySize));
    std::string public_key_hexstr = absl::BytesToHexString(absl::string_view(
        reinterpret_cast<char*>(public_key), ED25519_PUBLIC_KEY_LEN));
    if (is_new_key_pair) {
      std::cout << "Generated new Signature Authentication key pair."
                << std::endl;
      std::cout << "Private key: " << private_key_hexstr << std::endl;
      std::cout << "Public key: " << public_key_hexstr << std::endl;
      return 0;
    }
    QUIC_LOG(INFO) << "Private key: " << private_key_hexstr;
    QUIC_LOG(INFO) << "Public key: " << public_key_hexstr;
    signature_auth_private_key = std::string(
        reinterpret_cast<char*>(private_key), ED25519_PRIVATE_KEY_LEN);
    signature_auth_public_key = std::string(reinterpret_cast<char*>(public_key),
                                            ED25519_PUBLIC_KEY_LEN);
  }

  bool bring_up_tun = quiche::GetQuicheCommandLineFlag(FLAGS_bring_up_tun);
  bool bring_up_tap = quiche::GetQuicheCommandLineFlag(FLAGS_bring_up_tap);
  if (urls.empty() && !bring_up_tun && !bring_up_tap) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }
  if (bring_up_tun && bring_up_tap) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  quiche::QuicheSystemEventLoop system_event_loop("masque_client");
  const bool disable_certificate_verification =
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification);
  MasqueMode masque_mode = MasqueMode::kOpen;
  std::string mode_string = quiche::GetQuicheCommandLineFlag(FLAGS_masque_mode);
  if (!mode_string.empty()) {
    if (mode_string == "open") {
      masque_mode = MasqueMode::kOpen;
    } else if (mode_string == "connectip" || mode_string == "connect-ip") {
      masque_mode = MasqueMode::kConnectIp;
    } else if (mode_string == "connectethernet" ||
               mode_string == "connect-ethernet") {
      masque_mode = MasqueMode::kConnectEthernet;
    } else {
      QUIC_LOG(ERROR) << "Invalid masque_mode \"" << mode_string << "\"";
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
    QUIC_LOG(ERROR) << "Invalid address_family " << address_family;
    return 1;
  }
  const bool dns_on_client =
      quiche::GetQuicheCommandLineFlag(FLAGS_dns_on_client);
  std::unique_ptr<QuicEventLoop> event_loop =
      GetDefaultEventLoop()->Create(QuicDefaultClock::Get());

  std::vector<std::unique_ptr<MasqueClient>> masque_clients;
  for (absl::string_view uri_template_sv : absl::StrSplit(urls[0], ',')) {
    std::string uri_template = std::string(uri_template_sv);
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
      QUIC_LOG(ERROR) << "Failed to parse MASQUE URI template \""
                      << uri_template << "\"";
      return 1;
    }
    std::unique_ptr<MasqueClient> masque_client;
    if (masque_clients.empty()) {
      std::string host = uri_template.substr(parsed_uri_template.host.begin,
                                             parsed_uri_template.host.len);
      std::unique_ptr<ProofVerifier> proof_verifier;
      if (disable_certificate_verification) {
        proof_verifier = std::make_unique<FakeProofVerifier>();
      } else {
        proof_verifier = CreateDefaultProofVerifier(host);
      }
      masque_client =
          MasqueClient::Create(uri_template, masque_mode, event_loop.get(),
                               std::move(proof_verifier));
    } else {
      masque_client = tools::CreateAndConnectMasqueEncapsulatedClient(
          masque_clients.back().get(), masque_mode, event_loop.get(),
          uri_template, disable_certificate_verification,
          address_family_for_lookup, dns_on_client,
          /*is_also_underlying=*/true);
    }
    if (masque_client == nullptr) {
      return 1;
    }

    QUIC_LOG(INFO) << "MASQUE[" << masque_clients.size() << "] to "
                   << uri_template << " is connected "
                   << masque_client->connection_id() << " in " << masque_mode
                   << " mode";

    masque_client->masque_client_session()->set_additional_headers(
        quiche::GetQuicheCommandLineFlag(FLAGS_proxy_headers));
    if (!signature_auth_param.empty()) {
      masque_client->masque_client_session()->EnableSignatureAuth(
          signature_auth_key_id, signature_auth_private_key,
          signature_auth_public_key);
    }
    masque_clients.push_back(std::move(masque_client));
  }
  std::unique_ptr<MasqueClient> masque_client =
      std::move(masque_clients.back());
  masque_clients.pop_back();

  if (bring_up_tun) {
    QUIC_LOG(INFO) << "Bringing up tun";
    MasqueTunSession tun_session(event_loop.get(),
                                 masque_client->masque_client_session());
    masque_client->masque_client_session()->SendIpPacket(
        absl::string_view("asdf"), &tun_session);
    while (true) {
      event_loop->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
    }
    QUICHE_NOTREACHED();
  }
  if (bring_up_tap) {
    MasqueTapSession tap_session(event_loop.get(),
                                 masque_client->masque_client_session());
    tap_session.CreateInterface();
    while (true) {
      event_loop->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
    }
    QUICHE_NOTREACHED();
  }

  for (size_t i = 1; i < urls.size(); ++i) {
    if (absl::StartsWith(urls[i], "/")) {
      QuicSpdyClientStream* stream =
          masque_client->masque_client_session()->SendGetRequest(urls[i]);
      while (stream->time_to_response_complete().IsInfinite()) {
        event_loop->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
      }
      // Print the response body to stdout.
      std::cout << std::endl << stream->data() << std::endl;
    } else {
      std::unique_ptr<MasqueEncapsulatedClient> encapsulated_client =
          tools::CreateAndConnectMasqueEncapsulatedClient(
              masque_client.get(), masque_mode, event_loop.get(), urls[i],
              disable_certificate_verification, address_family_for_lookup,
              dns_on_client, /*is_also_underlying=*/false);
      if (!encapsulated_client || !tools::SendRequestOnMasqueEncapsulatedClient(
                                      *encapsulated_client, urls[i])) {
        return 1;
      }
    }
  }

  return 0;
}

}  // namespace

}  // namespace quic

int main(int argc, char* argv[]) { return quic::RunMasqueClient(argc, argv); }
