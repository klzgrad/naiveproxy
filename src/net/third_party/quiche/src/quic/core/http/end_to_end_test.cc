// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_epoll_connection_helper.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer_wrapper.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_epoll.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_error_code_wrappers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_port_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_sleep.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_loopback.h"
#include "net/third_party/quiche/src/quic/test_tools/bad_packet_writer.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/packet_dropping_test_writer.h"
#include "net/third_party/quiche/src/quic/test_tools/packet_reordering_writer.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_encoder_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_client_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_dispatcher_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_flow_controller_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_sent_packet_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_server_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_session_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_id_manager_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_sequencer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_client.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_server.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/server_thread.h"
#include "net/third_party/quiche/src/quic/tools/quic_backend_response.h"
#include "net/third_party/quiche/src/quic/tools/quic_client.h"
#include "net/third_party/quiche/src/quic/tools/quic_memory_cache_backend.h"
#include "net/third_party/quiche/src/quic/tools/quic_server.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_client_stream.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_stream.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

using spdy::kV3LowestPriority;
using spdy::SpdyFramer;
using spdy::SpdyHeaderBlock;
using spdy::SpdySerializedFrame;
using spdy::SpdySettingsIR;

namespace quic {
namespace test {
namespace {

const char kFooResponseBody[] = "Artichoke hearts make me happy.";
const char kBarResponseBody[] = "Palm hearts are pretty delicious, also.";
const float kSessionToStreamRatio = 1.5;

// Run all tests with the cross products of all versions.
struct TestParams {
  TestParams(const ParsedQuicVersionVector& client_supported_versions,
             const ParsedQuicVersionVector& server_supported_versions,
             ParsedQuicVersion negotiated_version,
             QuicTag congestion_control_tag)
      : client_supported_versions(client_supported_versions),
        server_supported_versions(server_supported_versions),
        negotiated_version(negotiated_version),
        congestion_control_tag(congestion_control_tag) {}

  friend std::ostream& operator<<(std::ostream& os, const TestParams& p) {
    os << "{ server_supported_versions: "
       << ParsedQuicVersionVectorToString(p.server_supported_versions);
    os << " client_supported_versions: "
       << ParsedQuicVersionVectorToString(p.client_supported_versions);
    os << " negotiated_version: "
       << ParsedQuicVersionToString(p.negotiated_version);
    os << " congestion_control_tag: "
       << QuicTagToString(p.congestion_control_tag) << " }";
    return os;
  }

  ParsedQuicVersionVector client_supported_versions;
  ParsedQuicVersionVector server_supported_versions;
  ParsedQuicVersion negotiated_version;
  QuicTag congestion_control_tag;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  std::string rv = quiche::QuicheStrCat(
      ParsedQuicVersionToString(p.negotiated_version), "_Server_",
      ParsedQuicVersionVectorToString(p.server_supported_versions), "_Client_",
      ParsedQuicVersionVectorToString(p.client_supported_versions), "_",
      QuicTagToString(p.congestion_control_tag));
  std::replace(rv.begin(), rv.end(), ',', '_');
  std::replace(rv.begin(), rv.end(), ' ', '_');
  return rv;
}

// Constructs various test permutations.
std::vector<TestParams> GetTestParams(bool use_tls_handshake) {
  // Divide the versions into buckets in which the intra-frame format
  // is compatible. When clients encounter QUIC version negotiation
  // they simply retransmit all packets using the new version's
  // QUIC framing. However, they are unable to change the intra-frame
  // layout (for example to change HTTP/2 headers to SPDY/3, or a change in the
  // handshake protocol). So these tests need to ensure that clients are never
  // attempting to do 0-RTT across incompatible versions. Chromium only
  // supports a single version at a time anyway. :)
  ParsedQuicVersionVector all_supported_versions =
      FilterSupportedVersions(AllSupportedVersions());

  // Buckets are separated by versions: versions without crypto frames use
  // STREAM frames for the handshake, and only have QUIC crypto as the handshake
  // protocol. Versions that use CRYPTO frames for the handshake must also be
  // split based on the handshake protocol. If the handshake protocol (QUIC
  // crypto or TLS) changes, the ClientHello/CHLO must be reconstructed for the
  // correct protocol.
  ParsedQuicVersionVector version_buckets[3];

  for (const ParsedQuicVersion& version : all_supported_versions) {
    if (!use_tls_handshake && version.handshake_protocol == PROTOCOL_TLS1_3) {
      continue;
    }
    if (!QuicVersionUsesCryptoFrames(version.transport_version)) {
      version_buckets[0].push_back(version);
    } else if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      version_buckets[1].push_back(version);
    } else {
      version_buckets[2].push_back(version);
    }
  }

  std::vector<TestParams> params;
  for (const QuicTag congestion_control_tag : {kRENO, kTBBR, kQBIC, kB2ON}) {
    if (!GetQuicReloadableFlag(quic_allow_client_enabled_bbr_v2) &&
        congestion_control_tag == kB2ON) {
      continue;
    }
    for (const ParsedQuicVersionVector& client_versions : version_buckets) {
      if (FilterSupportedVersions(client_versions).empty()) {
        continue;
      }
      // Add an entry for server and client supporting all versions.
      params.push_back(TestParams(client_versions, all_supported_versions,
                                  client_versions.front(),
                                  congestion_control_tag));
      // Test client supporting all versions and server supporting
      // 1 version. Simulate an old server and exercise version
      // downgrade in the client. Protocol negotiation should
      // occur.  Skip the i = 0 case because it is essentially the
      // same as the default case.
      for (size_t i = 1; i < client_versions.size(); ++i) {
        ParsedQuicVersionVector server_supported_versions;
        server_supported_versions.push_back(client_versions[i]);
        if (FilterSupportedVersions(server_supported_versions).empty()) {
          continue;
        }
        params.push_back(TestParams(client_versions, server_supported_versions,
                                    server_supported_versions.front(),
                                    congestion_control_tag));
      }  // End of inner version loop.
    }    // End of outer version loop.
  }      // End of congestion_control_tag loop.

  return params;
}

void WriteHeadersOnStream(QuicSpdyStream* stream) {
  // Since QuicSpdyStream uses QuicHeaderList::empty() to detect too large
  // headers, it also fails when receiving empty headers.
  SpdyHeaderBlock headers;
  headers["foo"] = "bar";
  stream->WriteHeaders(std::move(headers), /* fin = */ false, nullptr);
}

class ServerDelegate : public PacketDroppingTestWriter::Delegate {
 public:
  explicit ServerDelegate(QuicDispatcher* dispatcher)
      : dispatcher_(dispatcher) {}
  ~ServerDelegate() override = default;
  void OnCanWrite() override { dispatcher_->OnCanWrite(); }

 private:
  QuicDispatcher* dispatcher_;
};

class ClientDelegate : public PacketDroppingTestWriter::Delegate {
 public:
  explicit ClientDelegate(QuicClient* client) : client_(client) {}
  ~ClientDelegate() override = default;
  void OnCanWrite() override {
    QuicEpollEvent event(EPOLLOUT);
    client_->epoll_network_helper()->OnEvent(client_->GetLatestFD(), &event);
  }

 private:
  QuicClient* client_;
};

class EndToEndTest : public QuicTestWithParam<TestParams> {
 protected:
  EndToEndTest()
      : initialized_(false),
        connect_to_server_on_initialize_(true),
        server_address_(QuicSocketAddress(TestLoopback(),
                                          QuicPickServerPortForTestsOrDie())),
        server_hostname_("test.example.com"),
        client_writer_(nullptr),
        server_writer_(nullptr),
        negotiated_version_(UnsupportedQuicVersion()),
        chlo_multiplier_(0),
        stream_factory_(nullptr),
        expected_server_connection_id_length_(kQuicDefaultConnectionIdLength) {
    client_supported_versions_ = GetParam().client_supported_versions;
    server_supported_versions_ = GetParam().server_supported_versions;
    negotiated_version_ = GetParam().negotiated_version;

    QUIC_LOG(INFO) << "Using Configuration: " << GetParam();

    // Use different flow control windows for client/server.
    client_config_.SetInitialStreamFlowControlWindowToSend(
        2 * kInitialStreamFlowControlWindowForTest);
    client_config_.SetInitialSessionFlowControlWindowToSend(
        2 * kInitialSessionFlowControlWindowForTest);
    server_config_.SetInitialStreamFlowControlWindowToSend(
        3 * kInitialStreamFlowControlWindowForTest);
    server_config_.SetInitialSessionFlowControlWindowToSend(
        3 * kInitialSessionFlowControlWindowForTest);

    // The default idle timeouts can be too strict when running on a busy
    // machine.
    const QuicTime::Delta timeout = QuicTime::Delta::FromSeconds(30);
    client_config_.set_max_time_before_crypto_handshake(timeout);
    client_config_.set_max_idle_time_before_crypto_handshake(timeout);
    server_config_.set_max_time_before_crypto_handshake(timeout);
    server_config_.set_max_idle_time_before_crypto_handshake(timeout);

    AddToCache("/foo", 200, kFooResponseBody);
    AddToCache("/bar", 200, kBarResponseBody);
  }

  ~EndToEndTest() override { QuicRecyclePort(server_address_.port()); }

  virtual void CreateClientWithWriter() {
    client_.reset(CreateQuicClient(client_writer_));
  }

  QuicTestClient* CreateQuicClient(QuicPacketWriterWrapper* writer) {
    QuicTestClient* client =
        new QuicTestClient(server_address_, server_hostname_, client_config_,
                           client_supported_versions_,
                           crypto_test_utils::ProofVerifierForTesting());
    client->UseWriter(writer);
    if (!pre_shared_key_client_.empty()) {
      client->client()->SetPreSharedKey(pre_shared_key_client_);
    }
    client->UseConnectionIdLength(override_server_connection_id_length_);
    client->UseClientConnectionIdLength(override_client_connection_id_length_);
    client->client()->set_connection_debug_visitor(connection_debug_visitor_);
    client->Connect();
    return client;
  }

  void set_smaller_flow_control_receive_window() {
    const uint32_t kClientIFCW = 64 * 1024;
    const uint32_t kServerIFCW = 1024 * 1024;
    set_client_initial_stream_flow_control_receive_window(kClientIFCW);
    set_client_initial_session_flow_control_receive_window(
        kSessionToStreamRatio * kClientIFCW);
    set_server_initial_stream_flow_control_receive_window(kServerIFCW);
    set_server_initial_session_flow_control_receive_window(
        kSessionToStreamRatio * kServerIFCW);
  }

  void set_client_initial_stream_flow_control_receive_window(uint32_t window) {
    ASSERT_TRUE(client_ == nullptr);
    QUIC_DLOG(INFO) << "Setting client initial stream flow control window: "
                    << window;
    client_config_.SetInitialStreamFlowControlWindowToSend(window);
  }

  void set_client_initial_session_flow_control_receive_window(uint32_t window) {
    ASSERT_TRUE(client_ == nullptr);
    QUIC_DLOG(INFO) << "Setting client initial session flow control window: "
                    << window;
    client_config_.SetInitialSessionFlowControlWindowToSend(window);
  }

  void set_client_initial_max_stream_data_incoming_bidirectional(
      uint32_t window) {
    ASSERT_TRUE(client_ == nullptr);
    QUIC_DLOG(INFO)
        << "Setting client initial max stream data incoming bidirectional: "
        << window;
    client_config_.SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(
        window);
  }

  void set_server_initial_max_stream_data_outgoing_bidirectional(
      uint32_t window) {
    ASSERT_TRUE(client_ == nullptr);
    QUIC_DLOG(INFO)
        << "Setting server initial max stream data outgoing bidirectional: "
        << window;
    server_config_.SetInitialMaxStreamDataBytesOutgoingBidirectionalToSend(
        window);
  }

  void set_server_initial_stream_flow_control_receive_window(uint32_t window) {
    ASSERT_TRUE(server_thread_ == nullptr);
    QUIC_DLOG(INFO) << "Setting server initial stream flow control window: "
                    << window;
    server_config_.SetInitialStreamFlowControlWindowToSend(window);
  }

  void set_server_initial_session_flow_control_receive_window(uint32_t window) {
    ASSERT_TRUE(server_thread_ == nullptr);
    QUIC_DLOG(INFO) << "Setting server initial session flow control window: "
                    << window;
    server_config_.SetInitialSessionFlowControlWindowToSend(window);
  }

  const QuicSentPacketManager* GetSentPacketManagerFromFirstServerSession() {
    return &GetServerConnection()->sent_packet_manager();
  }

  QuicSpdyClientSession* GetClientSession() {
    return client_->client()->client_session();
  }

  QuicConnection* GetClientConnection() {
    return GetClientSession()->connection();
  }

  QuicConnection* GetServerConnection() {
    return GetServerSession()->connection();
  }

  QuicSpdySession* GetServerSession() {
    QuicDispatcher* dispatcher =
        QuicServerPeer::GetDispatcher(server_thread_->server());
    EXPECT_EQ(1u, dispatcher->session_map().size());
    return static_cast<QuicSpdySession*>(
        dispatcher->session_map().begin()->second.get());
  }

  bool Initialize() {
    QuicTagVector copt;
    server_config_.SetConnectionOptionsToSend(copt);
    copt = client_extra_copts_;

    // TODO(nimia): Consider setting the congestion control algorithm for the
    // client as well according to the test parameter.
    copt.push_back(GetParam().congestion_control_tag);
    copt.push_back(k2PTO);
    if (VersionHasIetfQuicFrames(negotiated_version_.transport_version)) {
      copt.push_back(kILD0);
    }
    copt.push_back(kPLE1);
    client_config_.SetConnectionOptionsToSend(copt);

    // Start the server first, because CreateQuicClient() attempts
    // to connect to the server.
    StartServer();

    if (!connect_to_server_on_initialize_) {
      initialized_ = true;
      return true;
    }

    CreateClientWithWriter();
    static QuicEpollEvent event(EPOLLOUT);
    if (client_writer_ != nullptr) {
      client_writer_->Initialize(
          QuicConnectionPeer::GetHelper(GetClientConnection()),
          QuicConnectionPeer::GetAlarmFactory(GetClientConnection()),
          std::make_unique<ClientDelegate>(client_->client()));
    }
    initialized_ = true;
    return client_->client()->connected();
  }

  void SetUp() override {
    // The ownership of these gets transferred to the QuicPacketWriterWrapper
    // when Initialize() is executed.
    client_writer_ = new PacketDroppingTestWriter();
    server_writer_ = new PacketDroppingTestWriter();
  }

  void TearDown() override {
    ASSERT_TRUE(initialized_) << "You must call Initialize() in every test "
                              << "case. Otherwise, your test will leak memory.";
    GetClientConnection()->set_debug_visitor(nullptr);
    StopServer();
  }

  void StartServer() {
    auto* test_server = new QuicTestServer(
        crypto_test_utils::ProofSourceForTesting(), server_config_,
        server_supported_versions_, &memory_cache_backend_,
        expected_server_connection_id_length_);
    server_thread_ =
        std::make_unique<ServerThread>(test_server, server_address_);
    if (chlo_multiplier_ != 0) {
      server_thread_->server()->SetChloMultiplier(chlo_multiplier_);
    }
    if (!pre_shared_key_server_.empty()) {
      server_thread_->server()->SetPreSharedKey(pre_shared_key_server_);
    }
    server_thread_->Initialize();
    server_address_ =
        QuicSocketAddress(server_address_.host(), server_thread_->GetPort());
    QuicDispatcher* dispatcher =
        QuicServerPeer::GetDispatcher(server_thread_->server());
    QuicDispatcherPeer::UseWriter(dispatcher, server_writer_);

    server_writer_->Initialize(QuicDispatcherPeer::GetHelper(dispatcher),
                               QuicDispatcherPeer::GetAlarmFactory(dispatcher),
                               std::make_unique<ServerDelegate>(dispatcher));
    if (stream_factory_ != nullptr) {
      static_cast<QuicTestServer*>(server_thread_->server())
          ->SetSpdyStreamFactory(stream_factory_);
    }

    server_thread_->Start();
  }

  void StopServer() {
    if (server_thread_) {
      server_thread_->Quit();
      server_thread_->Join();
    }
  }

  void AddToCache(quiche::QuicheStringPiece path,
                  int response_code,
                  quiche::QuicheStringPiece body) {
    memory_cache_backend_.AddSimpleResponse(server_hostname_, path,
                                            response_code, body);
  }

  void SetPacketLossPercentage(int32_t loss) {
    client_writer_->set_fake_packet_loss_percentage(loss);
    server_writer_->set_fake_packet_loss_percentage(loss);
  }

  void SetPacketSendDelay(QuicTime::Delta delay) {
    client_writer_->set_fake_packet_delay(delay);
    server_writer_->set_fake_packet_delay(delay);
  }

  void SetReorderPercentage(int32_t reorder) {
    client_writer_->set_fake_reorder_percentage(reorder);
    server_writer_->set_fake_reorder_percentage(reorder);
  }

  // Verifies that the client and server connections were both free of packets
  // being discarded, based on connection stats.
  // Calls server_thread_ Pause() and Resume(), which may only be called once
  // per test.
  void VerifyCleanConnection(bool had_packet_loss) {
    QuicConnectionStats client_stats = GetClientConnection()->GetStats();
    // TODO(ianswett): Determine why this becomes even more flaky with BBR
    // enabled.  b/62141144
    if (!had_packet_loss && !GetQuicReloadableFlag(quic_default_to_bbr)) {
      EXPECT_EQ(0u, client_stats.packets_lost);
    }
    EXPECT_EQ(0u, client_stats.packets_discarded);
    // When client starts with an unsupported version, the version negotiation
    // packet sent by server for the old connection (respond for the connection
    // close packet) will be dropped by the client.
    if (!ServerSendsVersionNegotiation()) {
      EXPECT_EQ(0u, client_stats.packets_dropped);
    }
    if (!ClientSupportsIetfQuicNotSupportedByServer()) {
      // In this case, if client sends 0-RTT POST with v99, receives IETF
      // version negotiation packet and speaks a GQUIC version. Server processes
      // this connection in time wait list and keeps sending IETF version
      // negotiation packet for incoming packets. But these version negotiation
      // packets cannot be processed by the client speaking GQUIC.
      EXPECT_EQ(client_stats.packets_received, client_stats.packets_processed);
    }

    server_thread_->Pause();
    QuicConnectionStats server_stats = GetServerConnection()->GetStats();
    if (!had_packet_loss) {
      EXPECT_EQ(0u, server_stats.packets_lost);
    }
    EXPECT_EQ(0u, server_stats.packets_discarded);
    // TODO(ianswett): Restore the check for packets_dropped equals 0.
    // The expect for packets received is equal to packets processed fails
    // due to version negotiation packets.
    server_thread_->Resume();
  }

  // Client supports IETF QUIC, while it is not supported by server.
  bool ClientSupportsIetfQuicNotSupportedByServer() {
    return VersionHasIetfInvariantHeader(
               client_supported_versions_[0].transport_version) &&
           !VersionHasIetfInvariantHeader(
               FilterSupportedVersions(GetParam().server_supported_versions)[0]
                   .transport_version);
  }

  // Returns true when client starts with an unsupported version, and client
  // closes connection when version negotiation is received.
  bool ServerSendsVersionNegotiation() {
    return client_supported_versions_[0] != GetParam().negotiated_version;
  }

  bool SupportsIetfQuicWithTls(ParsedQuicVersion version) {
    return VersionHasIetfInvariantHeader(version.transport_version) &&
           version.handshake_protocol == PROTOCOL_TLS1_3;
  }

  static void ExpectFlowControlsSynced(QuicSession* client,
                                       QuicSession* server) {
    EXPECT_EQ(
        QuicFlowControllerPeer::SendWindowSize(client->flow_controller()),
        QuicFlowControllerPeer::ReceiveWindowSize(server->flow_controller()));
    EXPECT_EQ(
        QuicFlowControllerPeer::ReceiveWindowSize(client->flow_controller()),
        QuicFlowControllerPeer::SendWindowSize(server->flow_controller()));
  }

  static void ExpectFlowControlsSynced(QuicStream* client, QuicStream* server) {
    EXPECT_EQ(
        QuicFlowControllerPeer::SendWindowSize(client->flow_controller()),
        QuicFlowControllerPeer::ReceiveWindowSize(server->flow_controller()));
    EXPECT_EQ(
        QuicFlowControllerPeer::ReceiveWindowSize(client->flow_controller()),
        QuicFlowControllerPeer::SendWindowSize(server->flow_controller()));
  }

  // Must be called before Initialize to have effect.
  void SetSpdyStreamFactory(QuicTestServer::StreamFactory* factory) {
    stream_factory_ = factory;
  }

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return GetNthClientInitiatedBidirectionalStreamId(
        GetClientConnection()->transport_version(), n);
  }

  QuicStreamId GetNthServerInitiatedBidirectionalId(int n) {
    return GetNthServerInitiatedBidirectionalStreamId(
        GetClientConnection()->transport_version(), n);
  }

  ScopedEnvironmentForThreads environment_;
  bool initialized_;
  // If true, the Initialize() function will create |client_| and starts to
  // connect to the server.
  // Default is true.
  bool connect_to_server_on_initialize_;
  QuicSocketAddress server_address_;
  std::string server_hostname_;
  QuicMemoryCacheBackend memory_cache_backend_;
  std::unique_ptr<ServerThread> server_thread_;
  std::unique_ptr<QuicTestClient> client_;
  QuicConnectionDebugVisitor* connection_debug_visitor_ = nullptr;
  PacketDroppingTestWriter* client_writer_;
  PacketDroppingTestWriter* server_writer_;
  QuicConfig client_config_;
  QuicConfig server_config_;
  ParsedQuicVersionVector client_supported_versions_;
  ParsedQuicVersionVector server_supported_versions_;
  QuicTagVector client_extra_copts_;
  ParsedQuicVersion negotiated_version_;
  size_t chlo_multiplier_;
  QuicTestServer::StreamFactory* stream_factory_;
  std::string pre_shared_key_client_;
  std::string pre_shared_key_server_;
  int override_server_connection_id_length_ = -1;
  int override_client_connection_id_length_ = -1;
  uint8_t expected_server_connection_id_length_;
};

// Run all end to end tests with all supported versions.
INSTANTIATE_TEST_SUITE_P(EndToEndTests,
                         EndToEndTest,
                         ::testing::ValuesIn(GetTestParams(false)),
                         ::testing::PrintToStringParamName());

class EndToEndTestWithTls : public EndToEndTest {};

INSTANTIATE_TEST_SUITE_P(EndToEndTestsWithTls,
                         EndToEndTestWithTls,
                         ::testing::ValuesIn(GetTestParams(true)),
                         ::testing::PrintToStringParamName());

TEST_P(EndToEndTestWithTls, HandshakeSuccessful) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();
  // There have been occasions where it seemed that negotiated_version_ and the
  // version in the connection are not in sync. If it is happening, it has not
  // been recreatable; this assert is here just to check and raise a flag if it
  // happens.
  ASSERT_EQ(GetClientConnection()->transport_version(),
            negotiated_version_.transport_version);

  QuicCryptoStream* crypto_stream =
      QuicSessionPeer::GetMutableCryptoStream(GetClientSession());
  QuicStreamSequencer* sequencer = QuicStreamPeer::sequencer(crypto_stream);
  EXPECT_FALSE(QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
  server_thread_->Pause();
  crypto_stream = QuicSessionPeer::GetMutableCryptoStream(GetServerSession());
  sequencer = QuicStreamPeer::sequencer(crypto_stream);
  EXPECT_FALSE(QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
}

TEST_P(EndToEndTest, SimpleRequestResponse) {
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  if (VersionUsesHttp3(GetClientConnection()->transport_version())) {
    EXPECT_TRUE(QuicSpdySessionPeer::GetSendControlStream(GetClientSession()));
    EXPECT_TRUE(
        QuicSpdySessionPeer::GetReceiveControlStream(GetClientSession()));
    EXPECT_TRUE(QuicSpdySessionPeer::GetSendControlStream(GetServerSession()));
    EXPECT_TRUE(
        QuicSpdySessionPeer::GetReceiveControlStream(GetServerSession()));
  }
}

TEST_P(EndToEndTestWithTls, SimpleRequestResponse) {
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls, HandshakeConfirmed) {
  ASSERT_TRUE(Initialize());
  if (!GetParam().negotiated_version.HasHandshakeDone()) {
    return;
  }
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  // Verify handshake state.
  EXPECT_EQ(HANDSHAKE_CONFIRMED, GetClientSession()->GetHandshakeState());
  server_thread_->Pause();
  EXPECT_EQ(HANDSHAKE_CONFIRMED, GetServerSession()->GetHandshakeState());
  server_thread_->Resume();
  client_->Disconnect();
}

TEST_P(EndToEndTestWithTls, SendAndReceiveCoalescedPackets) {
  ASSERT_TRUE(Initialize());
  if (!GetClientConnection()->version().CanSendCoalescedPackets()) {
    return;
  }
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  // Verify client successfully processes coalesced packets.
  QuicConnectionStats client_stats = GetClientConnection()->GetStats();
  EXPECT_LT(0u, client_stats.num_coalesced_packets_received);
  EXPECT_EQ(client_stats.num_coalesced_packets_processed,
            client_stats.num_coalesced_packets_received);
  // TODO(fayang): verify server successfully processes coalesced packets.
}

// Simple transaction, but set a non-default ack delay at the client
// and ensure it gets to the server.
TEST_P(EndToEndTest, SimpleRequestResponseWithAckDelayChange) {
  // Force the ACK delay to be something other than the default.
  // Note that it is sent only if doing IETF QUIC.
  client_config_.SetMaxAckDelayToSendMs(kDefaultDelayedAckTimeMs + 100u);
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  if (GetQuicReloadableFlag(quic_negotiate_ack_delay_time)) {
    EXPECT_EQ(kDefaultDelayedAckTimeMs + 100u,
              GetSentPacketManagerFromFirstServerSession()
                  ->peer_max_ack_delay()
                  .ToMilliseconds());
  } else {
    EXPECT_EQ(kDefaultDelayedAckTimeMs,
              GetSentPacketManagerFromFirstServerSession()
                  ->peer_max_ack_delay()
                  .ToMilliseconds());
  }
}

// Simple transaction, but set a non-default ack exponent at the client
// and ensure it gets to the server.
TEST_P(EndToEndTest, SimpleRequestResponseWithAckExponentChange) {
  const uint32_t kClientAckDelayExponent = kDefaultAckDelayExponent + 100u;
  // Force the ACK exponent to be something other than the default.
  // Note that it is sent only if doing IETF QUIC.
  client_config_.SetAckDelayExponentToSend(kClientAckDelayExponent);
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  if (VersionHasIetfQuicFrames(
          GetParam().negotiated_version.transport_version)) {
    // Should be only for IETF QUIC.
    EXPECT_EQ(kClientAckDelayExponent,
              GetServerConnection()->framer().peer_ack_delay_exponent());
  } else {
    // No change for Google QUIC.
    EXPECT_EQ(kDefaultAckDelayExponent,
              GetServerConnection()->framer().peer_ack_delay_exponent());
  }
  // No change, regardless of version.
  EXPECT_EQ(kDefaultAckDelayExponent,
            GetServerConnection()->framer().local_ack_delay_exponent());
}

TEST_P(EndToEndTest, SimpleRequestResponseForcedVersionNegotiation) {
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  testing::NiceMock<MockQuicConnectionDebugVisitor> visitor;
  connection_debug_visitor_ = &visitor;
  EXPECT_CALL(visitor, OnVersionNegotiationPacket(testing::_)).Times(1);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
}

TEST_P(EndToEndTestWithTls, ForcedVersionNegotiation) {
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTest, SimpleRequestResponseZeroConnectionID) {
  if (!GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 0;
  expected_server_connection_id_length_ = 0;
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  EXPECT_EQ(GetClientConnection()->connection_id(),
            QuicUtils::CreateZeroConnectionId(
                GetParam().negotiated_version.transport_version));
}

TEST_P(EndToEndTestWithTls, ZeroConnectionID) {
  if (!GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 0;
  expected_server_connection_id_length_ = 0;
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(GetClientConnection()->connection_id(),
            QuicUtils::CreateZeroConnectionId(
                GetParam().negotiated_version.transport_version));
}

TEST_P(EndToEndTestWithTls, BadConnectionIdLength) {
  if (!GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 9;
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

// Tests a very long (16-byte) initial destination connection ID to make
// sure the dispatcher properly replaces it with an 8-byte one.
TEST_P(EndToEndTestWithTls, LongBadConnectionIdLength) {
  if (!GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 16;
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

TEST_P(EndToEndTestWithTls, ClientConnectionId) {
  if (!GetParam().negotiated_version.SupportsClientConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_client_connection_id_length_ = kQuicDefaultConnectionIdLength;
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(override_client_connection_id_length_, client_->client()
                                                       ->client_session()
                                                       ->connection()
                                                       ->client_connection_id()
                                                       .length());
}

TEST_P(EndToEndTestWithTls, ForcedVersionNegotiationAndClientConnectionId) {
  if (!GetParam().negotiated_version.SupportsClientConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  override_client_connection_id_length_ = kQuicDefaultConnectionIdLength;
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(override_client_connection_id_length_, client_->client()
                                                       ->client_session()
                                                       ->connection()
                                                       ->client_connection_id()
                                                       .length());
}

TEST_P(EndToEndTestWithTls, ForcedVersionNegotiationAndBadConnectionIdLength) {
  if (!GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  override_server_connection_id_length_ = 9;
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

// Forced Version Negotiation with a client connection ID and a long
// connection ID.
TEST_P(EndToEndTestWithTls, ForcedVersNegoAndClientCIDAndLongCID) {
  if (!GetParam().negotiated_version.SupportsClientConnectionIds() ||
      !GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  override_server_connection_id_length_ = 16;
  override_client_connection_id_length_ = 18;
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
  EXPECT_EQ(override_client_connection_id_length_, client_->client()
                                                       ->client_session()
                                                       ->connection()
                                                       ->client_connection_id()
                                                       .length());
}

TEST_P(EndToEndTest, MixGoodAndBadConnectionIdLengths) {
  if (!GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }

  // Start client_ which will use a bad connection ID length.
  override_server_connection_id_length_ = 9;
  ASSERT_TRUE(Initialize());
  override_server_connection_id_length_ = -1;

  // Start client2 which will use a good connection ID length.
  std::unique_ptr<QuicTestClient> client2(CreateQuicClient(nullptr));
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["content-length"] = "3";
  client2->SendMessage(headers, "", /*fin=*/false);
  client2->SendData("eep", true);

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());

  client2->WaitForResponse();
  EXPECT_EQ(kFooResponseBody, client2->response_body());
  EXPECT_EQ("200", client2->response_headers()->find(":status")->second);
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client2->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

TEST_P(EndToEndTestWithTls, SimpleRequestResponseWithIetfDraftSupport) {
  if (!GetParam().negotiated_version.HasIetfQuicFrames()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  QuicVersionInitializeSupportForIetfDraft();
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTest, SimpleRequestResponseWithLargeReject) {
  chlo_multiplier_ = 1;
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->ReceivedInchoateReject());
}

TEST_P(EndToEndTestWithTls, SimpleRequestResponsev6) {
  server_address_ =
      QuicSocketAddress(QuicIpAddress::Loopback6(), server_address_.port());
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls,
       ClientDoesNotAllowServerDataOnServerInitiatedBidirectionalStreams) {
  set_client_initial_max_stream_data_incoming_bidirectional(0);
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls,
       ServerDoesNotAllowClientDataOnServerInitiatedBidirectionalStreams) {
  set_server_initial_max_stream_data_outgoing_bidirectional(0);
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls,
       BothEndpointsDisallowDataOnServerInitiatedBidirectionalStreams) {
  set_client_initial_max_stream_data_incoming_bidirectional(0);
  set_server_initial_max_stream_data_outgoing_bidirectional(0);
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

// Regression test for a bug where we would always fail to decrypt the first
// initial packet. Undecryptable packets can be seen after the handshake
// is complete due to dropping the initial keys at that point, so we only test
// for undecryptable packets before then.
TEST_P(EndToEndTestWithTls, NoUndecryptablePacketsBeforeHandshakeComplete) {
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  QuicConnectionStats client_stats = GetClientConnection()->GetStats();
  EXPECT_EQ(
      0u,
      client_stats.undecryptable_packets_received_before_handshake_complete);

  server_thread_->Pause();
  QuicConnectionStats server_stats = GetServerConnection()->GetStats();
  EXPECT_EQ(
      0u,
      server_stats.undecryptable_packets_received_before_handshake_complete);
  server_thread_->Resume();
}

TEST_P(EndToEndTestWithTls, SeparateFinPacket) {
  ASSERT_TRUE(Initialize());

  // Send a request in two parts: the request and then an empty packet with FIN.
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  client_->SendMessage(headers, "", /*fin=*/false);
  client_->SendData("", true);
  client_->WaitForResponse();
  EXPECT_EQ(kFooResponseBody, client_->response_body());
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Now do the same thing but with a content length.
  headers["content-length"] = "3";
  client_->SendMessage(headers, "", /*fin=*/false);
  client_->SendData("foo", true);
  client_->WaitForResponse();
  EXPECT_EQ(kFooResponseBody, client_->response_body());
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls, MultipleRequestResponse) {
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTest, MultipleRequestResponseZeroConnectionID) {
  if (!GetParam().negotiated_version.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 0;
  expected_server_connection_id_length_ = 0;
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls, MultipleStreams) {
  // Verifies quic_test_client can track responses of all active streams.
  ASSERT_TRUE(Initialize());

  const int kNumRequests = 10;

  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["content-length"] = "3";

  for (int i = 0; i < kNumRequests; ++i) {
    client_->SendMessage(headers, "bar", /*fin=*/true);
  }

  while (kNumRequests > client_->num_responses()) {
    client_->ClearPerRequestState();
    client_->WaitForResponse();
    EXPECT_EQ(kFooResponseBody, client_->response_body());
    EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  }
}

TEST_P(EndToEndTestWithTls, MultipleClients) {
  ASSERT_TRUE(Initialize());
  std::unique_ptr<QuicTestClient> client2(CreateQuicClient(nullptr));

  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["content-length"] = "3";

  client_->SendMessage(headers, "", /*fin=*/false);
  client2->SendMessage(headers, "", /*fin=*/false);

  client_->SendData("bar", true);
  client_->WaitForResponse();
  EXPECT_EQ(kFooResponseBody, client_->response_body());
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  client2->SendData("eep", true);
  client2->WaitForResponse();
  EXPECT_EQ(kFooResponseBody, client2->response_body());
  EXPECT_EQ("200", client2->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls, RequestOverMultiplePackets) {
  // Send a large enough request to guarantee fragmentation.
  std::string huge_request =
      "/some/path?query=" + std::string(kMaxOutgoingPacketSize, '.');
  AddToCache(huge_request, 200, kBarResponseBody);

  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest(huge_request));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls, MultiplePacketsRandomOrder) {
  // Send a large enough request to guarantee fragmentation.
  std::string huge_request =
      "/some/path?query=" + std::string(kMaxOutgoingPacketSize, '.');
  AddToCache(huge_request, 200, kBarResponseBody);

  ASSERT_TRUE(Initialize());
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(50);

  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest(huge_request));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls, PostMissingBytes) {
  ASSERT_TRUE(Initialize());

  // Add a content length header with no body.
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["content-length"] = "3";

  // This should be detected as stream fin without complete request,
  // triggering an error response.
  client_->SendCustomSynchronousRequest(headers, "");
  EXPECT_EQ(QuicSimpleServerStream::kErrorResponseBody,
            client_->response_body());
  EXPECT_EQ("500", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTest, LargePostNoPacketLoss) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // 1 MB body.
  std::string body(1024 * 1024, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  // TODO(ianswett): There should not be packet loss in this test, but on some
  // platforms the receive buffer overflows.
  VerifyCleanConnection(true);
}

TEST_P(EndToEndTest, LargePostNoPacketLoss1sRTT) {
  ASSERT_TRUE(Initialize());
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(1000));

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // 100 KB body.
  std::string body(100 * 1024, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  VerifyCleanConnection(false);
}

TEST_P(EndToEndTest, LargePostWithPacketLoss) {
  // Connect with lower fake packet loss than we'd like to test.
  // Until b/10126687 is fixed, losing handshake packets is pretty
  // brutal.
  SetPacketLossPercentage(5);
  ASSERT_TRUE(Initialize());

  // Wait for the server SHLO before upping the packet loss.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  SetPacketLossPercentage(30);

  // 10 KB body.
  std::string body(1024 * 10, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  VerifyCleanConnection(true);
}

// Regression test for b/80090281.
TEST_P(EndToEndTest, LargePostWithPacketLossAndAlwaysBundleWindowUpdates) {
  ASSERT_TRUE(Initialize());

  // Wait for the server SHLO before upping the packet loss.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Normally server only bundles a retransmittable frame once every other
  // kMaxConsecutiveNonRetransmittablePackets ack-only packets. Setting the max
  // to 0 to reliably reproduce b/80090281.
  server_thread_->Schedule([this]() {
    QuicConnectionPeer::SetMaxConsecutiveNumPacketsWithNoRetransmittableFrames(
        GetServerConnection(), 0);
  });

  SetPacketLossPercentage(30);

  // 10 KB body.
  std::string body(1024 * 10, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  VerifyCleanConnection(true);
}

TEST_P(EndToEndTest, LargePostWithPacketLossAndBlockedSocket) {
  // Connect with lower fake packet loss than we'd like to test.  Until
  // b/10126687 is fixed, losing handshake packets is pretty brutal.
  SetPacketLossPercentage(5);
  ASSERT_TRUE(Initialize());

  // Wait for the server SHLO before upping the packet loss.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  SetPacketLossPercentage(10);
  client_writer_->set_fake_blocked_socket_percentage(10);

  // 10 KB body.
  std::string body(1024 * 10, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
}

TEST_P(EndToEndTest, LargePostNoPacketLossWithDelayAndReordering) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  // Both of these must be called when the writer is not actively used.
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(30);

  // 1 MB body.
  std::string body(1024 * 1024, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
}

TEST_P(EndToEndTest, LargePostZeroRTTFailure) {
  // Send a request and then disconnect. This prepares the client to attempt
  // a 0-RTT handshake for the next request.
  ASSERT_TRUE(Initialize());

  std::string body(20480, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  EXPECT_FALSE(GetClientSession()->EarlyDataAccepted());
  EXPECT_FALSE(GetClientSession()->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));

  EXPECT_TRUE(GetClientSession()->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  // Restart the server so that the 0-RTT handshake will take 1 RTT.
  StopServer();
  server_writer_ = new PacketDroppingTestWriter();
  StartServer();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  EXPECT_FALSE(GetClientSession()->EarlyDataAccepted());
  EXPECT_FALSE(GetClientSession()->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  VerifyCleanConnection(false);
}

TEST_P(EndToEndTest, SynchronousRequestZeroRTTFailure) {
  // Send a request and then disconnect. This prepares the client to attempt
  // a 0-RTT handshake for the next request.
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_FALSE(GetClientSession()->EarlyDataAccepted());
  EXPECT_FALSE(GetClientSession()->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  EXPECT_TRUE(GetClientSession()->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  // Restart the server so that the 0-RTT handshake will take 1 RTT.
  StopServer();
  server_writer_ = new PacketDroppingTestWriter();
  StartServer();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  EXPECT_FALSE(GetClientSession()->EarlyDataAccepted());
  EXPECT_FALSE(GetClientSession()->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  VerifyCleanConnection(false);
}

TEST_P(EndToEndTest, LargePostSynchronousRequest) {
  // Send a request and then disconnect. This prepares the client to attempt
  // a 0-RTT handshake for the next request.
  ASSERT_TRUE(Initialize());

  std::string body(20480, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  EXPECT_FALSE(GetClientSession()->EarlyDataAccepted());
  EXPECT_FALSE(GetClientSession()->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));

  EXPECT_TRUE(GetClientSession()->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  // Restart the server so that the 0-RTT handshake will take 1 RTT.
  StopServer();
  server_writer_ = new PacketDroppingTestWriter();
  StartServer();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));

  EXPECT_FALSE(GetClientSession()->EarlyDataAccepted());
  EXPECT_FALSE(GetClientSession()->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  VerifyCleanConnection(false);
}

TEST_P(EndToEndTest, RejectWithPacketLoss) {
  // In this test, we intentionally drop the first packet from the
  // server, which corresponds with the initial REJ response from
  // the server.
  server_writer_->set_fake_drop_first_n_packets(1);
  ASSERT_TRUE(Initialize());
}

TEST_P(EndToEndTest, SetInitialReceivedConnectionOptions) {
  QuicTagVector initial_received_options;
  initial_received_options.push_back(kTBBR);
  initial_received_options.push_back(kIW10);
  initial_received_options.push_back(kPRST);
  EXPECT_TRUE(server_config_.SetInitialReceivedConnectionOptions(
      initial_received_options));

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  EXPECT_FALSE(server_config_.SetInitialReceivedConnectionOptions(
      initial_received_options));

  // Verify that server's configuration is correct.
  server_thread_->Pause();
  EXPECT_TRUE(server_config_.HasReceivedConnectionOptions());
  EXPECT_TRUE(
      ContainsQuicTag(server_config_.ReceivedConnectionOptions(), kTBBR));
  EXPECT_TRUE(
      ContainsQuicTag(server_config_.ReceivedConnectionOptions(), kIW10));
  EXPECT_TRUE(
      ContainsQuicTag(server_config_.ReceivedConnectionOptions(), kPRST));
}

TEST_P(EndToEndTest, LargePostSmallBandwidthLargeBuffer) {
  ASSERT_TRUE(Initialize());
  SetPacketSendDelay(QuicTime::Delta::FromMicroseconds(1));
  // 256KB per second with a 256KB buffer from server to client.  Wireless
  // clients commonly have larger buffers, but our max CWND is 200.
  server_writer_->set_max_bandwidth_and_buffer_size(
      QuicBandwidth::FromBytesPerSecond(256 * 1024), 256 * 1024);

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // 1 MB body.
  std::string body(1024 * 1024, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  // This connection may drop packets, because the buffer is smaller than the
  // max CWND.
  VerifyCleanConnection(true);
}

TEST_P(EndToEndTestWithTls, DoNotSetSendAlarmIfConnectionFlowControlBlocked) {
  // Regression test for b/14677858.
  // Test that the resume write alarm is not set in QuicConnection::OnCanWrite
  // if currently connection level flow control blocked. If set, this results in
  // an infinite loop in the EpollServer, as the alarm fires and is immediately
  // rescheduled.
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // Ensure both stream and connection level are flow control blocked by setting
  // the send window offset to 0.
  const uint64_t flow_control_window =
      server_config_.GetInitialStreamFlowControlWindowToSend();
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  QuicSession* session = GetClientSession();
  QuicFlowControllerPeer::SetSendWindowOffset(stream->flow_controller(), 0);
  QuicFlowControllerPeer::SetSendWindowOffset(session->flow_controller(), 0);
  EXPECT_TRUE(stream->flow_controller()->IsBlocked());
  EXPECT_TRUE(session->flow_controller()->IsBlocked());

  // Make sure that the stream has data pending so that it will be marked as
  // write blocked when it receives a stream level WINDOW_UPDATE.
  stream->WriteOrBufferBody("hello", false);

  // The stream now attempts to write, fails because it is still connection
  // level flow control blocked, and is added to the write blocked list.
  QuicWindowUpdateFrame window_update(kInvalidControlFrameId, stream->id(),
                                      2 * flow_control_window);
  stream->OnWindowUpdateFrame(window_update);

  // Prior to fixing b/14677858 this call would result in an infinite loop in
  // Chromium. As a proxy for detecting this, we now check whether the
  // send alarm is set after OnCanWrite. It should not be, as the
  // connection is still flow control blocked.
  session->connection()->OnCanWrite();

  QuicAlarm* send_alarm =
      QuicConnectionPeer::GetSendAlarm(session->connection());
  EXPECT_FALSE(send_alarm->IsSet());
}

// TODO(nharper): Needs to get turned back to EndToEndTestWithTls
// when we figure out why the test doesn't work on chrome.
TEST_P(EndToEndTest, InvalidStream) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  std::string body(kMaxOutgoingPacketSize, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  // Force the client to write with a stream ID belonging to a nonexistent
  // server-side stream.
  QuicSpdySession* session = GetClientSession();
  QuicSessionPeer::SetNextOutgoingBidirectionalStreamId(
      session, GetNthServerInitiatedBidirectionalId(0));

  client_->SendCustomSynchronousRequest(headers, body);
  EXPECT_THAT(client_->stream_error(),
              IsStreamError(QUIC_STREAM_CONNECTION_ERROR));
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_INVALID_STREAM_ID));
}

// Test that if the server will close the connection if the client attempts
// to send a request with overly large headers.
TEST_P(EndToEndTest, LargeHeaders) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  std::string body(kMaxOutgoingPacketSize, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["key1"] = std::string(15 * 1024, 'a');
  headers["key2"] = std::string(15 * 1024, 'a');
  headers["key3"] = std::string(15 * 1024, 'a');

  client_->SendCustomSynchronousRequest(headers, body);

  if (VersionUsesHttp3(client_->client()
                           ->client_session()
                           ->connection()
                           ->transport_version())) {
    EXPECT_THAT(client_->connection_error(),
                IsError(QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE));
  } else {
    EXPECT_THAT(client_->stream_error(), IsStreamError(QUIC_HEADERS_TOO_LARGE));
    EXPECT_THAT(client_->connection_error(), IsQuicNoError());
  }
}

TEST_P(EndToEndTest, EarlyResponseWithQuicStreamNoError) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  std::string large_body(1024 * 1024, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  // Insert an invalid content_length field in request to trigger an early
  // response from server.
  headers["content-length"] = "-3";

  client_->SendCustomSynchronousRequest(headers, large_body);
  EXPECT_EQ("bad", client_->response_body());
  EXPECT_EQ("500", client_->response_headers()->find(":status")->second);
  EXPECT_THAT(client_->stream_error(), IsQuicStreamNoError());
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

// TODO(rch): this test seems to cause net_unittests timeouts :|
TEST_P(EndToEndTestWithTls, QUIC_TEST_DISABLED_IN_CHROME(MultipleTermination)) {
  ASSERT_TRUE(Initialize());

  // Set the offset so we won't frame.  Otherwise when we pick up termination
  // before HTTP framing is complete, we send an error and close the stream,
  // and the second write is picked up as writing on a closed stream.
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  ASSERT_TRUE(stream != nullptr);
  QuicStreamPeer::SetStreamBytesWritten(3, stream);

  client_->SendData("bar", true);
  client_->WaitForWriteToFlush();

  // By default the stream protects itself from writes after terminte is set.
  // Override this to test the server handling buggy clients.
  QuicStreamPeer::SetWriteSideClosed(false, client_->GetOrCreateStream());

  EXPECT_QUIC_BUG(client_->SendData("eep", true), "Fin already buffered");
}

// TODO(nharper): Needs to get turned back to EndToEndTestWithTls
// when we figure out why the test doesn't work on chrome.
TEST_P(EndToEndTest, Timeout) {
  client_config_.SetIdleNetworkTimeout(QuicTime::Delta::FromMicroseconds(500),
                                       QuicTime::Delta::FromMicroseconds(500));
  // Note: we do NOT ASSERT_TRUE: we may time out during initial handshake:
  // that's enough to validate timeout in this case.
  Initialize();
  while (client_->client()->connected()) {
    client_->client()->WaitForEvents();
  }
}

TEST_P(EndToEndTestWithTls, MaxDynamicStreamsLimitRespected) {
  // Set a limit on maximum number of incoming dynamic streams.
  // Make sure the limit is respected by the peer.
  const uint32_t kServerMaxDynamicStreams = 1;
  server_config_.SetMaxBidirectionalStreamsToSend(kServerMaxDynamicStreams);
  ASSERT_TRUE(Initialize());
  if (VersionHasIetfQuicFrames(
          GetParam().negotiated_version.transport_version)) {
    // Do not run this test for /IETF QUIC. This test relies on the fact that
    // Google QUIC allows a small number of additional streams beyond the
    // negotiated limit, which is not supported in IETF QUIC. Note that the test
    // needs to be here, after calling Initialize(), because all tests end up
    // calling EndToEndTest::TearDown(), which asserts that Initialize has been
    // called and then proceeds to tear things down -- which fails if they are
    // not properly set up.
    return;
  }
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // Make the client misbehave after negotiation.
  const int kServerMaxStreams = kMaxStreamsMinimumIncrement + 1;
  QuicSessionPeer::SetMaxOpenOutgoingStreams(GetClientSession(),
                                             kServerMaxStreams + 1);

  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["content-length"] = "3";

  // The server supports a small number of additional streams beyond the
  // negotiated limit. Open enough streams to go beyond that limit.
  for (int i = 0; i < kServerMaxStreams + 1; ++i) {
    client_->SendMessage(headers, "", /*fin=*/false);
  }
  client_->WaitForResponse();

  EXPECT_TRUE(client_->connected());
  EXPECT_THAT(client_->stream_error(), IsStreamError(QUIC_REFUSED_STREAM));
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

TEST_P(EndToEndTest, SetIndependentMaxDynamicStreamsLimits) {
  // Each endpoint can set max dynamic streams independently.
  const uint32_t kClientMaxDynamicStreams = 4;
  const uint32_t kServerMaxDynamicStreams = 3;
  client_config_.SetMaxBidirectionalStreamsToSend(kClientMaxDynamicStreams);
  server_config_.SetMaxBidirectionalStreamsToSend(kServerMaxDynamicStreams);
  client_config_.SetMaxUnidirectionalStreamsToSend(kClientMaxDynamicStreams);
  server_config_.SetMaxUnidirectionalStreamsToSend(kServerMaxDynamicStreams);

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // The client has received the server's limit and vice versa.
  QuicSpdyClientSession* client_session = GetClientSession();
  // The value returned by max_allowed... includes the Crypto and Header
  // stream (created as a part of initialization). The config. values,
  // above, are treated as "number of requests/responses" - that is, they do
  // not include the static Crypto and Header streams. Reduce the value
  // returned by max_allowed... by 2 to remove the static streams from the
  // count.
  size_t client_max_open_outgoing_bidirectional_streams =
      VersionHasIetfQuicFrames(
          client_session->connection()->transport_version())
          ? QuicSessionPeer::v99_streamid_manager(client_session)
                ->max_outgoing_bidirectional_streams()
          : QuicSessionPeer::GetStreamIdManager(client_session)
                ->max_open_outgoing_streams();
  size_t client_max_open_outgoing_unidirectional_streams =
      VersionHasIetfQuicFrames(
          client_session->connection()->transport_version())
          ? QuicSessionPeer::v99_streamid_manager(client_session)
                    ->max_outgoing_unidirectional_streams() -
                client_session->num_expected_unidirectional_static_streams()
          : QuicSessionPeer::GetStreamIdManager(client_session)
                ->max_open_outgoing_streams();
  EXPECT_EQ(kServerMaxDynamicStreams,
            client_max_open_outgoing_bidirectional_streams);
  EXPECT_EQ(kServerMaxDynamicStreams,
            client_max_open_outgoing_unidirectional_streams);
  server_thread_->Pause();
  QuicSession* server_session = GetServerSession();
  size_t server_max_open_outgoing_bidirectional_streams =
      VersionHasIetfQuicFrames(
          server_session->connection()->transport_version())
          ? QuicSessionPeer::v99_streamid_manager(server_session)
                ->max_outgoing_bidirectional_streams()
          : QuicSessionPeer::GetStreamIdManager(server_session)
                ->max_open_outgoing_streams();
  size_t server_max_open_outgoing_unidirectional_streams =
      VersionHasIetfQuicFrames(
          server_session->connection()->transport_version())
          ? QuicSessionPeer::v99_streamid_manager(server_session)
                    ->max_outgoing_unidirectional_streams() -
                server_session->num_expected_unidirectional_static_streams()
          : QuicSessionPeer::GetStreamIdManager(server_session)
                ->max_open_outgoing_streams();
  EXPECT_EQ(kClientMaxDynamicStreams,
            server_max_open_outgoing_bidirectional_streams);
  EXPECT_EQ(kClientMaxDynamicStreams,
            server_max_open_outgoing_unidirectional_streams);

  server_thread_->Resume();
}

TEST_P(EndToEndTest, NegotiateCongestionControl) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  CongestionControlType expected_congestion_control_type = kRenoBytes;
  switch (GetParam().congestion_control_tag) {
    case kRENO:
      expected_congestion_control_type = kRenoBytes;
      break;
    case kTBBR:
      expected_congestion_control_type = kBBR;
      break;
    case kQBIC:
      expected_congestion_control_type = kCubicBytes;
      break;
    case kB2ON:
      expected_congestion_control_type = kBBRv2;
      break;
    default:
      QUIC_DLOG(FATAL) << "Unexpected congestion control tag";
  }

  server_thread_->Pause();
  EXPECT_EQ(expected_congestion_control_type,
            QuicSentPacketManagerPeer::GetSendAlgorithm(
                *GetSentPacketManagerFromFirstServerSession())
                ->GetCongestionControlType());
  server_thread_->Resume();
}

TEST_P(EndToEndTest, ClientSuggestsRTT) {
  // Client suggests initial RTT, verify it is used.
  const QuicTime::Delta kInitialRTT = QuicTime::Delta::FromMicroseconds(20000);
  client_config_.SetInitialRoundTripTimeUsToSend(kInitialRTT.ToMicroseconds());

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  ASSERT_EQ(1u, dispatcher->session_map().size());
  const QuicSentPacketManager& client_sent_packet_manager =
      GetClientConnection()->sent_packet_manager();
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();

  EXPECT_EQ(kInitialRTT,
            client_sent_packet_manager.GetRttStats()->initial_rtt());
  EXPECT_EQ(kInitialRTT,
            server_sent_packet_manager->GetRttStats()->initial_rtt());
  server_thread_->Resume();
}

TEST_P(EndToEndTest, ClientSuggestsIgnoredRTT) {
  // Client suggests initial RTT, but also specifies NRTT, so it's not used.
  const QuicTime::Delta kInitialRTT = QuicTime::Delta::FromMicroseconds(20000);
  client_config_.SetInitialRoundTripTimeUsToSend(kInitialRTT.ToMicroseconds());
  QuicTagVector options;
  options.push_back(kNRTT);
  client_config_.SetConnectionOptionsToSend(options);

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  ASSERT_EQ(1u, dispatcher->session_map().size());
  const QuicSentPacketManager& client_sent_packet_manager =
      GetClientConnection()->sent_packet_manager();
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();

  EXPECT_EQ(kInitialRTT,
            client_sent_packet_manager.GetRttStats()->initial_rtt());
  EXPECT_EQ(kInitialRTT,
            server_sent_packet_manager->GetRttStats()->initial_rtt());
  server_thread_->Resume();
}

TEST_P(EndToEndTest, MaxInitialRTT) {
  // Client tries to suggest twice the server's max initial rtt and the server
  // uses the max.
  client_config_.SetInitialRoundTripTimeUsToSend(2 *
                                                 kMaxInitialRoundTripTimeUs);

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  const QuicSentPacketManager& client_sent_packet_manager =
      GetClientConnection()->sent_packet_manager();

  // Now that acks have been exchanged, the RTT estimate has decreased on the
  // server and is not infinite on the client.
  EXPECT_FALSE(
      client_sent_packet_manager.GetRttStats()->smoothed_rtt().IsInfinite());
  const RttStats& server_rtt_stats =
      *GetServerConnection()->sent_packet_manager().GetRttStats();
  EXPECT_EQ(static_cast<int64_t>(kMaxInitialRoundTripTimeUs),
            server_rtt_stats.initial_rtt().ToMicroseconds());
  EXPECT_GE(static_cast<int64_t>(kMaxInitialRoundTripTimeUs),
            server_rtt_stats.smoothed_rtt().ToMicroseconds());
  server_thread_->Resume();
}

TEST_P(EndToEndTest, MinInitialRTT) {
  // Client tries to suggest 0 and the server uses the default.
  client_config_.SetInitialRoundTripTimeUsToSend(0);

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  const QuicSentPacketManager& client_sent_packet_manager =
      GetClientConnection()->sent_packet_manager();
  const QuicSentPacketManager& server_sent_packet_manager =
      GetServerConnection()->sent_packet_manager();

  // Now that acks have been exchanged, the RTT estimate has decreased on the
  // server and is not infinite on the client.
  EXPECT_FALSE(
      client_sent_packet_manager.GetRttStats()->smoothed_rtt().IsInfinite());
  // Expect the default rtt of 100ms.
  EXPECT_EQ(QuicTime::Delta::FromMilliseconds(100),
            server_sent_packet_manager.GetRttStats()->initial_rtt());
  // Ensure the bandwidth is valid.
  client_sent_packet_manager.BandwidthEstimate();
  server_sent_packet_manager.BandwidthEstimate();
  server_thread_->Resume();
}

TEST_P(EndToEndTest, 0ByteConnectionId) {
  if (VersionHasIetfInvariantHeader(
          GetParam().negotiated_version.transport_version)) {
    // SetBytesForConnectionIdToSend only applies to Google QUIC encoding.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetBytesForConnectionIdToSend(0);
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  QuicConnection* client_connection = GetClientConnection();
  QuicPacketHeader* header =
      QuicConnectionPeer::GetLastHeader(client_connection);
  EXPECT_EQ(CONNECTION_ID_ABSENT, header->source_connection_id_included);
}

TEST_P(EndToEndTestWithTls, 8ByteConnectionId) {
  if (VersionHasIetfInvariantHeader(
          GetParam().negotiated_version.transport_version)) {
    // SetBytesForConnectionIdToSend only applies to Google QUIC encoding.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetBytesForConnectionIdToSend(8);
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  QuicConnection* client_connection = GetClientConnection();
  QuicPacketHeader* header =
      QuicConnectionPeer::GetLastHeader(client_connection);
  EXPECT_EQ(CONNECTION_ID_PRESENT, header->destination_connection_id_included);
}

TEST_P(EndToEndTestWithTls, 15ByteConnectionId) {
  if (VersionHasIetfInvariantHeader(
          GetParam().negotiated_version.transport_version)) {
    // SetBytesForConnectionIdToSend only applies to Google QUIC encoding.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetBytesForConnectionIdToSend(15);
  ASSERT_TRUE(Initialize());

  // Our server is permissive and allows for out of bounds values.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  QuicConnection* client_connection = GetClientConnection();
  QuicPacketHeader* header =
      QuicConnectionPeer::GetLastHeader(client_connection);
  EXPECT_EQ(CONNECTION_ID_PRESENT, header->destination_connection_id_included);
}

TEST_P(EndToEndTestWithTls, ResetConnection) {
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  client_->ResetConnection();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

// TODO(nharper): Needs to get turned back to EndToEndTestWithTls
// when we figure out why the test doesn't work on chrome.
TEST_P(EndToEndTest, MaxStreamsUberTest) {
  // Connect with lower fake packet loss than we'd like to test.  Until
  // b/10126687 is fixed, losing handshake packets is pretty brutal.
  SetPacketLossPercentage(1);
  ASSERT_TRUE(Initialize());
  std::string large_body(10240, 'a');
  int max_streams = 100;

  AddToCache("/large_response", 200, large_body);

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  SetPacketLossPercentage(10);

  for (int i = 0; i < max_streams; ++i) {
    EXPECT_LT(0, client_->SendRequest("/large_response"));
  }

  // WaitForEvents waits 50ms and returns true if there are outstanding
  // requests.
  while (client_->client()->WaitForEvents() == true) {
  }
}

TEST_P(EndToEndTestWithTls, StreamCancelErrorTest) {
  ASSERT_TRUE(Initialize());
  std::string small_body(256, 'a');

  AddToCache("/small_response", 200, small_body);

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  QuicSession* session = GetClientSession();
  // Lose the request.
  SetPacketLossPercentage(100);
  EXPECT_LT(0, client_->SendRequest("/small_response"));
  client_->client()->WaitForEvents();
  // Transmit the cancel, and ensure the connection is torn down properly.
  SetPacketLossPercentage(0);
  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  session->SendRstStream(stream_id, QUIC_STREAM_CANCELLED, 0);

  // WaitForEvents waits 50ms and returns true if there are outstanding
  // requests.
  while (client_->client()->WaitForEvents() == true) {
  }
  // It should be completely fine to RST a stream before any data has been
  // received for that stream.
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

TEST_P(EndToEndTest, ConnectionMigrationClientIPChanged) {
  ASSERT_TRUE(Initialize());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Store the client IP address which was used to send the first request.
  QuicIpAddress old_host =
      client_->client()->network_helper()->GetLatestClientAddress().host();

  // Migrate socket to the new IP address.
  QuicIpAddress new_host = TestLoopback(2);
  EXPECT_NE(old_host, new_host);
  ASSERT_TRUE(client_->client()->MigrateSocket(new_host));

  // Send a request using the new socket.
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTest, ConnectionMigrationClientPortChanged) {
  // Tests that the client's port can change during an established QUIC
  // connection, and that doing so does not result in the connection being
  // closed by the server.
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Store the client address which was used to send the first request.
  QuicSocketAddress old_address =
      client_->client()->network_helper()->GetLatestClientAddress();
  int old_fd = client_->client()->GetLatestFD();

  // Create a new socket before closing the old one, which will result in a new
  // ephemeral port.
  QuicClientPeer::CreateUDPSocketAndBind(client_->client());

  // Stop listening and close the old FD.
  QuicClientPeer::CleanUpUDPSocket(client_->client(), old_fd);

  // The packet writer needs to be updated to use the new FD.
  client_->client()->network_helper()->CreateQuicPacketWriter();

  // Change the internal state of the client and connection to use the new port,
  // this is done because in a real NAT rebinding the client wouldn't see any
  // port change, and so expects no change to incoming port.
  // This is kind of ugly, but needed as we are simply swapping out the client
  // FD rather than any more complex NAT rebinding simulation.
  int new_port =
      client_->client()->network_helper()->GetLatestClientAddress().port();
  QuicClientPeer::SetClientPort(client_->client(), new_port);
  QuicConnectionPeer::SetSelfAddress(GetClientConnection(),
                                     QuicSocketAddress(client_->client()
                                                           ->client_session()
                                                           ->connection()
                                                           ->self_address()
                                                           .host(),
                                                       new_port));

  // Register the new FD for epoll events.
  int new_fd = client_->client()->GetLatestFD();
  QuicEpollServer* eps = client_->epoll_server();
  eps->RegisterFD(new_fd, client_->client()->epoll_network_helper(),
                  EPOLLIN | EPOLLOUT | EPOLLET);

  // Send a second request, using the new FD.
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Verify that the client's ephemeral port is different.
  QuicSocketAddress new_address =
      client_->client()->network_helper()->GetLatestClientAddress();
  EXPECT_EQ(old_address.host(), new_address.host());
  EXPECT_NE(old_address.port(), new_address.port());
}

TEST_P(EndToEndTest, NegotiatedInitialCongestionWindow) {
  SetQuicReloadableFlag(quic_unified_iw_options, true);
  client_extra_copts_.push_back(kIW03);

  ASSERT_TRUE(Initialize());

  // Values are exchanged during crypto handshake, so wait for that to finish.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();
  server_thread_->Pause();

  QuicPacketCount cwnd =
      GetServerConnection()->sent_packet_manager().initial_congestion_window();
  EXPECT_EQ(3u, cwnd);
}

TEST_P(EndToEndTest, DifferentFlowControlWindows) {
  // Client and server can set different initial flow control receive windows.
  // These are sent in CHLO/SHLO. Tests that these values are exchanged properly
  // in the crypto handshake.
  const uint32_t kClientStreamIFCW = 123456;
  const uint32_t kClientSessionIFCW = 234567;
  set_client_initial_stream_flow_control_receive_window(kClientStreamIFCW);
  set_client_initial_session_flow_control_receive_window(kClientSessionIFCW);

  uint32_t kServerStreamIFCW = 32 * 1024;
  uint32_t kServerSessionIFCW = 48 * 1024;
  set_server_initial_stream_flow_control_receive_window(kServerStreamIFCW);
  set_server_initial_session_flow_control_receive_window(kServerSessionIFCW);

  ASSERT_TRUE(Initialize());

  // Values are exchanged during crypto handshake, so wait for that to finish.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Open a data stream to make sure the stream level flow control is updated.
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  WriteHeadersOnStream(stream);
  stream->WriteOrBufferBody("hello", false);

  // Client should have the right values for server's receive window.
  EXPECT_EQ(kServerStreamIFCW,
            client_->client()
                ->client_session()
                ->config()
                ->ReceivedInitialStreamFlowControlWindowBytes());
  EXPECT_EQ(kServerSessionIFCW,
            client_->client()
                ->client_session()
                ->config()
                ->ReceivedInitialSessionFlowControlWindowBytes());
  EXPECT_EQ(kServerStreamIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                   stream->flow_controller()));
  EXPECT_EQ(kServerSessionIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                    GetClientSession()->flow_controller()));

  // Server should have the right values for client's receive window.
  server_thread_->Pause();
  QuicSession* session = GetServerSession();
  EXPECT_EQ(kClientStreamIFCW,
            session->config()->ReceivedInitialStreamFlowControlWindowBytes());
  EXPECT_EQ(kClientSessionIFCW,
            session->config()->ReceivedInitialSessionFlowControlWindowBytes());
  EXPECT_EQ(kClientSessionIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                    session->flow_controller()));
  server_thread_->Resume();
}

// Test negotiation of IFWA connection option.
TEST_P(EndToEndTest, NegotiatedServerInitialFlowControlWindow) {
  const uint32_t kClientStreamIFCW = 123456;
  const uint32_t kClientSessionIFCW = 234567;
  set_client_initial_stream_flow_control_receive_window(kClientStreamIFCW);
  set_client_initial_session_flow_control_receive_window(kClientSessionIFCW);

  uint32_t kServerStreamIFCW = 32 * 1024;
  uint32_t kServerSessionIFCW = 48 * 1024;
  set_server_initial_stream_flow_control_receive_window(kServerStreamIFCW);
  set_server_initial_session_flow_control_receive_window(kServerSessionIFCW);

  // Bump the window.
  const uint32_t kExpectedStreamIFCW = 1024 * 1024;
  const uint32_t kExpectedSessionIFCW = 1.5 * 1024 * 1024;
  client_extra_copts_.push_back(kIFWA);

  ASSERT_TRUE(Initialize());

  // Values are exchanged during crypto handshake, so wait for that to finish.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Open a data stream to make sure the stream level flow control is updated.
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  WriteHeadersOnStream(stream);
  stream->WriteOrBufferBody("hello", false);

  // Client should have the right values for server's receive window.
  EXPECT_EQ(kExpectedStreamIFCW,
            client_->client()
                ->client_session()
                ->config()
                ->ReceivedInitialStreamFlowControlWindowBytes());
  EXPECT_EQ(kExpectedSessionIFCW,
            client_->client()
                ->client_session()
                ->config()
                ->ReceivedInitialSessionFlowControlWindowBytes());
  EXPECT_EQ(kExpectedStreamIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                     stream->flow_controller()));
  EXPECT_EQ(kExpectedSessionIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                      GetClientSession()->flow_controller()));
}

TEST_P(EndToEndTest, HeadersAndCryptoStreamsNoConnectionFlowControl) {
  // The special headers and crypto streams should be subject to per-stream flow
  // control limits, but should not be subject to connection level flow control
  const uint32_t kStreamIFCW = 32 * 1024;
  const uint32_t kSessionIFCW = 48 * 1024;
  set_client_initial_stream_flow_control_receive_window(kStreamIFCW);
  set_client_initial_session_flow_control_receive_window(kSessionIFCW);
  set_server_initial_stream_flow_control_receive_window(kStreamIFCW);
  set_server_initial_session_flow_control_receive_window(kSessionIFCW);

  ASSERT_TRUE(Initialize());

  // Wait for crypto handshake to finish. This should have contributed to the
  // crypto stream flow control window, but not affected the session flow
  // control window.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  QuicCryptoStream* crypto_stream =
      QuicSessionPeer::GetMutableCryptoStream(GetClientSession());
  // In v47 and later, the crypto handshake (sent in CRYPTO frames) is not
  // subject to flow control.
  const QuicTransportVersion transport_version =
      GetClientConnection()->transport_version();
  if (!QuicVersionUsesCryptoFrames(transport_version)) {
    EXPECT_LT(QuicFlowControllerPeer::SendWindowSize(
                  crypto_stream->flow_controller()),
              kStreamIFCW);
  }
  // When stream type is enabled, control streams will send settings and
  // contribute to flow control windows, so this expectation is no longer valid.
  if (!VersionUsesHttp3(transport_version)) {
    EXPECT_EQ(kSessionIFCW, QuicFlowControllerPeer::SendWindowSize(
                                GetClientSession()->flow_controller()));
  }

  // Send a request with no body, and verify that the connection level window
  // has not been affected.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  // No headers stream in IETF QUIC.
  if (VersionUsesHttp3(transport_version)) {
    return;
  }

  QuicHeadersStream* headers_stream =
      QuicSpdySessionPeer::GetHeadersStream(GetClientSession());
  EXPECT_LT(
      QuicFlowControllerPeer::SendWindowSize(headers_stream->flow_controller()),
      kStreamIFCW);
  EXPECT_EQ(kSessionIFCW, QuicFlowControllerPeer::SendWindowSize(
                              GetClientSession()->flow_controller()));

  // Server should be in a similar state: connection flow control window should
  // not have any bytes marked as received.
  server_thread_->Pause();
  QuicSession* session = GetServerSession();
  QuicFlowController* server_connection_flow_controller =
      session->flow_controller();
  EXPECT_EQ(kSessionIFCW, QuicFlowControllerPeer::ReceiveWindowSize(
                              server_connection_flow_controller));
  server_thread_->Resume();
}

TEST_P(EndToEndTest, FlowControlsSynced) {
  set_smaller_flow_control_receive_window();

  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  QuicSpdySession* const client_session = GetClientSession();
  const QuicTransportVersion version =
      client_session->connection()->transport_version();

  if (VersionUsesHttp3(version)) {
    // Make sure that the client has received the initial SETTINGS frame, which
    // is sent in the first packet on the control stream.
    while (!QuicSpdySessionPeer::GetReceiveControlStream(client_session)) {
      client_->client()->WaitForEvents();
    }
  }

  // Make sure that all data sent by the client has been received by the server
  // (and the ack received by the client).
  while (client_session->HasUnackedStreamData()) {
    client_->client()->WaitForEvents();
  }

  server_thread_->Pause();

  QuicSpdySession* const server_session = GetServerSession();
  ExpectFlowControlsSynced(client_session, server_session);

  // Check control streams.
  if (VersionUsesHttp3(version)) {
    ExpectFlowControlsSynced(
        QuicSpdySessionPeer::GetReceiveControlStream(client_session),
        QuicSpdySessionPeer::GetSendControlStream(server_session));
    ExpectFlowControlsSynced(
        QuicSpdySessionPeer::GetSendControlStream(client_session),
        QuicSpdySessionPeer::GetReceiveControlStream(server_session));
  }

  // Check crypto stream.
  if (!QuicVersionUsesCryptoFrames(version)) {
    ExpectFlowControlsSynced(
        QuicSessionPeer::GetMutableCryptoStream(client_session),
        QuicSessionPeer::GetMutableCryptoStream(server_session));
  }

  // Check headers stream.
  if (!VersionUsesHttp3(version)) {
    SpdyFramer spdy_framer(SpdyFramer::ENABLE_COMPRESSION);
    SpdySettingsIR settings_frame;
    settings_frame.AddSetting(SETTINGS_MAX_HEADER_LIST_SIZE,
                              kDefaultMaxUncompressedHeaderSize);
    SpdySerializedFrame frame(spdy_framer.SerializeFrame(settings_frame));

    QuicFlowController* client_header_stream_flow_controller =
        QuicSpdySessionPeer::GetHeadersStream(client_session)
            ->flow_controller();
    QuicFlowController* server_header_stream_flow_controller =
        QuicSpdySessionPeer::GetHeadersStream(server_session)
            ->flow_controller();
    // Both client and server are sending this SETTINGS frame, and the send
    // window is consumed. But because of timing issue, the server may send or
    // not send the frame, and the client may send/ not send / receive / not
    // receive the frame.
    // TODO(fayang): Rewrite this part because it is hacky.
    QuicByteCount win_difference1 = QuicFlowControllerPeer::ReceiveWindowSize(
                                        server_header_stream_flow_controller) -
                                    QuicFlowControllerPeer::SendWindowSize(
                                        client_header_stream_flow_controller);
    if (win_difference1 != 0) {
      EXPECT_EQ(frame.size(), win_difference1);
    }

    QuicByteCount win_difference2 = QuicFlowControllerPeer::ReceiveWindowSize(
                                        client_header_stream_flow_controller) -
                                    QuicFlowControllerPeer::SendWindowSize(
                                        server_header_stream_flow_controller);
    if (win_difference2 != 0) {
      EXPECT_EQ(frame.size(), win_difference2);
    }

    // Client *may* have received the SETTINGs frame.
    // TODO(fayang): Rewrite this part because it is hacky.
    float ratio1 = static_cast<float>(QuicFlowControllerPeer::ReceiveWindowSize(
                       client_session->flow_controller())) /
                   QuicFlowControllerPeer::ReceiveWindowSize(
                       QuicSpdySessionPeer::GetHeadersStream(client_session)
                           ->flow_controller());
    float ratio2 = static_cast<float>(QuicFlowControllerPeer::ReceiveWindowSize(
                       client_session->flow_controller())) /
                   (QuicFlowControllerPeer::ReceiveWindowSize(
                        QuicSpdySessionPeer::GetHeadersStream(client_session)
                            ->flow_controller()) +
                    frame.size());
    EXPECT_TRUE(ratio1 == kSessionToStreamRatio ||
                ratio2 == kSessionToStreamRatio);
  }

  server_thread_->Resume();
}

TEST_P(EndToEndTestWithTls, RequestWithNoBodyWillNeverSendStreamFrameWithFIN) {
  // A stream created on receipt of a simple request with no body will never get
  // a stream frame with a FIN. Verify that we don't keep track of the stream in
  // the locally closed streams map: it will never be removed if so.
  ASSERT_TRUE(Initialize());

  // Send a simple headers only request, and receive response.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Now verify that the server is not waiting for a final FIN or RST.
  server_thread_->Pause();
  QuicSession* session = GetServerSession();
  EXPECT_EQ(
      0u,
      QuicSessionPeer::GetLocallyClosedStreamsHighestOffset(session).size());
  server_thread_->Resume();
}

// A TestAckListener verifies that its OnAckNotification method has been
// called exactly once on destruction.
class TestAckListener : public QuicAckListenerInterface {
 public:
  explicit TestAckListener(int bytes_to_ack) : bytes_to_ack_(bytes_to_ack) {}

  void OnPacketAcked(int acked_bytes,
                     QuicTime::Delta /*delta_largest_observed*/) override {
    ASSERT_LE(acked_bytes, bytes_to_ack_);
    bytes_to_ack_ -= acked_bytes;
  }

  void OnPacketRetransmitted(int /*retransmitted_bytes*/) override {}

  bool has_been_notified() const { return bytes_to_ack_ == 0; }

 protected:
  // Object is ref counted.
  ~TestAckListener() override { EXPECT_EQ(0, bytes_to_ack_); }

 private:
  int bytes_to_ack_;
};

class TestResponseListener : public QuicSpdyClientBase::ResponseListener {
 public:
  void OnCompleteResponse(QuicStreamId id,
                          const SpdyHeaderBlock& response_headers,
                          const std::string& response_body) override {
    QUIC_DVLOG(1) << "response for stream " << id << " "
                  << response_headers.DebugString() << "\n"
                  << response_body;
  }
};

TEST_P(EndToEndTest, AckNotifierWithPacketLossAndBlockedSocket) {
  // Verify that even in the presence of packet loss and occasionally blocked
  // socket,  an AckNotifierDelegate will get informed that the data it is
  // interested in has been ACKed. This tests end-to-end ACK notification, and
  // demonstrates that retransmissions do not break this functionality.

  SetPacketLossPercentage(5);
  ASSERT_TRUE(Initialize());

  // Wait for the server SHLO before upping the packet loss.
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  SetPacketLossPercentage(30);
  client_writer_->set_fake_blocked_socket_percentage(10);

  // Create a POST request and send the headers only.
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  client_->SendMessage(headers, "", /*fin=*/false);

  // Size of headers on the request stream.  Zero if headers are sent on the
  // header stream.
  size_t header_size = 0;
  if (VersionUsesHttp3(client_->client()
                           ->client_session()
                           ->connection()
                           ->transport_version())) {
    // Determine size of compressed headers.
    NoopDecoderStreamErrorDelegate decoder_stream_error_delegate;
    NoopQpackStreamSenderDelegate encoder_stream_sender_delegate;
    QpackEncoder qpack_encoder(&decoder_stream_error_delegate);
    qpack_encoder.set_qpack_stream_sender_delegate(
        &encoder_stream_sender_delegate);
    std::string encoded_headers =
        qpack_encoder.EncodeHeaderList(/* stream_id = */ 0, headers, nullptr);
    header_size = encoded_headers.size();
  }

  // Test the AckNotifier's ability to track multiple packets by making the
  // request body exceed the size of a single packet.
  std::string request_string = "a request body bigger than one packet" +
                               std::string(kMaxOutgoingPacketSize, '.');

  // The TestAckListener will cause a failure if not notified.
  QuicReferenceCountedPointer<TestAckListener> ack_listener(
      new TestAckListener(header_size + request_string.length()));

  // Send the request, and register the delegate for ACKs.
  client_->SendData(request_string, true, ack_listener);
  client_->WaitForResponse();
  EXPECT_EQ(kFooResponseBody, client_->response_body());
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Send another request to flush out any pending ACKs on the server.
  client_->SendSynchronousRequest("/bar");

  // Make sure the delegate does get the notification it expects.
  while (!ack_listener->has_been_notified()) {
    // Waits for up to 50 ms.
    client_->client()->WaitForEvents();
  }
}

// Send a public reset from the server.
TEST_P(EndToEndTestWithTls, ServerSendPublicReset) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  QuicConnection* client_connection = GetClientConnection();
  QuicConfig* config = client_->client()->session()->config();
  EXPECT_TRUE(config->HasReceivedStatelessResetToken());
  QuicUint128 stateless_reset_token = config->ReceivedStatelessResetToken();

  // Send the public reset.
  QuicConnectionId connection_id = client_connection->connection_id();
  QuicPublicResetPacket header;
  header.connection_id = connection_id;
  QuicFramer framer(server_supported_versions_, QuicTime::Zero(),
                    Perspective::IS_SERVER, kQuicDefaultConnectionIdLength);
  std::unique_ptr<QuicEncryptedPacket> packet;
  if (VersionHasIetfInvariantHeader(client_connection->transport_version())) {
    packet = framer.BuildIetfStatelessResetPacket(connection_id,
                                                  stateless_reset_token);
  } else {
    packet = framer.BuildPublicResetPacket(header);
  }
  // We must pause the server's thread in order to call WritePacket without
  // race conditions.
  server_thread_->Pause();
  server_writer_->WritePacket(
      packet->data(), packet->length(), server_address_.host(),
      client_->client()->network_helper()->GetLatestClientAddress(), nullptr);
  server_thread_->Resume();

  // The request should fail.
  EXPECT_EQ("", client_->SendSynchronousRequest("/foo"));
  EXPECT_TRUE(client_->response_headers()->empty());
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_PUBLIC_RESET));
}

// Send a public reset from the server for a different connection ID.
// It should be ignored.
TEST_P(EndToEndTestWithTls, ServerSendPublicResetWithDifferentConnectionId) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  QuicConnection* client_connection = GetClientConnection();
  QuicConfig* config = client_->client()->session()->config();
  EXPECT_TRUE(config->HasReceivedStatelessResetToken());
  QuicUint128 stateless_reset_token = config->ReceivedStatelessResetToken();
  // Send the public reset.
  QuicConnectionId incorrect_connection_id = TestConnectionId(
      TestConnectionIdToUInt64(client_connection->connection_id()) + 1);
  QuicPublicResetPacket header;
  header.connection_id = incorrect_connection_id;
  QuicFramer framer(server_supported_versions_, QuicTime::Zero(),
                    Perspective::IS_SERVER, kQuicDefaultConnectionIdLength);
  std::unique_ptr<QuicEncryptedPacket> packet;
  testing::NiceMock<MockQuicConnectionDebugVisitor> visitor;
  GetClientConnection()->set_debug_visitor(&visitor);
  if (VersionHasIetfInvariantHeader(client_connection->transport_version())) {
    packet = framer.BuildIetfStatelessResetPacket(incorrect_connection_id,
                                                  stateless_reset_token);
    EXPECT_CALL(visitor, OnIncorrectConnectionId(incorrect_connection_id))
        .Times(0);
  } else {
    packet = framer.BuildPublicResetPacket(header);
    EXPECT_CALL(visitor, OnIncorrectConnectionId(incorrect_connection_id))
        .Times(1);
  }
  // We must pause the server's thread in order to call WritePacket without
  // race conditions.
  server_thread_->Pause();
  server_writer_->WritePacket(
      packet->data(), packet->length(), server_address_.host(),
      client_->client()->network_helper()->GetLatestClientAddress(), nullptr);
  server_thread_->Resume();

  if (VersionHasIetfInvariantHeader(client_connection->transport_version())) {
    // The request should fail. IETF stateless reset does not include connection
    // ID.
    EXPECT_EQ("", client_->SendSynchronousRequest("/foo"));
    EXPECT_TRUE(client_->response_headers()->empty());
    EXPECT_THAT(client_->connection_error(), IsError(QUIC_PUBLIC_RESET));
    return;
  }
  // The connection should be unaffected.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  GetClientConnection()->set_debug_visitor(nullptr);
}

// Send a public reset from the client for a different connection ID.
// It should be ignored.
TEST_P(EndToEndTestWithTls, ClientSendPublicResetWithDifferentConnectionId) {
  ASSERT_TRUE(Initialize());

  // Send the public reset.
  QuicConnectionId incorrect_connection_id = TestConnectionId(
      TestConnectionIdToUInt64(GetClientConnection()->connection_id()) + 1);
  QuicPublicResetPacket header;
  header.connection_id = incorrect_connection_id;
  QuicFramer framer(server_supported_versions_, QuicTime::Zero(),
                    Perspective::IS_CLIENT, kQuicDefaultConnectionIdLength);
  std::unique_ptr<QuicEncryptedPacket> packet(
      framer.BuildPublicResetPacket(header));
  client_writer_->WritePacket(
      packet->data(), packet->length(),
      client_->client()->network_helper()->GetLatestClientAddress().host(),
      server_address_, nullptr);

  // The connection should be unaffected.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

// Send a version negotiation packet from the server for a different
// connection ID.  It should be ignored.
TEST_P(EndToEndTestWithTls,
       ServerSendVersionNegotiationWithDifferentConnectionId) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // Send the version negotiation packet.
  QuicConnection* client_connection = GetClientConnection();
  QuicConnectionId incorrect_connection_id = TestConnectionId(
      TestConnectionIdToUInt64(client_connection->connection_id()) + 1);
  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildVersionNegotiationPacket(
          incorrect_connection_id, EmptyQuicConnectionId(),
          VersionHasIetfInvariantHeader(client_connection->transport_version()),
          client_connection->version().HasLengthPrefixedConnectionIds(),
          server_supported_versions_));
  testing::NiceMock<MockQuicConnectionDebugVisitor> visitor;
  client_connection->set_debug_visitor(&visitor);
  EXPECT_CALL(visitor, OnIncorrectConnectionId(incorrect_connection_id))
      .Times(1);
  // We must pause the server's thread in order to call WritePacket without
  // race conditions.
  server_thread_->Pause();
  server_writer_->WritePacket(
      packet->data(), packet->length(), server_address_.host(),
      client_->client()->network_helper()->GetLatestClientAddress(), nullptr);
  server_thread_->Resume();

  // The connection should be unaffected.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  client_connection->set_debug_visitor(nullptr);
}

// A bad header shouldn't tear down the connection, because the receiver can't
// tell the connection ID.
TEST_P(EndToEndTestWithTls, BadPacketHeaderTruncated) {
  ASSERT_TRUE(Initialize());

  // Start the connection.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Packet with invalid public flags.
  char packet[] = {// public flags (8 byte connection_id)
                   0x3C,
                   // truncated connection ID
                   0x11};
  client_writer_->WritePacket(
      &packet[0], sizeof(packet),
      client_->client()->network_helper()->GetLatestClientAddress().host(),
      server_address_, nullptr);
  EXPECT_TRUE(server_thread_->WaitUntil(
      [&] {
        return QuicDispatcherPeer::GetAndClearLastError(
                   QuicServerPeer::GetDispatcher(server_thread_->server())) ==
               QUIC_INVALID_PACKET_HEADER;
      },
      QuicTime::Delta::FromSeconds(5)));

  // The connection should not be terminated.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

// A bad header shouldn't tear down the connection, because the receiver can't
// tell the connection ID.
TEST_P(EndToEndTestWithTls, BadPacketHeaderFlags) {
  ASSERT_TRUE(Initialize());

  // Start the connection.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Packet with invalid public flags.
  char packet[] = {
      // invalid public flags
      0xFF,
      // connection_id
      0x10,
      0x32,
      0x54,
      0x76,
      0x98,
      0xBA,
      0xDC,
      0xFE,
      // packet sequence number
      0xBC,
      0x9A,
      0x78,
      0x56,
      0x34,
      0x12,
      // private flags
      0x00,
  };
  client_writer_->WritePacket(
      &packet[0], sizeof(packet),
      client_->client()->network_helper()->GetLatestClientAddress().host(),
      server_address_, nullptr);

  EXPECT_TRUE(server_thread_->WaitUntil(
      [&] {
        return QuicDispatcherPeer::GetAndClearLastError(
                   QuicServerPeer::GetDispatcher(server_thread_->server())) ==
               QUIC_INVALID_PACKET_HEADER;
      },
      QuicTime::Delta::FromSeconds(5)));

  // The connection should not be terminated.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

// Send a packet from the client with bad encrypted data.  The server should not
// tear down the connection.
TEST_P(EndToEndTestWithTls, BadEncryptedData) {
  ASSERT_TRUE(Initialize());

  // Start the connection.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  std::unique_ptr<QuicEncryptedPacket> packet(ConstructEncryptedPacket(
      GetClientConnection()->connection_id(), EmptyQuicConnectionId(), false,
      false, 1, "At least 20 characters.", CONNECTION_ID_PRESENT,
      CONNECTION_ID_ABSENT, PACKET_4BYTE_PACKET_NUMBER));
  // Damage the encrypted data.
  std::string damaged_packet(packet->data(), packet->length());
  damaged_packet[30] ^= 0x01;
  QUIC_DLOG(INFO) << "Sending bad packet.";
  client_writer_->WritePacket(
      damaged_packet.data(), damaged_packet.length(),
      client_->client()->network_helper()->GetLatestClientAddress().host(),
      server_address_, nullptr);
  // Give the server time to process the packet.
  QuicSleep(QuicTime::Delta::FromSeconds(1));
  // This error is sent to the connection's OnError (which ignores it), so the
  // dispatcher doesn't see it.
  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  EXPECT_THAT(QuicDispatcherPeer::GetAndClearLastError(dispatcher),
              IsQuicNoError());
  server_thread_->Resume();

  // The connection should not be terminated.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

TEST_P(EndToEndTestWithTls, CanceledStreamDoesNotBecomeZombie) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  // Lose the request.
  SetPacketLossPercentage(100);
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  client_->SendMessage(headers, "test_body", /*fin=*/false);
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();

  // Cancel the stream.
  stream->Reset(QUIC_STREAM_CANCELLED);
  QuicSession* session = GetClientSession();
  // Verify canceled stream does not become zombie.
  EXPECT_TRUE(QuicSessionPeer::zombie_streams(session).empty());
  EXPECT_EQ(1u, QuicSessionPeer::closed_streams(session).size());
}

// A test stream that gives |response_body_| as an error response body.
class ServerStreamWithErrorResponseBody : public QuicSimpleServerStream {
 public:
  ServerStreamWithErrorResponseBody(
      QuicStreamId id,
      QuicSpdySession* session,
      QuicSimpleServerBackend* quic_simple_server_backend,
      std::string response_body)
      : QuicSimpleServerStream(id,
                               session,
                               BIDIRECTIONAL,
                               quic_simple_server_backend),
        response_body_(std::move(response_body)) {}

  ~ServerStreamWithErrorResponseBody() override = default;

 protected:
  void SendErrorResponse() override {
    QUIC_DLOG(INFO) << "Sending error response for stream " << id();
    SpdyHeaderBlock headers;
    headers[":status"] = "500";
    headers["content-length"] =
        quiche::QuicheTextUtils::Uint64ToString(response_body_.size());
    // This method must call CloseReadSide to cause the test case, StopReading
    // is not sufficient.
    QuicStreamPeer::CloseReadSide(this);
    SendHeadersAndBody(std::move(headers), response_body_);
  }

  std::string response_body_;
};

class StreamWithErrorFactory : public QuicTestServer::StreamFactory {
 public:
  explicit StreamWithErrorFactory(std::string response_body)
      : response_body_(std::move(response_body)) {}

  ~StreamWithErrorFactory() override = default;

  QuicSimpleServerStream* CreateStream(
      QuicStreamId id,
      QuicSpdySession* session,
      QuicSimpleServerBackend* quic_simple_server_backend) override {
    return new ServerStreamWithErrorResponseBody(
        id, session, quic_simple_server_backend, response_body_);
  }

 private:
  std::string response_body_;
};

// A test server stream that drops all received body.
class ServerStreamThatDropsBody : public QuicSimpleServerStream {
 public:
  ServerStreamThatDropsBody(QuicStreamId id,
                            QuicSpdySession* session,
                            QuicSimpleServerBackend* quic_simple_server_backend)
      : QuicSimpleServerStream(id,
                               session,
                               BIDIRECTIONAL,
                               quic_simple_server_backend) {}

  ~ServerStreamThatDropsBody() override = default;

 protected:
  void OnBodyAvailable() override {
    while (HasBytesToRead()) {
      struct iovec iov;
      if (GetReadableRegions(&iov, 1) == 0) {
        // No more data to read.
        break;
      }
      QUIC_DVLOG(1) << "Processed " << iov.iov_len << " bytes for stream "
                    << id();
      MarkConsumed(iov.iov_len);
    }

    if (!sequencer()->IsClosed()) {
      sequencer()->SetUnblocked();
      return;
    }

    // If the sequencer is closed, then all the body, including the fin, has
    // been consumed.
    OnFinRead();

    if (write_side_closed() || fin_buffered()) {
      return;
    }

    SendResponse();
  }
};

class ServerStreamThatDropsBodyFactory : public QuicTestServer::StreamFactory {
 public:
  ServerStreamThatDropsBodyFactory() = default;

  ~ServerStreamThatDropsBodyFactory() override = default;

  QuicSimpleServerStream* CreateStream(
      QuicStreamId id,
      QuicSpdySession* session,
      QuicSimpleServerBackend* quic_simple_server_backend) override {
    return new ServerStreamThatDropsBody(id, session,
                                         quic_simple_server_backend);
  }
};

// A test server stream that sends response with body size greater than 4GB.
class ServerStreamThatSendsHugeResponse : public QuicSimpleServerStream {
 public:
  ServerStreamThatSendsHugeResponse(
      QuicStreamId id,
      QuicSpdySession* session,
      QuicSimpleServerBackend* quic_simple_server_backend,
      int64_t body_bytes)
      : QuicSimpleServerStream(id,
                               session,
                               BIDIRECTIONAL,
                               quic_simple_server_backend),
        body_bytes_(body_bytes) {}

  ~ServerStreamThatSendsHugeResponse() override = default;

 protected:
  void SendResponse() override {
    QuicBackendResponse response;
    std::string body(body_bytes_, 'a');
    response.set_body(body);
    SendHeadersAndBodyAndTrailers(response.headers().Clone(), response.body(),
                                  response.trailers().Clone());
  }

 private:
  // Use a explicit int64_t rather than size_t to simulate a 64-bit server
  // talking to a 32-bit client.
  int64_t body_bytes_;
};

class ServerStreamThatSendsHugeResponseFactory
    : public QuicTestServer::StreamFactory {
 public:
  explicit ServerStreamThatSendsHugeResponseFactory(int64_t body_bytes)
      : body_bytes_(body_bytes) {}

  ~ServerStreamThatSendsHugeResponseFactory() override = default;

  QuicSimpleServerStream* CreateStream(
      QuicStreamId id,
      QuicSpdySession* session,
      QuicSimpleServerBackend* quic_simple_server_backend) override {
    return new ServerStreamThatSendsHugeResponse(
        id, session, quic_simple_server_backend, body_bytes_);
  }

  int64_t body_bytes_;
};

TEST_P(EndToEndTest, EarlyResponseFinRecording) {
  set_smaller_flow_control_receive_window();

  // Verify that an incoming FIN is recorded in a stream object even if the read
  // side has been closed.  This prevents an entry from being made in
  // locally_close_streams_highest_offset_ (which will never be deleted).
  // To set up the test condition, the server must do the following in order:
  // start sending the response and call CloseReadSide
  // receive the FIN of the request
  // send the FIN of the response

  // The response body must be larger than the flow control window so the server
  // must receive a window update from the client before it can finish sending
  // it.
  uint32_t response_body_size =
      2 * client_config_.GetInitialStreamFlowControlWindowToSend();
  std::string response_body(response_body_size, 'a');

  StreamWithErrorFactory stream_factory(response_body);
  SetSpdyStreamFactory(&stream_factory);

  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // A POST that gets an early error response, after the headers are received
  // and before the body is received, due to invalid content-length.
  // Set an invalid content-length, so the request will receive an early 500
  // response.
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/garbage";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["content-length"] = "-1";

  // The body must be large enough that the FIN will be in a different packet
  // than the end of the headers, but short enough to not require a flow control
  // update.  This allows headers processing to trigger the error response
  // before the request FIN is processed but receive the request FIN before the
  // response is sent completely.
  const uint32_t kRequestBodySize = kMaxOutgoingPacketSize + 10;
  std::string request_body(kRequestBodySize, 'a');

  // Send the request.
  client_->SendMessage(headers, request_body);
  client_->WaitForResponse();
  EXPECT_EQ("500", client_->response_headers()->find(":status")->second);

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();

  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  QuicDispatcher::SessionMap const& map =
      QuicDispatcherPeer::session_map(dispatcher);
  auto it = map.begin();
  EXPECT_TRUE(it != map.end());
  QuicSession* server_session = it->second.get();

  // The stream is not waiting for the arrival of the peer's final offset.
  EXPECT_EQ(
      0u, QuicSessionPeer::GetLocallyClosedStreamsHighestOffset(server_session)
              .size());

  server_thread_->Resume();
}

TEST_P(EndToEndTestWithTls, Trailers) {
  // Test sending and receiving HTTP/2 Trailers (trailing HEADERS frames).
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // Set reordering to ensure that Trailers arriving before body is ok.
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(30);

  // Add a response with headers, body, and trailers.
  const std::string kBody = "body content";

  SpdyHeaderBlock headers;
  headers[":status"] = "200";
  headers["content-length"] =
      quiche::QuicheTextUtils::Uint64ToString(kBody.size());

  SpdyHeaderBlock trailers;
  trailers["some-trailing-header"] = "trailing-header-value";

  memory_cache_backend_.AddResponse(server_hostname_, "/trailer_url",
                                    std::move(headers), kBody,
                                    trailers.Clone());

  EXPECT_EQ(kBody, client_->SendSynchronousRequest("/trailer_url"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
  EXPECT_EQ(trailers, client_->response_trailers());
}

// TODO(b/151749109): Test server push for IETF QUIC.
class EndToEndTestServerPush : public EndToEndTest {
 protected:
  const size_t kNumMaxStreams = 10;

  EndToEndTestServerPush() : EndToEndTest() {
    client_config_.SetMaxBidirectionalStreamsToSend(kNumMaxStreams);
    server_config_.SetMaxBidirectionalStreamsToSend(kNumMaxStreams);
    client_config_.SetMaxUnidirectionalStreamsToSend(kNumMaxStreams);
    server_config_.SetMaxUnidirectionalStreamsToSend(kNumMaxStreams);
  }

  // Add a request with its response and |num_resources| push resources into
  // cache.
  // If |resource_size| == 0, response body of push resources use default string
  // concatenating with resource url. Otherwise, generate a string of
  // |resource_size| as body.
  void AddRequestAndResponseWithServerPush(std::string host,
                                           std::string path,
                                           std::string response_body,
                                           std::string* push_urls,
                                           const size_t num_resources,
                                           const size_t resource_size) {
    bool use_large_response = resource_size != 0;
    std::string large_resource;
    if (use_large_response) {
      // Generate a response common body larger than flow control window for
      // push response.
      large_resource = std::string(resource_size, 'a');
    }
    std::list<QuicBackendResponse::ServerPushInfo> push_resources;
    for (size_t i = 0; i < num_resources; ++i) {
      std::string url = push_urls[i];
      QuicUrl resource_url(url);
      std::string body =
          use_large_response
              ? large_resource
              : quiche::QuicheStrCat("This is server push response body for ",
                                     url);
      SpdyHeaderBlock response_headers;
      response_headers[":status"] = "200";
      response_headers["content-length"] =
          quiche::QuicheTextUtils::Uint64ToString(body.size());
      push_resources.push_back(QuicBackendResponse::ServerPushInfo(
          resource_url, std::move(response_headers), kV3LowestPriority, body));
    }

    memory_cache_backend_.AddSimpleResponseWithServerPushResources(
        host, path, 200, response_body, push_resources);
  }
};

// Run all server push end to end tests with all supported versions.
INSTANTIATE_TEST_SUITE_P(EndToEndTestsServerPush,
                         EndToEndTestServerPush,
                         ::testing::ValuesIn(GetTestParams(false)),
                         ::testing::PrintToStringParamName());

TEST_P(EndToEndTestServerPush, ServerPush) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // Set reordering to ensure that body arriving before PUSH_PROMISE is ok.
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(30);

  // Add a response with headers, body, and push resources.
  const std::string kBody = "body content";
  size_t kNumResources = 4;
  std::string push_urls[] = {"https://example.com/font.woff",
                             "https://example.com/script.js",
                             "https://fonts.example.com/font.woff",
                             "https://example.com/logo-hires.jpg"};
  AddRequestAndResponseWithServerPush("example.com", "/push_example", kBody,
                                      push_urls, kNumResources, 0);

  client_->client()->set_response_listener(
      std::unique_ptr<QuicSpdyClientBase::ResponseListener>(
          new TestResponseListener));

  QUIC_DVLOG(1) << "send request for /push_example";
  EXPECT_EQ(kBody, client_->SendSynchronousRequest(
                       "https://example.com/push_example"));
  QuicStreamSequencer* sequencer;
  if (!VersionUsesHttp3(client_->client()
                            ->client_session()
                            ->connection()
                            ->transport_version())) {
    QuicHeadersStream* headers_stream =
        QuicSpdySessionPeer::GetHeadersStream(GetClientSession());
    sequencer = QuicStreamPeer::sequencer(headers_stream);
    // Headers stream's sequencer buffer shouldn't be released because server
    // push hasn't finished yet.
    EXPECT_TRUE(
        QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
  }

  for (const std::string& url : push_urls) {
    QUIC_DVLOG(1) << "send request for pushed stream on url " << url;
    std::string expected_body =
        quiche::QuicheStrCat("This is server push response body for ", url);
    std::string response_body = client_->SendSynchronousRequest(url);
    QUIC_DVLOG(1) << "response body " << response_body;
    EXPECT_EQ(expected_body, response_body);
  }
  if (!VersionUsesHttp3(client_->client()
                            ->client_session()
                            ->connection()
                            ->transport_version())) {
    EXPECT_FALSE(
        QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
  }
}

TEST_P(EndToEndTestServerPush, ServerPushUnderLimit) {
  // Tests that sending a request which has 4 push resources will trigger server
  // to push those 4 resources and client can handle pushed resources and match
  // them with requests later.
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // Set reordering to ensure that body arriving before PUSH_PROMISE is ok.
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(30);

  // Add a response with headers, body, and push resources.
  const std::string kBody = "body content";
  size_t const kNumResources = 4;
  std::string push_urls[] = {
      "https://example.com/font.woff",
      "https://example.com/script.js",
      "https://fonts.example.com/font.woff",
      "https://example.com/logo-hires.jpg",
  };
  AddRequestAndResponseWithServerPush("example.com", "/push_example", kBody,
                                      push_urls, kNumResources, 0);
  client_->client()->set_response_listener(
      std::unique_ptr<QuicSpdyClientBase::ResponseListener>(
          new TestResponseListener));

  // Send the first request: this will trigger the server to send all the push
  // resources associated with this request, and these will be cached by the
  // client.
  EXPECT_EQ(kBody, client_->SendSynchronousRequest(
                       "https://example.com/push_example"));

  for (const std::string& url : push_urls) {
    // Sending subsequent requesets will not actually send anything on the wire,
    // as the responses are already in the client's cache.
    QUIC_DVLOG(1) << "send request for pushed stream on url " << url;
    std::string expected_body =
        quiche::QuicheStrCat("This is server push response body for ", url);
    std::string response_body = client_->SendSynchronousRequest(url);
    QUIC_DVLOG(1) << "response body " << response_body;
    EXPECT_EQ(expected_body, response_body);
  }
  // Expect only original request has been sent and push responses have been
  // received as normal response.
  EXPECT_EQ(1u, client_->num_requests());
  EXPECT_EQ(1u + kNumResources, client_->num_responses());
}

TEST_P(EndToEndTestServerPush, ServerPushOverLimitNonBlocking) {
  // Tests that when streams are not blocked by flow control or congestion
  // control, pushing even more resources than max number of open outgoing
  // streams should still work because all response streams get closed
  // immediately after pushing resources.
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  if (VersionUsesHttp3(client_->client()
                           ->client_session()
                           ->connection()
                           ->transport_version())) {
    // TODO(b/142504641): Re-enable this test when we support push streams
    // arriving before the corresponding promises.
    return;
  }

  // Set reordering to ensure that body arriving before PUSH_PROMISE is ok.
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(30);

  // Add a response with headers, body, and push resources.
  const std::string kBody = "body content";

  // One more resource than max number of outgoing stream of this session.
  const size_t kNumResources = 1 + kNumMaxStreams;  // 11.
  std::string push_urls[11];
  for (size_t i = 0; i < kNumResources; ++i) {
    push_urls[i] =
        quiche::QuicheStrCat("https://example.com/push_resources", i);
  }
  AddRequestAndResponseWithServerPush("example.com", "/push_example", kBody,
                                      push_urls, kNumResources, 0);
  client_->client()->set_response_listener(
      std::unique_ptr<QuicSpdyClientBase::ResponseListener>(
          new TestResponseListener));

  // Send the first request: this will trigger the server to send all the push
  // resources associated with this request, and these will be cached by the
  // client.
  EXPECT_EQ(kBody, client_->SendSynchronousRequest(
                       "https://example.com/push_example"));

  for (const std::string& url : push_urls) {
    // Sending subsequent requesets will not actually send anything on the wire,
    // as the responses are already in the client's cache.
    EXPECT_EQ(
        quiche::QuicheStrCat("This is server push response body for ", url),
        client_->SendSynchronousRequest(url));
  }

  // Only 1 request should have been sent.
  EXPECT_EQ(1u, client_->num_requests());
  // The responses to the original request and all the promised resources
  // should have been received.
  EXPECT_EQ(12u, client_->num_responses());
}

TEST_P(EndToEndTestServerPush, ServerPushOverLimitWithBlocking) {
  // Tests that when server tries to send more large resources(large enough to
  // be blocked by flow control window or congestion control window) than max
  // open outgoing streams , server can open upto max number of outgoing
  // streams for them, and the rest will be queued up.

  // Reset flow control windows.
  size_t kFlowControlWnd = 20 * 1024;  // 20KB.
  // Response body is larger than 1 flow controlblock window.
  size_t kBodySize = kFlowControlWnd * 2;
  set_client_initial_stream_flow_control_receive_window(kFlowControlWnd);
  // Make sure conntection level flow control window is large enough not to
  // block data being sent out though they will be blocked by stream level one.
  set_client_initial_session_flow_control_receive_window(
      kBodySize * kNumMaxStreams + 1024);

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  // Set reordering to ensure that body arriving before PUSH_PROMISE is ok.
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(30);

  // Add a response with headers, body, and push resources.
  const std::string kBody = "body content";

  const size_t kNumResources = kNumMaxStreams + 1;
  std::string push_urls[11];
  for (size_t i = 0; i < kNumResources; ++i) {
    push_urls[i] = quiche::QuicheStrCat("http://example.com/push_resources", i);
  }
  AddRequestAndResponseWithServerPush("example.com", "/push_example", kBody,
                                      push_urls, kNumResources, kBodySize);

  client_->client()->set_response_listener(
      std::unique_ptr<QuicSpdyClientBase::ResponseListener>(
          new TestResponseListener));

  client_->SendRequest("https://example.com/push_example");

  // Pause after the first response arrives.
  while (!client_->response_complete()) {
    // Because of priority, the first response arrived should be to original
    // request.
    client_->WaitForResponse();
  }

  // Check server session to see if it has max number of outgoing streams opened
  // though more resources need to be pushed.
  server_thread_->Pause();
  EXPECT_EQ(kNumMaxStreams, GetServerSession()->GetNumOpenOutgoingStreams());
  server_thread_->Resume();

  EXPECT_EQ(1u, client_->num_requests());
  EXPECT_EQ(1u, client_->num_responses());
  EXPECT_EQ(kBody, client_->response_body());

  // "Send" request for a promised resources will not really send out it because
  // its response is being pushed(but blocked). And the following ack and
  // flow control behavior of SendSynchronousRequests()
  // will unblock the stream to finish receiving response.
  client_->SendSynchronousRequest(push_urls[0]);
  EXPECT_EQ(1u, client_->num_requests());
  EXPECT_EQ(2u, client_->num_responses());

  // Do same thing for the rest 10 resources.
  for (size_t i = 1; i < kNumResources; ++i) {
    client_->SendSynchronousRequest(push_urls[i]);
  }

  // Because of server push, client gets all pushed resources without actually
  // sending requests for them.
  EXPECT_EQ(1u, client_->num_requests());
  // Including response to original request, 12 responses in total were
  // received.
  EXPECT_EQ(12u, client_->num_responses());
}

// TODO(fayang): this test seems to cause net_unittests timeouts :|
TEST_P(EndToEndTest, DISABLED_TestHugePostWithPacketLoss) {
  // This test tests a huge post with introduced packet loss from client to
  // server and body size greater than 4GB, making sure QUIC code does not break
  // for 32-bit builds.
  ServerStreamThatDropsBodyFactory stream_factory;
  SetSpdyStreamFactory(&stream_factory);
  ASSERT_TRUE(Initialize());
  // Set client's epoll server's time out to 0 to make this test be finished
  // within a short time.
  client_->epoll_server()->set_timeout_in_us(0);

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  SetPacketLossPercentage(1);
  // To avoid storing the whole request body in memory, use a loop to repeatedly
  // send body size of kSizeBytes until the whole request body size is reached.
  const int kSizeBytes = 128 * 1024;
  // Request body size is 4G plus one more kSizeBytes.
  int64_t request_body_size_bytes = pow(2, 32) + kSizeBytes;
  ASSERT_LT(INT64_C(4294967296), request_body_size_bytes);
  std::string body(kSizeBytes, 'a');

  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["content-length"] =
      quiche::QuicheTextUtils::Uint64ToString(request_body_size_bytes);

  client_->SendMessage(headers, "", /*fin=*/false);

  for (int i = 0; i < request_body_size_bytes / kSizeBytes; ++i) {
    bool fin = (i == request_body_size_bytes - 1);
    client_->SendData(std::string(body.data(), kSizeBytes), fin);
    client_->client()->WaitForEvents();
  }
  VerifyCleanConnection(true);
}

// TODO(fayang): this test seems to cause net_unittests timeouts :|
TEST_P(EndToEndTest, DISABLED_TestHugeResponseWithPacketLoss) {
  // This test tests a huge response with introduced loss from server to client
  // and body size greater than 4GB, making sure QUIC code does not break for
  // 32-bit builds.
  const int kSizeBytes = 128 * 1024;
  int64_t response_body_size_bytes = pow(2, 32) + kSizeBytes;
  ASSERT_LT(4294967296, response_body_size_bytes);
  ServerStreamThatSendsHugeResponseFactory stream_factory(
      response_body_size_bytes);
  SetSpdyStreamFactory(&stream_factory);

  StartServer();

  // Use a quic client that drops received body.
  QuicTestClient* client =
      new QuicTestClient(server_address_, server_hostname_, client_config_,
                         client_supported_versions_);
  client->client()->set_drop_response_body(true);
  client->UseWriter(client_writer_);
  client->Connect();
  client_.reset(client);
  static QuicEpollEvent event(EPOLLOUT);
  client_writer_->Initialize(
      QuicConnectionPeer::GetHelper(GetClientConnection()),
      QuicConnectionPeer::GetAlarmFactory(GetClientConnection()),
      std::make_unique<ClientDelegate>(client_->client()));
  initialized_ = true;
  ASSERT_TRUE(client_->client()->connected());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  SetPacketLossPercentage(1);
  client_->SendRequest("/huge_response");
  client_->WaitForResponse();
  VerifyCleanConnection(true);
}

// Regression test for b/111515567
TEST_P(EndToEndTest, AgreeOnStopWaiting) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  QuicConnection* client_connection = GetClientConnection();
  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  // Verify client and server connections agree on the value of
  // no_stop_waiting_frames.
  EXPECT_EQ(QuicConnectionPeer::GetNoStopWaitingFrames(client_connection),
            QuicConnectionPeer::GetNoStopWaitingFrames(server_connection));
  server_thread_->Resume();
}

// Regression test for b/111515567
TEST_P(EndToEndTest, AgreeOnStopWaitingWithNoStopWaitingOption) {
  QuicTagVector options;
  options.push_back(kNSTP);
  client_config_.SetConnectionOptionsToSend(options);
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  QuicConnection* client_connection = GetClientConnection();
  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  // Verify client and server connections agree on the value of
  // no_stop_waiting_frames.
  EXPECT_EQ(QuicConnectionPeer::GetNoStopWaitingFrames(client_connection),
            QuicConnectionPeer::GetNoStopWaitingFrames(server_connection));
  server_thread_->Resume();
}

TEST_P(EndToEndTest, ReleaseHeadersStreamBufferWhenIdle) {
  // Tests that when client side has no active request and no waiting
  // PUSH_PROMISE, its headers stream's sequencer buffer should be released.
  ASSERT_TRUE(Initialize());
  client_->SendSynchronousRequest("/foo");
  if (VersionUsesHttp3(client_->client()
                           ->client_session()
                           ->connection()
                           ->transport_version())) {
    return;
  }
  QuicHeadersStream* headers_stream =
      QuicSpdySessionPeer::GetHeadersStream(GetClientSession());
  QuicStreamSequencer* sequencer = QuicStreamPeer::sequencer(headers_stream);
  EXPECT_FALSE(QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
}

TEST_P(EndToEndTest, WayTooLongRequestHeaders) {
  ASSERT_TRUE(Initialize());
  SpdyHeaderBlock headers;
  headers[":method"] = "GET";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["key"] = std::string(64 * 1024, 'a');

  client_->SendMessage(headers, "");
  client_->WaitForResponse();

  QuicErrorCode expected_error =
      GetQuicReloadableFlag(spdy_enable_granular_decompress_errors)
          ? QUIC_HPACK_INDEX_VARINT_ERROR
          : QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE;
  EXPECT_THAT(client_->connection_error(), IsError(expected_error));
}

class WindowUpdateObserver : public QuicConnectionDebugVisitor {
 public:
  WindowUpdateObserver() : num_window_update_frames_(0), num_ping_frames_(0) {}

  size_t num_window_update_frames() const { return num_window_update_frames_; }

  size_t num_ping_frames() const { return num_ping_frames_; }

  void OnWindowUpdateFrame(const QuicWindowUpdateFrame& /*frame*/,
                           const QuicTime& /*receive_time*/) override {
    ++num_window_update_frames_;
  }

  void OnPingFrame(const QuicPingFrame& /*frame*/) override {
    ++num_ping_frames_;
  }

 private:
  size_t num_window_update_frames_;
  size_t num_ping_frames_;
};

TEST_P(EndToEndTest, WindowUpdateInAck) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  WindowUpdateObserver observer;
  QuicConnection* client_connection = GetClientConnection();
  client_connection->set_debug_visitor(&observer);
  // 100KB body.
  std::string body(100 * 1024, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  client_->Disconnect();
  EXPECT_LT(0u, observer.num_window_update_frames());
  EXPECT_EQ(0u, observer.num_ping_frames());
}

TEST_P(EndToEndTestWithTls, SendStatelessResetTokenInShlo) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  QuicConfig* config = client_->client()->session()->config();
  EXPECT_TRUE(config->HasReceivedStatelessResetToken());
  EXPECT_EQ(QuicUtils::GenerateStatelessResetToken(
                client_->client()->session()->connection()->connection_id()),
            config->ReceivedStatelessResetToken());
  client_->Disconnect();
}

// Regression test for b/116200989.
TEST_P(EndToEndTest,
       SendStatelessResetIfServerConnectionClosedLocallyDuringHandshake) {
  connect_to_server_on_initialize_ = false;
  ASSERT_TRUE(Initialize());

  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  ASSERT_EQ(0u, dispatcher->session_map().size());
  // Note: this writer will only used by the server connection, not the time
  // wait list.
  QuicDispatcherPeer::UseWriter(
      dispatcher,
      // This cause the first server-sent packet, a.k.a REJ, to fail.
      new BadPacketWriter(/*packet_causing_write_error=*/0, EPERM));
  server_thread_->Resume();

  client_.reset(CreateQuicClient(client_writer_));
  EXPECT_EQ("", client_->SendSynchronousRequest("/foo"));
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_HANDSHAKE_FAILED));
}

// Regression test for b/116200989.
TEST_P(EndToEndTest,
       SendStatelessResetIfServerConnectionClosedLocallyAfterHandshake) {
  // Prevent the connection from expiring in the time wait list.
  SetQuicFlag(FLAGS_quic_time_wait_list_seconds, 10000);
  connect_to_server_on_initialize_ = false;
  ASSERT_TRUE(Initialize());

  // big_response_body is 64K, which is about 48 full-sized packets.
  const size_t kBigResponseBodySize = 65536;
  QuicData big_response_body(new char[kBigResponseBodySize](),
                             kBigResponseBodySize, /*owns_buffer=*/true);
  AddToCache("/big_response", 200, big_response_body.AsStringPiece());

  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  ASSERT_EQ(0u, dispatcher->session_map().size());
  QuicDispatcherPeer::UseWriter(
      dispatcher,
      // This will cause an server write error with EPERM, while sending the
      // response for /big_response.
      new BadPacketWriter(/*packet_causing_write_error=*/20, EPERM));
  server_thread_->Resume();

  client_.reset(CreateQuicClient(client_writer_));

  // First, a /foo request with small response should succeed.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Second, a /big_response request with big response should fail.
  EXPECT_LT(client_->SendSynchronousRequest("/big_response").length(),
            kBigResponseBodySize);
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_PUBLIC_RESET));
}

// Regression test of b/70782529.
TEST_P(EndToEndTest, DoNotCrashOnPacketWriteError) {
  ASSERT_TRUE(Initialize());
  BadPacketWriter* bad_writer =
      new BadPacketWriter(/*packet_causing_write_error=*/5,
                          /*error_code=*/90);
  std::unique_ptr<QuicTestClient> client(CreateQuicClient(bad_writer));

  // 1 MB body.
  std::string body(1024 * 1024, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  client->SendCustomSynchronousRequest(headers, body);
}

// Regression test for b/71711996. This test sends a connectivity probing packet
// as its last sent packet, and makes sure the server's ACK of that packet does
// not cause the client to fail.
TEST_P(EndToEndTest, LastPacketSentIsConnectivityProbing) {
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);

  // Wait for the client's ACK (of the response) to be received by the server.
  client_->WaitForDelayedAcks();

  // We are sending a connectivity probing packet from an unchanged client
  // address, so the server will not respond to us with a connectivity probing
  // packet, however the server should send an ack-only packet to us.
  client_->SendConnectivityProbing();

  // Wait for the server's last ACK to be received by the client.
  client_->WaitForDelayedAcks();
}

TEST_P(EndToEndTest, PreSharedKey) {
  client_config_.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(5));
  client_config_.set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(5));
  pre_shared_key_client_ = "foobar";
  pre_shared_key_server_ = "foobar";
  ASSERT_TRUE(Initialize());

  ASSERT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  EXPECT_EQ("200", client_->response_headers()->find(":status")->second);
}

// TODO: reenable once we have a way to make this run faster.
TEST_P(EndToEndTest, QUIC_TEST_DISABLED_IN_CHROME(PreSharedKeyMismatch)) {
  client_config_.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  client_config_.set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  pre_shared_key_client_ = "foo";
  pre_shared_key_server_ = "bar";
  // One of two things happens when Initialize() returns:
  // 1. Crypto handshake has completed, and it is unsuccessful. Initialize()
  //    returns false.
  // 2. Crypto handshake has not completed, Initialize() returns true. The call
  //    to WaitForCryptoHandshakeConfirmed() will wait for the handshake and
  //    return whether it is successful.
  ASSERT_FALSE(Initialize() &&
               client_->client()->WaitForCryptoHandshakeConfirmed());
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_HANDSHAKE_TIMEOUT));
}

// TODO: reenable once we have a way to make this run faster.
TEST_P(EndToEndTest, QUIC_TEST_DISABLED_IN_CHROME(PreSharedKeyNoClient)) {
  client_config_.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  client_config_.set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  pre_shared_key_server_ = "foobar";
  ASSERT_FALSE(Initialize() &&
               client_->client()->WaitForCryptoHandshakeConfirmed());
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_HANDSHAKE_TIMEOUT));
}

// TODO: reenable once we have a way to make this run faster.
TEST_P(EndToEndTest, QUIC_TEST_DISABLED_IN_CHROME(PreSharedKeyNoServer)) {
  client_config_.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  client_config_.set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  pre_shared_key_client_ = "foobar";
  ASSERT_FALSE(Initialize() &&
               client_->client()->WaitForCryptoHandshakeConfirmed());
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_HANDSHAKE_TIMEOUT));
}

TEST_P(EndToEndTest, RequestAndStreamRstInOnePacket) {
  // Regression test for b/80234898.
  ASSERT_TRUE(Initialize());

  // INCOMPLETE_RESPONSE will cause the server to not to send the trailer
  // (and the FIN) after the response body.
  std::string response_body(1305, 'a');
  SpdyHeaderBlock response_headers;
  response_headers[":status"] = quiche::QuicheTextUtils::Uint64ToString(200);
  response_headers["content-length"] =
      quiche::QuicheTextUtils::Uint64ToString(response_body.length());
  memory_cache_backend_.AddSpecialResponse(
      server_hostname_, "/test_url", std::move(response_headers), response_body,
      QuicBackendResponse::INCOMPLETE_RESPONSE);

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  client_->WaitForDelayedAcks();

  QuicSession* session = GetClientSession();
  const QuicPacketCount packets_sent_before =
      session->connection()->GetStats().packets_sent;

  client_->SendRequestAndRstTogether("/test_url");

  // Expect exactly one packet is sent from the block above.
  ASSERT_EQ(packets_sent_before + 1,
            session->connection()->GetStats().packets_sent);

  // Wait for the connection to become idle.
  client_->WaitForDelayedAcks();

  // The real expectation is the test does not crash or timeout.
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

TEST_P(EndToEndTest, ResetStreamOnTtlExpires) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  SetPacketLossPercentage(30);

  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  // Set a TTL which expires immediately.
  stream->MaybeSetTtl(QuicTime::Delta::FromMicroseconds(1));

  WriteHeadersOnStream(stream);
  // 1 MB body.
  std::string body(1024 * 1024, 'a');
  stream->WriteOrBufferBody(body, true);
  client_->WaitForResponse();
  EXPECT_THAT(client_->stream_error(), IsStreamError(QUIC_STREAM_TTL_EXPIRED));
}

TEST_P(EndToEndTest, SendMessages) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  QuicSession* client_session = GetClientSession();
  QuicConnection* client_connection = client_session->connection();
  if (!VersionSupportsMessageFrames(client_connection->transport_version())) {
    return;
  }

  SetPacketLossPercentage(30);
  ASSERT_GT(kMaxOutgoingPacketSize,
            client_session->GetCurrentLargestMessagePayload());
  ASSERT_LT(0, client_session->GetCurrentLargestMessagePayload());

  std::string message_string(kMaxOutgoingPacketSize, 'a');
  quiche::QuicheStringPiece message_buffer(message_string);
  QuicRandom* random =
      QuicConnectionPeer::GetHelper(client_connection)->GetRandomGenerator();
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  {
    QuicConnection::ScopedPacketFlusher flusher(client_session->connection());
    // Verify the largest message gets successfully sent.
    EXPECT_EQ(MessageResult(MESSAGE_STATUS_SUCCESS, 1),
              client_session->SendMessage(MakeSpan(
                  client_session->connection()
                      ->helper()
                      ->GetStreamSendBufferAllocator(),
                  quiche::QuicheStringPiece(
                      message_buffer.data(),
                      client_session->GetCurrentLargestMessagePayload()),
                  &storage)));
    // Send more messages with size (0, largest_payload] until connection is
    // write blocked.
    const int kTestMaxNumberOfMessages = 100;
    for (size_t i = 2; i <= kTestMaxNumberOfMessages; ++i) {
      size_t message_length =
          random->RandUint64() %
              client_session->GetCurrentLargestMessagePayload() +
          1;
      MessageResult result = client_session->SendMessage(MakeSpan(
          client_session->connection()
              ->helper()
              ->GetStreamSendBufferAllocator(),
          quiche::QuicheStringPiece(message_buffer.data(), message_length),
          &storage));
      if (result.status == MESSAGE_STATUS_BLOCKED) {
        // Connection is write blocked.
        break;
      }
      EXPECT_EQ(MessageResult(MESSAGE_STATUS_SUCCESS, i), result);
    }
  }

  client_->WaitForDelayedAcks();
  EXPECT_EQ(MESSAGE_STATUS_TOO_LARGE,
            client_session
                ->SendMessage(MakeSpan(
                    client_session->connection()
                        ->helper()
                        ->GetStreamSendBufferAllocator(),
                    quiche::QuicheStringPiece(
                        message_buffer.data(),
                        client_session->GetCurrentLargestMessagePayload() + 1),
                    &storage))
                .status);
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

class EndToEndPacketReorderingTest : public EndToEndTest {
 public:
  void CreateClientWithWriter() override {
    QUIC_LOG(ERROR) << "create client with reorder_writer_";
    reorder_writer_ = new PacketReorderingWriter();
    client_.reset(EndToEndTest::CreateQuicClient(reorder_writer_));
  }

  void SetUp() override {
    // Don't initialize client writer in base class.
    server_writer_ = new PacketDroppingTestWriter();
  }

 protected:
  PacketReorderingWriter* reorder_writer_;
};

INSTANTIATE_TEST_SUITE_P(EndToEndPacketReorderingTests,
                         EndToEndPacketReorderingTest,
                         ::testing::ValuesIn(GetTestParams(false)),
                         ::testing::PrintToStringParamName());

TEST_P(EndToEndPacketReorderingTest, ReorderedConnectivityProbing) {
  ASSERT_TRUE(Initialize());

  // Finish one request to make sure handshake established.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  // Wait for the connection to become idle, to make sure the packet gets
  // delayed is the connectivity probing packet.
  client_->WaitForDelayedAcks();

  QuicSocketAddress old_addr =
      client_->client()->network_helper()->GetLatestClientAddress();

  // Migrate socket to the new IP address.
  QuicIpAddress new_host = TestLoopback(2);
  EXPECT_NE(old_addr.host(), new_host);
  ASSERT_TRUE(client_->client()->MigrateSocket(new_host));

  // Write a connectivity probing after the next /foo request.
  reorder_writer_->SetDelay(1);
  client_->SendConnectivityProbing();

  ASSERT_TRUE(client_->MigrateSocketWithSpecifiedPort(old_addr.host(),
                                                      old_addr.port()));

  // The (delayed) connectivity probing will be sent after this request.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  // Send yet another request after the connectivity probing, when this request
  // returns, the probing is guaranteed to have been received by the server, and
  // the server's response to probing is guaranteed to have been received by the
  // client.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  EXPECT_EQ(1u,
            server_connection->GetStats().num_connectivity_probing_received);
  server_thread_->Resume();

  EXPECT_EQ(
      1u, GetClientConnection()->GetStats().num_connectivity_probing_received);
}

TEST_P(EndToEndPacketReorderingTest, Buffer0RttRequest) {
  ASSERT_TRUE(Initialize());
  // Finish one request to make sure handshake established.
  client_->SendSynchronousRequest("/foo");
  // Disconnect for next 0-rtt request.
  client_->Disconnect();

  // Client get valid STK now. Do a 0-rtt request.
  // Buffer a CHLO till another packets sent out.
  reorder_writer_->SetDelay(1);
  // Only send out a CHLO.
  client_->client()->Initialize();
  client_->client()->StartConnect();
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  ASSERT_TRUE(client_->client()->connected());

  // Send a request before handshake finishes.
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/bar";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  client_->SendMessage(headers, "");
  client_->WaitForResponse();
  EXPECT_EQ(kBarResponseBody, client_->response_body());
  QuicConnectionStats client_stats = GetClientConnection()->GetStats();
  EXPECT_EQ(0u, client_stats.packets_lost);
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());
}

// Test that STOP_SENDING makes it to the peer.  Create a stream and send a
// STOP_SENDING. The receiver should get a call to QuicStream::OnStopSending.
TEST_P(EndToEndTest, SimpleStopSendingTest) {
  const uint16_t kStopSendingTestCode = 123;
  ASSERT_TRUE(Initialize());
  if (!VersionHasIetfQuicFrames(negotiated_version_.transport_version)) {
    return;
  }
  QuicSession* client_session = GetClientSession();
  ASSERT_NE(nullptr, client_session);
  QuicConnection* client_connection = client_session->connection();
  ASSERT_NE(nullptr, client_connection);

  std::string response_body(1305, 'a');
  SpdyHeaderBlock response_headers;
  response_headers[":status"] = quiche::QuicheTextUtils::Uint64ToString(200);
  response_headers["content-length"] =
      quiche::QuicheTextUtils::Uint64ToString(response_body.length());
  memory_cache_backend_.AddStopSendingResponse(
      server_hostname_, "/test_url", std::move(response_headers), response_body,
      kStopSendingTestCode);

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  client_->WaitForDelayedAcks();

  QuicSession* session = GetClientSession();
  const QuicPacketCount packets_sent_before =
      session->connection()->GetStats().packets_sent;

  QuicStreamId stream_id = session->next_outgoing_bidirectional_stream_id();
  client_->SendRequest("/test_url");

  // Expect exactly one packet is sent from the block above.
  ASSERT_EQ(packets_sent_before + 1,
            session->connection()->GetStats().packets_sent);

  // Wait for the connection to become idle.
  client_->WaitForDelayedAcks();

  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
  QuicSimpleClientStream* client_stream =
      static_cast<QuicSimpleClientStream*>(client_->latest_created_stream());
  ASSERT_NE(nullptr, client_stream);
  // Ensure the stream has been write closed upon receiving STOP_SENDING.
  EXPECT_EQ(stream_id, client_stream->id());
  EXPECT_TRUE(client_stream->write_side_closed());
  EXPECT_EQ(kStopSendingTestCode,
            static_cast<uint16_t>(client_stream->stream_error()));
}

TEST_P(EndToEndTest, SimpleStopSendingRstStreamTest) {
  ASSERT_TRUE(Initialize());

  // Send a request without a fin, to keep the stream open
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  client_->SendMessage(headers, "", /*fin=*/false);
  // Stream should be open
  ASSERT_NE(nullptr, client_->latest_created_stream());
  EXPECT_FALSE(client_->latest_created_stream()->write_side_closed());
  EXPECT_FALSE(
      QuicStreamPeer::read_side_closed(client_->latest_created_stream()));

  // Send a RST_STREAM+STOP_SENDING on the stream
  // Code is not important.
  client_->latest_created_stream()->Reset(QUIC_BAD_APPLICATION_PAYLOAD);
  client_->WaitForResponse();

  // Stream should be gone.
  ASSERT_EQ(nullptr, client_->latest_created_stream());
}

class BadShloPacketWriter : public QuicPacketWriterWrapper {
 public:
  BadShloPacketWriter() : error_returned_(false) {}
  ~BadShloPacketWriter() override {}

  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          quic::PerPacketOptions* options) override {
    const WriteResult result = QuicPacketWriterWrapper::WritePacket(
        buffer, buf_len, self_address, peer_address, options);
    const uint8_t type_byte = buffer[0];
    if (!error_returned_ && (type_byte & FLAGS_LONG_HEADER) &&
        (((type_byte & 0x30) >> 4) == 1 || (type_byte & 0x7F) == 0x7C)) {
      QUIC_DVLOG(1) << "Return write error for ZERO_RTT_PACKET";
      error_returned_ = true;
      return WriteResult(WRITE_STATUS_ERROR, QUIC_EMSGSIZE);
    }
    return result;
  }

 private:
  bool error_returned_;
};

TEST_P(EndToEndTest, ZeroRttProtectedConnectionClose) {
  // This test ensures ZERO_RTT_PROTECTED connection close could close a client
  // which has switched to forward secure.
  connect_to_server_on_initialize_ =
      !VersionHasIetfInvariantHeader(negotiated_version_.transport_version);
  ASSERT_TRUE(Initialize());
  if (!VersionHasIetfInvariantHeader(negotiated_version_.transport_version)) {
    // Only runs for IETF QUIC header.
    return;
  }
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  ASSERT_EQ(0u, dispatcher->session_map().size());
  // Note: this writer will only used by the server connection, not the time
  // wait list.
  QuicDispatcherPeer::UseWriter(
      dispatcher,
      // This causes the first server sent ZERO_RTT_PROTECTED packet (i.e.,
      // SHLO) to be sent, but WRITE_ERROR is returned. Such that a
      // ZERO_RTT_PROTECTED connection close would be sent to a client with
      // encryption level FORWARD_SECURE.
      new BadShloPacketWriter());
  server_thread_->Resume();

  client_.reset(CreateQuicClient(client_writer_));
  EXPECT_EQ("", client_->SendSynchronousRequest("/foo"));
  // Verify ZERO_RTT_PROTECTED connection close is successfully processed by
  // client which switches to FORWARD_SECURE.
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_PACKET_WRITE_ERROR));
}

class BadShloPacketWriter2 : public QuicPacketWriterWrapper {
 public:
  BadShloPacketWriter2() : error_returned_(false) {}
  ~BadShloPacketWriter2() override {}

  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          quic::PerPacketOptions* options) override {
    const uint8_t type_byte = buffer[0];
    if ((type_byte & FLAGS_LONG_HEADER) &&
        (((type_byte & 0x30) >> 4) == 1 || (type_byte & 0x7F) == 0x7C)) {
      QUIC_DVLOG(1) << "Dropping ZERO_RTT_PACKET packet";
      return WriteResult(WRITE_STATUS_OK, buf_len);
    }
    if (!error_returned_ && !(type_byte & FLAGS_LONG_HEADER)) {
      QUIC_DVLOG(1) << "Return write error for short header packet";
      error_returned_ = true;
      return WriteResult(WRITE_STATUS_ERROR, QUIC_EMSGSIZE);
    }
    return QuicPacketWriterWrapper::WritePacket(buffer, buf_len, self_address,
                                                peer_address, options);
  }

 private:
  bool error_returned_;
};

TEST_P(EndToEndTest, ForwardSecureConnectionClose) {
  // This test ensures ZERO_RTT_PROTECTED connection close is sent to a client
  // which has ZERO_RTT_PROTECTED encryption level.
  connect_to_server_on_initialize_ =
      !VersionHasIetfInvariantHeader(negotiated_version_.transport_version);
  ASSERT_TRUE(Initialize());
  if (!VersionHasIetfInvariantHeader(negotiated_version_.transport_version)) {
    // Only runs for IETF QUIC header.
    return;
  }
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  ASSERT_EQ(0u, dispatcher->session_map().size());
  // Note: this writer will only used by the server connection, not the time
  // wait list.
  QuicDispatcherPeer::UseWriter(
      dispatcher,
      // This causes the all server sent ZERO_RTT_PROTECTED packets to be
      // dropped, and first short header packet causes write error.
      new BadShloPacketWriter2());
  server_thread_->Resume();
  client_.reset(CreateQuicClient(client_writer_));
  EXPECT_EQ("", client_->SendSynchronousRequest("/foo"));
  // Verify ZERO_RTT_PROTECTED connection close is successfully processed by
  // client.
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_PACKET_WRITE_ERROR));
}

// Test that the stream id manager closes the connection if a stream
// in excess of the allowed maximum.
TEST_P(EndToEndTest, TooBigStreamIdClosesConnection) {
  // Has to be before version test, see EndToEndTest::TearDown()
  ASSERT_TRUE(Initialize());
  if (!VersionHasIetfQuicFrames(negotiated_version_.transport_version)) {
    // Only runs for IETF QUIC.
    return;
  }
  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());

  std::string body(kMaxOutgoingPacketSize, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  // Force the client to write with a stream ID that exceeds the limit.
  QuicSpdySession* session = GetClientSession();
  QuicStreamIdManager* stream_id_manager =
      QuicSessionPeer::v99_bidirectional_stream_id_manager(session);
  QuicStreamCount max_number_of_streams =
      stream_id_manager->outgoing_max_streams();
  QuicSessionPeer::SetNextOutgoingBidirectionalStreamId(
      session, GetNthClientInitiatedBidirectionalId(max_number_of_streams + 1));
  client_->SendCustomSynchronousRequest(headers, body);
  EXPECT_THAT(client_->stream_error(),
              IsStreamError(QUIC_STREAM_CONNECTION_ERROR));
  EXPECT_THAT(GetClientSession()->error(), IsError(QUIC_INVALID_STREAM_ID));
  EXPECT_EQ(IETF_QUIC_TRANSPORT_CONNECTION_CLOSE,
            GetClientSession()->close_type());
  EXPECT_TRUE(
      IS_IETF_STREAM_FRAME(GetClientSession()->transport_close_frame_type()));
}

TEST_P(EndToEndTest, TestMaxPushId) {
  // Has to be before version test, see EndToEndTest::TearDown()
  ASSERT_TRUE(Initialize());
  if (!VersionHasIetfQuicFrames(negotiated_version_.transport_version)) {
    // Only runs for IETF QUIC.
    return;
  }

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  static_cast<QuicSpdySession*>(client_->client()->session())
      ->SetMaxPushId(kMaxQuicStreamId);

  client_->SendSynchronousRequest("/foo");

  EXPECT_TRUE(static_cast<QuicSpdySession*>(client_->client()->session())
                  ->CanCreatePushStreamWithId(kMaxQuicStreamId));

  EXPECT_TRUE(static_cast<QuicSpdySession*>(GetServerSession())
                  ->CanCreatePushStreamWithId(kMaxQuicStreamId));
}

TEST_P(EndToEndTest, CustomTransportParameters) {
  if (GetParam().negotiated_version.handshake_protocol != PROTOCOL_TLS1_3) {
    Initialize();
    return;
  }

  constexpr auto kCustomParameter =
      static_cast<TransportParameters::TransportParameterId>(0xff34);
  client_config_.custom_transport_parameters_to_send()[kCustomParameter] =
      "test";
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForCryptoHandshakeConfirmed());
  EXPECT_EQ(server_config_.received_custom_transport_parameters().at(
                kCustomParameter),
            "test");
}

}  // namespace
}  // namespace test
}  // namespace quic
