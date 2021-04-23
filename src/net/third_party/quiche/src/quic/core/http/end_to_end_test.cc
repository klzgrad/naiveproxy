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

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quic/core/crypto/null_encrypter.h"
#include "quic/core/http/http_constants.h"
#include "quic/core/http/quic_spdy_client_stream.h"
#include "quic/core/quic_data_writer.h"
#include "quic/core/quic_epoll_connection_helper.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_framer.h"
#include "quic/core/quic_packet_creator.h"
#include "quic/core/quic_packet_writer_wrapper.h"
#include "quic/core/quic_packets.h"
#include "quic/core/quic_session.h"
#include "quic/core/quic_utils.h"
#include "quic/platform/api/quic_epoll.h"
#include "quic/platform/api/quic_error_code_wrappers.h"
#include "quic/platform/api/quic_expect_bug.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/platform/api/quic_port_utils.h"
#include "quic/platform/api/quic_sleep.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_test.h"
#include "quic/platform/api/quic_test_loopback.h"
#include "quic/test_tools/bad_packet_writer.h"
#include "quic/test_tools/crypto_test_utils.h"
#include "quic/test_tools/packet_dropping_test_writer.h"
#include "quic/test_tools/packet_reordering_writer.h"
#include "quic/test_tools/qpack/qpack_encoder_peer.h"
#include "quic/test_tools/qpack/qpack_encoder_test_utils.h"
#include "quic/test_tools/qpack/qpack_header_table_peer.h"
#include "quic/test_tools/qpack/qpack_test_utils.h"
#include "quic/test_tools/quic_client_peer.h"
#include "quic/test_tools/quic_config_peer.h"
#include "quic/test_tools/quic_connection_peer.h"
#include "quic/test_tools/quic_dispatcher_peer.h"
#include "quic/test_tools/quic_flow_controller_peer.h"
#include "quic/test_tools/quic_sent_packet_manager_peer.h"
#include "quic/test_tools/quic_server_peer.h"
#include "quic/test_tools/quic_session_peer.h"
#include "quic/test_tools/quic_spdy_session_peer.h"
#include "quic/test_tools/quic_stream_id_manager_peer.h"
#include "quic/test_tools/quic_stream_peer.h"
#include "quic/test_tools/quic_stream_sequencer_peer.h"
#include "quic/test_tools/quic_test_client.h"
#include "quic/test_tools/quic_test_server.h"
#include "quic/test_tools/quic_test_utils.h"
#include "quic/test_tools/server_thread.h"
#include "quic/test_tools/simple_session_cache.h"
#include "quic/tools/quic_backend_response.h"
#include "quic/tools/quic_client.h"
#include "quic/tools/quic_memory_cache_backend.h"
#include "quic/tools/quic_server.h"
#include "quic/tools/quic_simple_client_stream.h"
#include "quic/tools/quic_simple_server_stream.h"
#include "common/platform/api/quiche_text_utils.h"

using spdy::kV3LowestPriority;
using spdy::SpdyFramer;
using spdy::SpdyHeaderBlock;
using spdy::SpdySerializedFrame;
using spdy::SpdySettingsIR;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

namespace quic {
namespace test {
namespace {

const char kFooResponseBody[] = "Artichoke hearts make me happy.";
const char kBarResponseBody[] = "Palm hearts are pretty delicious, also.";
const char kTestUserAgentId[] = "quic/core/http/end_to_end_test.cc";
const float kSessionToStreamRatio = 1.5;

// Run all tests with the cross products of all versions.
struct TestParams {
  TestParams(const ParsedQuicVersion& version, QuicTag congestion_control_tag)
      : version(version), congestion_control_tag(congestion_control_tag) {}

  friend std::ostream& operator<<(std::ostream& os, const TestParams& p) {
    os << "{ version: " << ParsedQuicVersionToString(p.version);
    os << " congestion_control_tag: "
       << QuicTagToString(p.congestion_control_tag) << " }";
    return os;
  }

  ParsedQuicVersion version;
  QuicTag congestion_control_tag;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  std::string rv = absl::StrCat(ParsedQuicVersionToString(p.version), "_",
                                QuicTagToString(p.congestion_control_tag));
  std::replace(rv.begin(), rv.end(), ',', '_');
  std::replace(rv.begin(), rv.end(), ' ', '_');
  return rv;
}

// Constructs various test permutations.
std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  for (const QuicTag congestion_control_tag : {kRENO, kTBBR, kQBIC, kB2ON}) {
    if (!GetQuicReloadableFlag(quic_allow_client_enabled_bbr_v2) &&
        congestion_control_tag == kB2ON) {
      continue;
    }
    for (const ParsedQuicVersion& version : CurrentSupportedVersions()) {
      params.push_back(TestParams(version, congestion_control_tag));
    }  // End of outer version loop.
  }    // End of congestion_control_tag loop.

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
        version_(GetParam().version),
        client_supported_versions_({version_}),
        server_supported_versions_(CurrentSupportedVersions()),
        chlo_multiplier_(0),
        stream_factory_(nullptr),
        expected_server_connection_id_length_(kQuicDefaultConnectionIdLength) {
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
    // Enable fixes for bugs found in tests and prod.
    SetQuicRestartFlag(quic_enable_zero_rtt_for_tls_v2, true);
  }

  ~EndToEndTest() override { QuicRecyclePort(server_address_.port()); }

  virtual void CreateClientWithWriter() {
    client_.reset(CreateQuicClient(client_writer_));
  }

  QuicTestClient* CreateQuicClient(QuicPacketWriterWrapper* writer) {
    QuicTestClient* client =
        new QuicTestClient(server_address_, server_hostname_, client_config_,
                           client_supported_versions_,
                           crypto_test_utils::ProofVerifierForTesting(),
                           std::make_unique<SimpleSessionCache>());
    client->SetUserAgentID(kTestUserAgentId);
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
    QuicConnection* server_connection = GetServerConnection();
    if (server_connection == nullptr) {
      ADD_FAILURE() << "Missing server connection";
      return nullptr;
    }
    return &server_connection->sent_packet_manager();
  }

  const QuicSentPacketManager* GetSentPacketManagerFromClientSession() {
    QuicConnection* client_connection = GetClientConnection();
    if (client_connection == nullptr) {
      ADD_FAILURE() << "Missing client connection";
      return nullptr;
    }
    return &client_connection->sent_packet_manager();
  }

  QuicSpdyClientSession* GetClientSession() {
    if (!client_) {
      ADD_FAILURE() << "Missing QuicTestClient";
      return nullptr;
    }
    if (client_->client() == nullptr) {
      ADD_FAILURE() << "Missing MockableQuicClient";
      return nullptr;
    }
    return client_->client()->client_session();
  }

  QuicConnection* GetClientConnection() {
    QuicSpdyClientSession* client_session = GetClientSession();
    if (client_session == nullptr) {
      ADD_FAILURE() << "Missing client session";
      return nullptr;
    }
    return client_session->connection();
  }

  QuicConnection* GetServerConnection() {
    QuicSpdySession* server_session = GetServerSession();
    if (server_session == nullptr) {
      ADD_FAILURE() << "Missing server session";
      return nullptr;
    }
    return server_session->connection();
  }

  QuicSpdySession* GetServerSession() {
    if (!server_thread_) {
      ADD_FAILURE() << "Missing server thread";
      return nullptr;
    }
    QuicServer* quic_server = server_thread_->server();
    if (quic_server == nullptr) {
      ADD_FAILURE() << "Missing server";
      return nullptr;
    }
    QuicDispatcher* dispatcher = QuicServerPeer::GetDispatcher(quic_server);
    if (dispatcher == nullptr) {
      ADD_FAILURE() << "Missing dispatcher";
      return nullptr;
    }
    if (dispatcher->NumSessions() == 0) {
      ADD_FAILURE() << "Empty dispatcher session map";
      return nullptr;
    }
    EXPECT_EQ(1u, dispatcher->NumSessions());
    return static_cast<QuicSpdySession*>(
        QuicDispatcherPeer::GetFirstSessionIfAny(dispatcher));
  }

  bool Initialize() {
    QuicTagVector copt;
    server_config_.SetConnectionOptionsToSend(copt);
    copt = client_extra_copts_;

    // TODO(nimia): Consider setting the congestion control algorithm for the
    // client as well according to the test parameter.
    copt.push_back(GetParam().congestion_control_tag);
    copt.push_back(k2PTO);
    if (version_.HasIetfQuicFrames()) {
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
    if (!client_) {
      ADD_FAILURE() << "Missing QuicTestClient";
      return false;
    }
    MockableQuicClient* client = client_->client();
    if (client == nullptr) {
      ADD_FAILURE() << "Missing MockableQuicClient";
      return false;
    }
    static QuicEpollEvent event(EPOLLOUT);
    if (client_writer_ != nullptr) {
      QuicConnection* client_connection = GetClientConnection();
      if (client_connection == nullptr) {
        ADD_FAILURE() << "Missing client connection";
        return false;
      }
      client_writer_->Initialize(
          QuicConnectionPeer::GetHelper(client_connection),
          QuicConnectionPeer::GetAlarmFactory(client_connection),
          std::make_unique<ClientDelegate>(client));
    }
    initialized_ = true;
    return client->connected();
  }

  void SetUp() override {
    // The ownership of these gets transferred to the QuicPacketWriterWrapper
    // when Initialize() is executed.
    client_writer_ = new PacketDroppingTestWriter();
    server_writer_ = new PacketDroppingTestWriter();
  }

  void TearDown() override {
    EXPECT_TRUE(initialized_) << "You must call Initialize() in every test "
                              << "case. Otherwise, your test will leak memory.";
    QuicConnection* client_connection = GetClientConnection();
    if (client_connection != nullptr) {
      client_connection->set_debug_visitor(nullptr);
    } else {
      ADD_FAILURE() << "Missing client connection";
    }
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

  void AddToCache(absl::string_view path,
                  int response_code,
                  absl::string_view body) {
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
    QuicConnection* client_connection = GetClientConnection();
    if (client_connection == nullptr) {
      ADD_FAILURE() << "Missing client connection";
      return;
    }
    QuicConnectionStats client_stats = client_connection->GetStats();
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
    if (!version_.UsesTls()) {
      // Only enforce this for QUIC crypto because accounting of number of
      // packets received, processed gets complicated with packets coalescing
      // and key dropping. For example, a received undecryptable coalesced
      // packet can be processed later and each sub-packet increases
      // packets_processed.
      EXPECT_EQ(client_stats.packets_received, client_stats.packets_processed);
    }

    if (!server_thread_) {
      ADD_FAILURE() << "Missing server thread";
      return;
    }
    server_thread_->Pause();
    QuicSpdySession* server_session = GetServerSession();
    if (server_session != nullptr) {
      QuicConnection* server_connection = server_session->connection();
      if (server_connection != nullptr) {
        QuicConnectionStats server_stats = server_connection->GetStats();
        if (!had_packet_loss) {
          EXPECT_EQ(0u, server_stats.packets_lost);
        }
        EXPECT_EQ(0u, server_stats.packets_discarded);
        EXPECT_EQ(server_session->user_agent_id().value_or("MissingUserAgent"),
                  kTestUserAgentId);
      } else {
        ADD_FAILURE() << "Missing server connection";
      }
    } else {
      ADD_FAILURE() << "Missing server session";
    }
    // TODO(ianswett): Restore the check for packets_dropped equals 0.
    // The expect for packets received is equal to packets processed fails
    // due to version negotiation packets.
    server_thread_->Resume();
  }

  // Returns true when client starts with an unsupported version, and client
  // closes connection when version negotiation is received.
  bool ServerSendsVersionNegotiation() {
    return client_supported_versions_[0] != version_;
  }

  bool SupportsIetfQuicWithTls(ParsedQuicVersion version) {
    return version.HasIetfInvariantHeader() &&
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
    EXPECT_EQ(QuicStreamPeer::SendWindowSize(client),
              QuicStreamPeer::ReceiveWindowSize(server));
    EXPECT_EQ(QuicStreamPeer::ReceiveWindowSize(client),
              QuicStreamPeer::SendWindowSize(server));
  }

  // Must be called before Initialize to have effect.
  void SetSpdyStreamFactory(QuicTestServer::StreamFactory* factory) {
    stream_factory_ = factory;
  }

  QuicStreamId GetNthClientInitiatedBidirectionalId(int n) {
    return GetNthClientInitiatedBidirectionalStreamId(
        version_.transport_version, n);
  }

  QuicStreamId GetNthServerInitiatedBidirectionalId(int n) {
    return GetNthServerInitiatedBidirectionalStreamId(
        version_.transport_version, n);
  }

  bool CheckResponseHeaders(QuicTestClient* client,
                            const std::string& expected_status) {
    const spdy::SpdyHeaderBlock* response_headers = client->response_headers();
    auto it = response_headers->find(":status");
    if (it == response_headers->end()) {
      ADD_FAILURE() << "Did not find :status header in response";
      return false;
    }
    if (it->second != expected_status) {
      ADD_FAILURE() << "Got bad :status response: \"" << it->second << "\"";
      return false;
    }
    return true;
  }

  bool CheckResponseHeaders(QuicTestClient* client) {
    return CheckResponseHeaders(client, "200");
  }

  bool CheckResponseHeaders(const std::string& expected_status) {
    return CheckResponseHeaders(client_.get(), expected_status);
  }

  bool CheckResponseHeaders() { return CheckResponseHeaders(client_.get()); }

  bool CheckResponse(QuicTestClient* client,
                     const std::string& received_response,
                     const std::string& expected_response) {
    EXPECT_THAT(client_->stream_error(), IsQuicStreamNoError());
    EXPECT_THAT(client_->connection_error(), IsQuicNoError());

    if (received_response.empty() && !expected_response.empty()) {
      ADD_FAILURE() << "Failed to get any response for request";
      return false;
    }
    if (received_response != expected_response) {
      ADD_FAILURE() << "Got wrong response: \"" << received_response << "\"";
      return false;
    }
    return CheckResponseHeaders(client);
  }

  bool SendSynchronousRequestAndCheckResponse(
      QuicTestClient* client,
      const std::string& request,
      const std::string& expected_response) {
    std::string received_response = client->SendSynchronousRequest(request);
    return CheckResponse(client, received_response, expected_response);
  }

  bool SendSynchronousRequestAndCheckResponse(
      const std::string& request,
      const std::string& expected_response) {
    return SendSynchronousRequestAndCheckResponse(client_.get(), request,
                                                  expected_response);
  }

  bool SendSynchronousFooRequestAndCheckResponse(QuicTestClient* client) {
    return SendSynchronousRequestAndCheckResponse(client, "/foo",
                                                  kFooResponseBody);
  }

  bool SendSynchronousFooRequestAndCheckResponse() {
    return SendSynchronousFooRequestAndCheckResponse(client_.get());
  }

  bool SendSynchronousBarRequestAndCheckResponse() {
    std::string received_response = client_->SendSynchronousRequest("/bar");
    return CheckResponse(client_.get(), received_response, kBarResponseBody);
  }

  bool WaitForFooResponseAndCheckIt(QuicTestClient* client) {
    client->WaitForResponse();
    std::string received_response = client->response_body();
    return CheckResponse(client_.get(), received_response, kFooResponseBody);
  }

  bool WaitForFooResponseAndCheckIt() {
    return WaitForFooResponseAndCheckIt(client_.get());
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
  ParsedQuicVersion version_;
  ParsedQuicVersionVector client_supported_versions_;
  ParsedQuicVersionVector server_supported_versions_;
  QuicTagVector client_extra_copts_;
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
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(EndToEndTest, HandshakeSuccessful) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(server_thread_);
  server_thread_->WaitForCryptoHandshakeConfirmed();
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicCryptoStream* client_crypto_stream =
      QuicSessionPeer::GetMutableCryptoStream(client_session);
  ASSERT_TRUE(client_crypto_stream);
  QuicStreamSequencer* client_sequencer =
      QuicStreamPeer::sequencer(client_crypto_stream);
  ASSERT_TRUE(client_sequencer);
  EXPECT_FALSE(
      QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(client_sequencer));

  // We've had bugs in the past where the connections could end up on the wrong
  // version. This was never diagnosed but could have been due to in-connection
  // version negotiation back when that existed. At this point in time, our test
  // setup ensures that connections here always use |version_|, but we add this
  // sanity check out of paranoia to catch a regression of this type.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(client_connection->version(), version_);

  server_thread_->Pause();
  QuicSpdySession* server_session = GetServerSession();
  QuicConnection* server_connection = nullptr;
  QuicCryptoStream* server_crypto_stream = nullptr;
  QuicStreamSequencer* server_sequencer = nullptr;
  if (server_session != nullptr) {
    server_connection = server_session->connection();
    server_crypto_stream =
        QuicSessionPeer::GetMutableCryptoStream(server_session);
  } else {
    ADD_FAILURE() << "Missing server session";
  }
  if (server_crypto_stream != nullptr) {
    server_sequencer = QuicStreamPeer::sequencer(server_crypto_stream);
  } else {
    ADD_FAILURE() << "Missing server crypto stream";
  }
  if (server_sequencer != nullptr) {
    EXPECT_FALSE(
        QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(server_sequencer));
  } else {
    ADD_FAILURE() << "Missing server sequencer";
  }
  if (server_connection != nullptr) {
    EXPECT_EQ(server_connection->version(), version_);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, SimpleRequestResponse) {
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  if (version_.UsesHttp3()) {
    QuicSpdyClientSession* client_session = GetClientSession();
    ASSERT_TRUE(client_session);
    EXPECT_TRUE(QuicSpdySessionPeer::GetSendControlStream(client_session));
    EXPECT_TRUE(QuicSpdySessionPeer::GetReceiveControlStream(client_session));
    server_thread_->Pause();
    QuicSpdySession* server_session = GetServerSession();
    if (server_session != nullptr) {
      EXPECT_TRUE(QuicSpdySessionPeer::GetSendControlStream(server_session));
      EXPECT_TRUE(QuicSpdySessionPeer::GetReceiveControlStream(server_session));
    } else {
      ADD_FAILURE() << "Missing server session";
    }
    server_thread_->Resume();
  }
  QuicConnectionStats client_stats = GetClientConnection()->GetStats();
  EXPECT_TRUE(client_stats.handshake_completion_time.IsInitialized());
}

TEST_P(EndToEndTest, HandshakeConfirmed) {
  ASSERT_TRUE(Initialize());
  if (!version_.UsesTls()) {
    return;
  }
  SendSynchronousFooRequestAndCheckResponse();
  // Verify handshake state.
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_EQ(HANDSHAKE_CONFIRMED, client_session->GetHandshakeState());
  server_thread_->Pause();
  QuicSpdySession* server_session = GetServerSession();
  if (server_session != nullptr) {
    EXPECT_EQ(HANDSHAKE_CONFIRMED, server_session->GetHandshakeState());
  } else {
    ADD_FAILURE() << "Missing server session";
  }
  server_thread_->Resume();
  client_->Disconnect();
}

TEST_P(EndToEndTest, SendAndReceiveCoalescedPackets) {
  ASSERT_TRUE(Initialize());
  if (!version_.CanSendCoalescedPackets()) {
    return;
  }
  SendSynchronousFooRequestAndCheckResponse();
  // Verify client successfully processes coalesced packets.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionStats client_stats = client_connection->GetStats();
  EXPECT_LT(0u, client_stats.num_coalesced_packets_received);
  EXPECT_EQ(client_stats.num_coalesced_packets_processed,
            client_stats.num_coalesced_packets_received);
  // TODO(fayang): verify server successfully processes coalesced packets.
}

// Simple transaction, but set a non-default ack delay at the client
// and ensure it gets to the server.
TEST_P(EndToEndTest, SimpleRequestResponseWithAckDelayChange) {
  // Force the ACK delay to be something other than the default.
  constexpr uint32_t kClientMaxAckDelay = kDefaultDelayedAckTimeMs + 100u;
  client_config_.SetMaxAckDelayToSendMs(kClientMaxAckDelay);
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  server_thread_->Pause();
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();
  if (server_sent_packet_manager != nullptr) {
    EXPECT_EQ(
        kClientMaxAckDelay,
        server_sent_packet_manager->peer_max_ack_delay().ToMilliseconds());
  } else {
    ADD_FAILURE() << "Missing server sent packet manager";
  }
  server_thread_->Resume();
}

// Simple transaction, but set a non-default ack exponent at the client
// and ensure it gets to the server.
TEST_P(EndToEndTest, SimpleRequestResponseWithAckExponentChange) {
  const uint32_t kClientAckDelayExponent = 19;
  EXPECT_NE(kClientAckDelayExponent, kDefaultAckDelayExponent);
  // Force the ACK exponent to be something other than the default.
  // Note that it is sent only with QUIC+TLS.
  client_config_.SetAckDelayExponentToSend(kClientAckDelayExponent);
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();

  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    if (version_.UsesTls()) {
      // Should be only sent with QUIC+TLS.
      EXPECT_EQ(kClientAckDelayExponent,
                server_connection->framer().peer_ack_delay_exponent());
    } else {
      // No change for QUIC_CRYPTO.
      EXPECT_EQ(kDefaultAckDelayExponent,
                server_connection->framer().peer_ack_delay_exponent());
    }
    // No change, regardless of version.
    EXPECT_EQ(kDefaultAckDelayExponent,
              server_connection->framer().local_ack_delay_exponent());
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, SimpleRequestResponseForcedVersionNegotiation) {
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  NiceMock<MockQuicConnectionDebugVisitor> visitor;
  connection_debug_visitor_ = &visitor;
  EXPECT_CALL(visitor, OnVersionNegotiationPacket(_)).Times(1);
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());

  SendSynchronousFooRequestAndCheckResponse();

  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
}

TEST_P(EndToEndTest, ForcedVersionNegotiation) {
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());

  SendSynchronousFooRequestAndCheckResponse();
}

TEST_P(EndToEndTest, SimpleRequestResponseZeroConnectionID) {
  if (!version_.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 0;
  expected_server_connection_id_length_ = 0;
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(client_connection->connection_id(),
            QuicUtils::CreateZeroConnectionId(version_.transport_version));
}

TEST_P(EndToEndTest, ZeroConnectionID) {
  if (!version_.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 0;
  expected_server_connection_id_length_ = 0;
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(client_connection->connection_id(),
            QuicUtils::CreateZeroConnectionId(version_.transport_version));
}

TEST_P(EndToEndTest, BadConnectionIdLength) {
  if (!version_.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 9;
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

// Tests a very long (16-byte) initial destination connection ID to make
// sure the dispatcher properly replaces it with an 8-byte one.
TEST_P(EndToEndTest, LongBadConnectionIdLength) {
  if (!version_.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 16;
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

TEST_P(EndToEndTest, ClientConnectionId) {
  if (!version_.SupportsClientConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_client_connection_id_length_ = kQuicDefaultConnectionIdLength;
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(override_client_connection_id_length_, client_->client()
                                                       ->client_session()
                                                       ->connection()
                                                       ->client_connection_id()
                                                       .length());
}

TEST_P(EndToEndTest, ForcedVersionNegotiationAndClientConnectionId) {
  if (!version_.SupportsClientConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  override_client_connection_id_length_ = kQuicDefaultConnectionIdLength;
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(override_client_connection_id_length_, client_->client()
                                                       ->client_session()
                                                       ->connection()
                                                       ->client_connection_id()
                                                       .length());
}

TEST_P(EndToEndTest, ForcedVersionNegotiationAndBadConnectionIdLength) {
  if (!version_.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  override_server_connection_id_length_ = 9;
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

// Forced Version Negotiation with a client connection ID and a long
// connection ID.
TEST_P(EndToEndTest, ForcedVersNegoAndClientCIDAndLongCID) {
  if (!version_.SupportsClientConnectionIds() ||
      !version_.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  override_server_connection_id_length_ = 16;
  override_client_connection_id_length_ = 18;
  ASSERT_TRUE(Initialize());
  ASSERT_TRUE(ServerSendsVersionNegotiation());
  SendSynchronousFooRequestAndCheckResponse();
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
  if (!version_.AllowsVariableLengthConnectionIds()) {
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

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client_->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());

  WaitForFooResponseAndCheckIt(client2.get());
  EXPECT_EQ(kQuicDefaultConnectionIdLength, client2->client()
                                                ->client_session()
                                                ->connection()
                                                ->connection_id()
                                                .length());
}

TEST_P(EndToEndTest, SimpleRequestResponseWithIetfDraftSupport) {
  if (!version_.HasIetfQuicFrames()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  QuicVersionInitializeSupportForIetfDraft();
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
}

TEST_P(EndToEndTest, SimpleRequestResponseWithLargeReject) {
  chlo_multiplier_ = 1;
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  if (version_.UsesTls()) {
    // REJ messages are a QUIC crypto feature, so TLS always returns false.
    EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  } else {
    EXPECT_TRUE(client_->client()->ReceivedInchoateReject());
  }
}

TEST_P(EndToEndTest, SimpleRequestResponsev6) {
  server_address_ =
      QuicSocketAddress(QuicIpAddress::Loopback6(), server_address_.port());
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
}

TEST_P(EndToEndTest,
       ClientDoesNotAllowServerDataOnServerInitiatedBidirectionalStreams) {
  set_client_initial_max_stream_data_incoming_bidirectional(0);
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
}

TEST_P(EndToEndTest,
       ServerDoesNotAllowClientDataOnServerInitiatedBidirectionalStreams) {
  set_server_initial_max_stream_data_outgoing_bidirectional(0);
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
}

TEST_P(EndToEndTest,
       BothEndpointsDisallowDataOnServerInitiatedBidirectionalStreams) {
  set_client_initial_max_stream_data_incoming_bidirectional(0);
  set_server_initial_max_stream_data_outgoing_bidirectional(0);
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
}

// Regression test for a bug where we would always fail to decrypt the first
// initial packet. Undecryptable packets can be seen after the handshake
// is complete due to dropping the initial keys at that point, so we only test
// for undecryptable packets before then.
TEST_P(EndToEndTest, NoUndecryptablePacketsBeforeHandshakeComplete) {
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();

  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionStats client_stats = client_connection->GetStats();
  EXPECT_EQ(
      0u,
      client_stats.undecryptable_packets_received_before_handshake_complete);

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    QuicConnectionStats server_stats = server_connection->GetStats();
    EXPECT_EQ(
        0u,
        server_stats.undecryptable_packets_received_before_handshake_complete);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, SeparateFinPacket) {
  ASSERT_TRUE(Initialize());

  // Send a request in two parts: the request and then an empty packet with FIN.
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  client_->SendMessage(headers, "", /*fin=*/false);
  client_->SendData("", true);
  WaitForFooResponseAndCheckIt();

  // Now do the same thing but with a content length.
  headers["content-length"] = "3";
  client_->SendMessage(headers, "", /*fin=*/false);
  client_->SendData("foo", true);
  WaitForFooResponseAndCheckIt();
}

TEST_P(EndToEndTest, MultipleRequestResponse) {
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  SendSynchronousBarRequestAndCheckResponse();
}

TEST_P(EndToEndTest, MultipleRequestResponseZeroConnectionID) {
  if (!version_.AllowsVariableLengthConnectionIds()) {
    ASSERT_TRUE(Initialize());
    return;
  }
  override_server_connection_id_length_ = 0;
  expected_server_connection_id_length_ = 0;
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  SendSynchronousBarRequestAndCheckResponse();
}

TEST_P(EndToEndTest, MultipleStreams) {
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
    ASSERT_TRUE(WaitForFooResponseAndCheckIt());
  }
}

TEST_P(EndToEndTest, MultipleClients) {
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
  WaitForFooResponseAndCheckIt();

  client2->SendData("eep", true);
  WaitForFooResponseAndCheckIt(client2.get());
}

TEST_P(EndToEndTest, RequestOverMultiplePackets) {
  // Send a large enough request to guarantee fragmentation.
  std::string huge_request =
      "/some/path?query=" + std::string(kMaxOutgoingPacketSize, '.');
  AddToCache(huge_request, 200, kBarResponseBody);

  ASSERT_TRUE(Initialize());

  SendSynchronousRequestAndCheckResponse(huge_request, kBarResponseBody);
}

TEST_P(EndToEndTest, MultiplePacketsRandomOrder) {
  // Send a large enough request to guarantee fragmentation.
  std::string huge_request =
      "/some/path?query=" + std::string(kMaxOutgoingPacketSize, '.');
  AddToCache(huge_request, 200, kBarResponseBody);

  ASSERT_TRUE(Initialize());
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(50);

  SendSynchronousRequestAndCheckResponse(huge_request, kBarResponseBody);
}

TEST_P(EndToEndTest, PostMissingBytes) {
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
  CheckResponseHeaders("500");
}

TEST_P(EndToEndTest, LargePostNoPacketLoss) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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
  // Disable blackhole detection as this test is testing loss recovery.
  client_extra_copts_.push_back(kNBHD);
  SetPacketLossPercentage(5);
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForHandshakeConfirmed());
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
  // Disable blackhole detection as this test is testing loss recovery.
  client_extra_copts_.push_back(kNBHD);
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForHandshakeConfirmed());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Normally server only bundles a retransmittable frame once every other
  // kMaxConsecutiveNonRetransmittablePackets ack-only packets. Setting the max
  // to 0 to reliably reproduce b/80090281.
  server_thread_->Schedule([this]() {
    QuicConnection* server_connection = GetServerConnection();
    if (server_connection != nullptr) {
      QuicConnectionPeer::
          SetMaxConsecutiveNumPacketsWithNoRetransmittableFrames(
              server_connection, 0);
    } else {
      ADD_FAILURE() << "Missing server connection";
    }
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
  // Disable blackhole detection as this test is testing loss recovery.
  client_extra_copts_.push_back(kNBHD);
  SetPacketLossPercentage(5);
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForHandshakeConfirmed());
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
  EXPECT_TRUE(client_->client()->WaitForHandshakeConfirmed());
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

TEST_P(EndToEndTest, AddressToken) {
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfQuicFrames()) {
    return;
  }

  SendSynchronousFooRequestAndCheckResponse();
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  SendSynchronousFooRequestAndCheckResponse();

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    if (GetQuicReloadableFlag(quic_enable_token_based_address_validation)) {
      // Verify address is validated via validating token received in INITIAL
      // packet.
      EXPECT_FALSE(server_connection->GetStats()
                       .address_validated_via_decrypting_packet);
      EXPECT_TRUE(server_connection->GetStats().address_validated_via_token);
    } else {
      EXPECT_TRUE(server_connection->GetStats()
                      .address_validated_via_decrypting_packet);
      EXPECT_FALSE(server_connection->GetStats().address_validated_via_token);
    }
  } else {
    ADD_FAILURE() << "Missing server connection";
  }

  server_thread_->Resume();

  client_->Disconnect();
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
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  // Restart the server so that the 0-RTT handshake will take 1 RTT.
  StopServer();
  server_writer_ = new PacketDroppingTestWriter();
  StartServer();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));
  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());
  VerifyCleanConnection(false);
}

// Regression test for b/168020146.
TEST_P(EndToEndTest, MultipleZeroRtt) {
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();
}

TEST_P(EndToEndTest, SynchronousRequestZeroRTTFailure) {
  // Send a request and then disconnect. This prepares the client to attempt
  // a 0-RTT handshake for the next request.
  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  // Restart the server so that the 0-RTT handshake will take 1 RTT.
  StopServer();
  server_writer_ = new PacketDroppingTestWriter();
  StartServer();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
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
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  // Restart the server so that the 0-RTT handshake will take 1 RTT.
  StopServer();
  server_writer_ = new PacketDroppingTestWriter();
  StartServer();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody,
            client_->SendCustomSynchronousRequest(headers, body));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  VerifyCleanConnection(false);
}

// This is a regression test for b/162595387
TEST_P(EndToEndTest, PostZeroRTTRequestDuringHandshake) {
  if (!version_.UsesTls()) {
    // This test is TLS specific.
    ASSERT_TRUE(Initialize());
    return;
  }
  // Send a request and then disconnect. This prepares the client to attempt
  // a 0-RTT handshake for the next request.
  NiceMock<MockQuicConnectionDebugVisitor> visitor;
  connection_debug_visitor_ = &visitor;
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // The 0-RTT handshake should succeed.
  ON_CALL(visitor, OnCryptoFrame(_))
      .WillByDefault(Invoke([this](const QuicCryptoFrame& frame) {
        if (frame.level != ENCRYPTION_HANDSHAKE) {
          return;
        }
        // At this point in the handshake, the client should have derived
        // ENCRYPTION_ZERO_RTT keys (thus set encryption_established). It
        // should also have set ENCRYPTION_HANDSHAKE keys after receiving
        // the server's ENCRYPTION_INITIAL flight.
        EXPECT_TRUE(
            GetClientSession()->GetCryptoStream()->encryption_established());
        EXPECT_TRUE(
            GetClientConnection()->framer().HasEncrypterOfEncryptionLevel(
                ENCRYPTION_HANDSHAKE));
        SpdyHeaderBlock headers;
        headers[":method"] = "POST";
        headers[":path"] = "/foo";
        headers[":scheme"] = "https";
        headers[":authority"] = server_hostname_;
        EXPECT_GT(
            client_->SendMessage(headers, "", /*fin*/ true, /*flush*/ false),
            0);
      }));
  client_->Connect();
  ASSERT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  client_->WaitForWriteToFlush();
  client_->WaitForResponse();
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->response_body());

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());
}

// Regression test for b/166836136.
TEST_P(EndToEndTest, RetransmissionAfterZeroRTTRejectBeforeOneRtt) {
  if (!version_.UsesTls()) {
    // This test is TLS specific.
    ASSERT_TRUE(Initialize());
    return;
  }
  // Send a request and then disconnect. This prepares the client to attempt
  // a 0-RTT handshake for the next request.
  NiceMock<MockQuicConnectionDebugVisitor> visitor;
  connection_debug_visitor_ = &visitor;
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  client_->Disconnect();

  // Restart the server so that the 0-RTT handshake will take 1 RTT.
  StopServer();
  server_writer_ = new PacketDroppingTestWriter();
  StartServer();

  ON_CALL(visitor, OnZeroRttRejected(_)).WillByDefault(Invoke([this]() {
    EXPECT_FALSE(GetClientSession()->IsEncryptionEstablished());
    // Trigger an OnCanWrite() to make sure no unencrypted data will be written.
    GetClientSession()->OnCanWrite();
  }));

  // The 0-RTT handshake should fail.
  client_->Connect();
  ASSERT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  client_->WaitForWriteToFlush();
  client_->WaitForResponse();
  ASSERT_TRUE(client_->client()->connected());

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
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

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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

TEST_P(EndToEndTest, DoNotSetSendAlarmIfConnectionFlowControlBlocked) {
  // Regression test for b/14677858.
  // Test that the resume write alarm is not set in QuicConnection::OnCanWrite
  // if currently connection level flow control blocked. If set, this results in
  // an infinite loop in the EpollServer, as the alarm fires and is immediately
  // rescheduled.
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  // Ensure both stream and connection level are flow control blocked by setting
  // the send window offset to 0.
  const uint64_t flow_control_window =
      server_config_.GetInitialStreamFlowControlWindowToSend();
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  QuicSession* session = GetClientSession();
  ASSERT_TRUE(session);
  QuicStreamPeer::SetSendWindowOffset(stream, 0);
  QuicFlowControllerPeer::SetSendWindowOffset(session->flow_controller(), 0);
  EXPECT_TRUE(stream->IsFlowControlBlocked());
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

TEST_P(EndToEndTest, InvalidStream) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  std::string body(kMaxOutgoingPacketSize, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  // Force the client to write with a stream ID belonging to a nonexistent
  // server-side stream.
  QuicSpdySession* session = GetClientSession();
  ASSERT_TRUE(session);
  QuicSessionPeer::SetNextOutgoingBidirectionalStreamId(
      session, GetNthServerInitiatedBidirectionalId(0));

  client_->SendCustomSynchronousRequest(headers, body);
  EXPECT_THAT(client_->stream_error(),
              IsStreamError(QUIC_STREAM_CONNECTION_ERROR));
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_INVALID_STREAM_ID));
}

// Test that the server resets the stream if the client sends a request
// with overly large headers.
TEST_P(EndToEndTest, LargeHeaders) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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

  if (version_.UsesHttp3()) {
    // QuicSpdyStream::OnHeadersTooLarge() resets the stream with
    // QUIC_HEADERS_TOO_LARGE.  This is sent as H3_EXCESSIVE_LOAD, the closest
    // HTTP/3 error code, and translated back to QUIC_STREAM_EXCESSIVE_LOAD on
    // the receiving side.
    EXPECT_THAT(client_->stream_error(),
                IsStreamError(QUIC_STREAM_EXCESSIVE_LOAD));
  } else {
    EXPECT_THAT(client_->stream_error(), IsStreamError(QUIC_HEADERS_TOO_LARGE));
  }
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

TEST_P(EndToEndTest, EarlyResponseWithQuicStreamNoError) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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
  CheckResponseHeaders("500");
  EXPECT_THAT(client_->stream_error(), IsQuicStreamNoError());
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

// TODO(rch): this test seems to cause net_unittests timeouts :|
TEST_P(EndToEndTest, QUIC_TEST_DISABLED_IN_CHROME(MultipleTermination)) {
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

TEST_P(EndToEndTest, Timeout) {
  client_config_.SetIdleNetworkTimeout(QuicTime::Delta::FromMicroseconds(500));
  // Note: we do NOT ASSERT_TRUE: we may time out during initial handshake:
  // that's enough to validate timeout in this case.
  Initialize();
  while (client_->client()->connected()) {
    client_->client()->WaitForEvents();
  }
}

TEST_P(EndToEndTest, MaxDynamicStreamsLimitRespected) {
  // Set a limit on maximum number of incoming dynamic streams.
  // Make sure the limit is respected by the peer.
  const uint32_t kServerMaxDynamicStreams = 1;
  server_config_.SetMaxBidirectionalStreamsToSend(kServerMaxDynamicStreams);
  ASSERT_TRUE(Initialize());
  if (version_.HasIetfQuicFrames()) {
    // Do not run this test for /IETF QUIC. This test relies on the fact that
    // Google QUIC allows a small number of additional streams beyond the
    // negotiated limit, which is not supported in IETF QUIC. Note that the test
    // needs to be here, after calling Initialize(), because all tests end up
    // calling EndToEndTest::TearDown(), which asserts that Initialize has been
    // called and then proceeds to tear things down -- which fails if they are
    // not properly set up.
    return;
  }
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  // Make the client misbehave after negotiation.
  const int kServerMaxStreams = kMaxStreamsMinimumIncrement + 1;
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicSessionPeer::SetMaxOpenOutgoingStreams(client_session,
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  // The client has received the server's limit and vice versa.
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  // The value returned by max_allowed... includes the Crypto and Header
  // stream (created as a part of initialization). The config. values,
  // above, are treated as "number of requests/responses" - that is, they do
  // not include the static Crypto and Header streams. Reduce the value
  // returned by max_allowed... by 2 to remove the static streams from the
  // count.
  size_t client_max_open_outgoing_bidirectional_streams =
      version_.HasIetfQuicFrames()
          ? QuicSessionPeer::ietf_streamid_manager(client_session)
                ->max_outgoing_bidirectional_streams()
          : QuicSessionPeer::GetStreamIdManager(client_session)
                ->max_open_outgoing_streams();
  size_t client_max_open_outgoing_unidirectional_streams =
      version_.HasIetfQuicFrames()
          ? QuicSessionPeer::ietf_streamid_manager(client_session)
                    ->max_outgoing_unidirectional_streams() -
                kHttp3StaticUnidirectionalStreamCount
          : QuicSessionPeer::GetStreamIdManager(client_session)
                ->max_open_outgoing_streams();
  EXPECT_EQ(kServerMaxDynamicStreams,
            client_max_open_outgoing_bidirectional_streams);
  EXPECT_EQ(kServerMaxDynamicStreams,
            client_max_open_outgoing_unidirectional_streams);
  server_thread_->Pause();
  QuicSession* server_session = GetServerSession();
  if (server_session != nullptr) {
    size_t server_max_open_outgoing_bidirectional_streams =
        version_.HasIetfQuicFrames()
            ? QuicSessionPeer::ietf_streamid_manager(server_session)
                  ->max_outgoing_bidirectional_streams()
            : QuicSessionPeer::GetStreamIdManager(server_session)
                  ->max_open_outgoing_streams();
    size_t server_max_open_outgoing_unidirectional_streams =
        version_.HasIetfQuicFrames()
            ? QuicSessionPeer::ietf_streamid_manager(server_session)
                      ->max_outgoing_unidirectional_streams() -
                  kHttp3StaticUnidirectionalStreamCount
            : QuicSessionPeer::GetStreamIdManager(server_session)
                  ->max_open_outgoing_streams();
    EXPECT_EQ(kClientMaxDynamicStreams,
              server_max_open_outgoing_bidirectional_streams);
    EXPECT_EQ(kClientMaxDynamicStreams,
              server_max_open_outgoing_unidirectional_streams);
  } else {
    ADD_FAILURE() << "Missing server session";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, NegotiateCongestionControl) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();
  if (server_sent_packet_manager != nullptr) {
    EXPECT_EQ(
        expected_congestion_control_type,
        QuicSentPacketManagerPeer::GetSendAlgorithm(*server_sent_packet_manager)
            ->GetCongestionControlType());
  } else {
    ADD_FAILURE() << "Missing server sent packet manager";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, ClientSuggestsRTT) {
  // Client suggests initial RTT, verify it is used.
  const QuicTime::Delta kInitialRTT = QuicTime::Delta::FromMicroseconds(20000);
  client_config_.SetInitialRoundTripTimeUsToSend(kInitialRTT.ToMicroseconds());

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(server_thread_);
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  const QuicSentPacketManager* client_sent_packet_manager =
      GetSentPacketManagerFromClientSession();
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();
  if (client_sent_packet_manager != nullptr &&
      server_sent_packet_manager != nullptr) {
    EXPECT_EQ(kInitialRTT,
              client_sent_packet_manager->GetRttStats()->initial_rtt());
    EXPECT_EQ(kInitialRTT,
              server_sent_packet_manager->GetRttStats()->initial_rtt());
  } else {
    ADD_FAILURE() << "Missing sent packet manager";
  }
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(server_thread_);
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  const QuicSentPacketManager* client_sent_packet_manager =
      GetSentPacketManagerFromClientSession();
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();
  if (client_sent_packet_manager != nullptr &&
      server_sent_packet_manager != nullptr) {
    EXPECT_EQ(kInitialRTT,
              client_sent_packet_manager->GetRttStats()->initial_rtt());
    EXPECT_EQ(kInitialRTT,
              server_sent_packet_manager->GetRttStats()->initial_rtt());
  } else {
    ADD_FAILURE() << "Missing sent packet manager";
  }
  server_thread_->Resume();
}

// Regression test for b/171378845
TEST_P(EndToEndTest, ClientDisablesGQuicZeroRtt) {
  if (version_.UsesTls()) {
    // This feature is gQUIC only.
    ASSERT_TRUE(Initialize());
    return;
  }
  QuicTagVector options;
  options.push_back(kQNZ2);
  client_config_.SetClientConnectionOptions(options);

  ASSERT_TRUE(Initialize());

  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_session->ReceivedInchoateReject());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->ReceivedInchoateReject());

  client_->Disconnect();

  // Make sure that the request succeeds but 0-RTT was not used.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_FALSE(client_->client()->EarlyDataAccepted());
}

TEST_P(EndToEndTest, MaxInitialRTT) {
  // Client tries to suggest twice the server's max initial rtt and the server
  // uses the max.
  client_config_.SetInitialRoundTripTimeUsToSend(2 *
                                                 kMaxInitialRoundTripTimeUs);

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(server_thread_);
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  const QuicSentPacketManager* client_sent_packet_manager =
      GetSentPacketManagerFromClientSession();
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();
  if (client_sent_packet_manager != nullptr &&
      server_sent_packet_manager != nullptr) {
    // Now that acks have been exchanged, the RTT estimate has decreased on the
    // server and is not infinite on the client.
    EXPECT_FALSE(
        client_sent_packet_manager->GetRttStats()->smoothed_rtt().IsInfinite());
    const RttStats* server_rtt_stats =
        server_sent_packet_manager->GetRttStats();
    EXPECT_EQ(static_cast<int64_t>(kMaxInitialRoundTripTimeUs),
              server_rtt_stats->initial_rtt().ToMicroseconds());
    EXPECT_GE(static_cast<int64_t>(kMaxInitialRoundTripTimeUs),
              server_rtt_stats->smoothed_rtt().ToMicroseconds());
  } else {
    ADD_FAILURE() << "Missing sent packet manager";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, MinInitialRTT) {
  // Client tries to suggest 0 and the server uses the default.
  client_config_.SetInitialRoundTripTimeUsToSend(0);

  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();
  const QuicSentPacketManager* client_sent_packet_manager =
      GetSentPacketManagerFromClientSession();
  const QuicSentPacketManager* server_sent_packet_manager =
      GetSentPacketManagerFromFirstServerSession();
  if (client_sent_packet_manager != nullptr &&
      server_sent_packet_manager != nullptr) {
    // Now that acks have been exchanged, the RTT estimate has decreased on the
    // server and is not infinite on the client.
    EXPECT_FALSE(
        client_sent_packet_manager->GetRttStats()->smoothed_rtt().IsInfinite());
    // Expect the default rtt of 100ms.
    EXPECT_EQ(QuicTime::Delta::FromMilliseconds(100),
              server_sent_packet_manager->GetRttStats()->initial_rtt());
    // Ensure the bandwidth is valid.
    client_sent_packet_manager->BandwidthEstimate();
    server_sent_packet_manager->BandwidthEstimate();
  } else {
    ADD_FAILURE() << "Missing sent packet manager";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, 0ByteConnectionId) {
  if (version_.HasIetfInvariantHeader()) {
    // SetBytesForConnectionIdToSend only applies to Google QUIC encoding.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetBytesForConnectionIdToSend(0);
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicPacketHeader* header =
      QuicConnectionPeer::GetLastHeader(client_connection);
  EXPECT_EQ(CONNECTION_ID_ABSENT, header->source_connection_id_included);
}

TEST_P(EndToEndTest, 8ByteConnectionId) {
  if (version_.HasIetfInvariantHeader()) {
    // SetBytesForConnectionIdToSend only applies to Google QUIC encoding.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetBytesForConnectionIdToSend(8);
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicPacketHeader* header =
      QuicConnectionPeer::GetLastHeader(client_connection);
  EXPECT_EQ(CONNECTION_ID_PRESENT, header->destination_connection_id_included);
}

TEST_P(EndToEndTest, 15ByteConnectionId) {
  if (version_.HasIetfInvariantHeader()) {
    // SetBytesForConnectionIdToSend only applies to Google QUIC encoding.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetBytesForConnectionIdToSend(15);
  ASSERT_TRUE(Initialize());

  // Our server is permissive and allows for out of bounds values.
  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicPacketHeader* header =
      QuicConnectionPeer::GetLastHeader(client_connection);
  EXPECT_EQ(CONNECTION_ID_PRESENT, header->destination_connection_id_included);
}

TEST_P(EndToEndTest, ResetConnection) {
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  client_->ResetConnection();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  SendSynchronousBarRequestAndCheckResponse();
}

TEST_P(EndToEndTest, MaxStreamsUberTest) {
  // Connect with lower fake packet loss than we'd like to test.  Until
  // b/10126687 is fixed, losing handshake packets is pretty brutal.
  SetPacketLossPercentage(1);
  ASSERT_TRUE(Initialize());
  std::string large_body(10240, 'a');
  int max_streams = 100;

  AddToCache("/large_response", 200, large_body);

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  SetPacketLossPercentage(10);

  for (int i = 0; i < max_streams; ++i) {
    EXPECT_LT(0, client_->SendRequest("/large_response"));
  }

  // WaitForEvents waits 50ms and returns true if there are outstanding
  // requests.
  while (client_->client()->WaitForEvents()) {
    ASSERT_TRUE(client_->connected());
  }
}

TEST_P(EndToEndTest, StreamCancelErrorTest) {
  ASSERT_TRUE(Initialize());
  std::string small_body(256, 'a');

  AddToCache("/small_response", 200, small_body);

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  QuicSession* session = GetClientSession();
  ASSERT_TRUE(session);
  // Lose the request.
  SetPacketLossPercentage(100);
  EXPECT_LT(0, client_->SendRequest("/small_response"));
  client_->client()->WaitForEvents();
  // Transmit the cancel, and ensure the connection is torn down properly.
  SetPacketLossPercentage(0);
  QuicStreamId stream_id = GetNthClientInitiatedBidirectionalId(0);
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  const QuicPacketCount packets_sent_before =
      client_connection->GetStats().packets_sent;
  session->ResetStream(stream_id, QUIC_STREAM_CANCELLED);
  const QuicPacketCount packets_sent_now =
      client_connection->GetStats().packets_sent;

  if (version_.UsesHttp3()) {
    // Make sure 2 packets were sent, one for QPACK instructions, another for
    // RESET_STREAM and STOP_SENDING.
    EXPECT_EQ(packets_sent_before + 2, packets_sent_now);
  }

  // WaitForEvents waits 50ms and returns true if there are outstanding
  // requests.
  while (client_->client()->WaitForEvents()) {
    ASSERT_TRUE(client_->connected());
  }
  // It should be completely fine to RST a stream before any data has been
  // received for that stream.
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

TEST_P(EndToEndTest, ConnectionMigrationClientIPChanged) {
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();

  // Store the client IP address which was used to send the first request.
  QuicIpAddress old_host =
      client_->client()->network_helper()->GetLatestClientAddress().host();

  // Migrate socket to the new IP address.
  QuicIpAddress new_host = TestLoopback(2);
  EXPECT_NE(old_host, new_host);
  ASSERT_TRUE(client_->client()->MigrateSocket(new_host));

  // Send a request using the new socket.
  SendSynchronousBarRequestAndCheckResponse();

  if (!version_.HasIetfQuicFrames() ||
      !client_->client()->session()->connection()->validate_client_address()) {
    return;
  }
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(1u,
            client_connection->GetStats().num_connectivity_probing_received);

  // Send another request.
  SendSynchronousBarRequestAndCheckResponse();
  // By the time the 2nd request is completed, the PATH_RESPONSE must have been
  // received by the server.
  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    EXPECT_FALSE(server_connection->HasPendingPathValidation());
    EXPECT_EQ(1u, server_connection->GetStats().num_validated_peer_migration);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, ConnectionMigrationNewTokenForNewIp) {
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfQuicFrames() ||
      !client_->client()->session()->connection()->validate_client_address()) {
    return;
  }
  SendSynchronousFooRequestAndCheckResponse();

  // Store the client IP address which was used to send the first request.
  QuicIpAddress old_host =
      client_->client()->network_helper()->GetLatestClientAddress().host();

  // Migrate socket to the new IP address.
  QuicIpAddress new_host = TestLoopback(2);
  EXPECT_NE(old_host, new_host);
  ASSERT_TRUE(client_->client()->MigrateSocket(new_host));

  // Send a request using the new socket.
  SendSynchronousBarRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(1u,
            client_connection->GetStats().num_connectivity_probing_received);

  // Send another request to ensure that the server will time to finish the
  // reverse path validation and send address token.
  SendSynchronousBarRequestAndCheckResponse();

  client_->Disconnect();
  // The 0-RTT handshake should succeed.
  client_->Connect();
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  ASSERT_TRUE(client_->client()->connected());
  SendSynchronousFooRequestAndCheckResponse();

  EXPECT_TRUE(GetClientSession()->EarlyDataAccepted());
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    if (GetQuicReloadableFlag(quic_enable_token_based_address_validation)) {
      // Verify address is validated via validating token received in INITIAL
      // packet.
      EXPECT_FALSE(server_connection->GetStats()
                       .address_validated_via_decrypting_packet);
      EXPECT_TRUE(server_connection->GetStats().address_validated_via_token);
    } else {
      EXPECT_TRUE(server_connection->GetStats()
                      .address_validated_via_decrypting_packet);
      EXPECT_FALSE(server_connection->GetStats().address_validated_via_token);
    }
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
  client_->Disconnect();
}

// A writer which copies the packet and send the copy with a specified self
// address and then send the same packet with the original self address.
class DuplicatePacketWithSpoofedSelfAddressWriter
    : public QuicPacketWriterWrapper {
 public:
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override {
    if (self_address_to_overwrite_.IsInitialized()) {
      // Send the same packet on the overwriting address before sending on the
      // actual self address.
      QuicPacketWriterWrapper::WritePacket(
          buffer, buf_len, self_address_to_overwrite_, peer_address, options);
    }
    return QuicPacketWriterWrapper::WritePacket(buffer, buf_len, self_address,
                                                peer_address, options);
  }

  void set_self_address_to_overwrite(const QuicIpAddress& self_address) {
    self_address_to_overwrite_ = self_address;
  }

 private:
  QuicIpAddress self_address_to_overwrite_;
};

TEST_P(EndToEndTest, ClientAddressSpoofedForSomePeriod) {
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfQuicFrames() ||
      !client_->client()->session()->connection()->validate_client_address()) {
    return;
  }
  auto writer = new DuplicatePacketWithSpoofedSelfAddressWriter();
  client_.reset(CreateQuicClient(writer));
  QuicIpAddress real_host = TestLoopback(1);
  client_->MigrateSocket(real_host);
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(
      0u, GetClientConnection()->GetStats().num_connectivity_probing_received);
  EXPECT_EQ(
      real_host,
      client_->client()->network_helper()->GetLatestClientAddress().host());
  client_->WaitForDelayedAcks();

  std::string large_body(10240, 'a');
  AddToCache("/large_response", 200, large_body);

  QuicIpAddress spoofed_host = TestLoopback(2);
  writer->set_self_address_to_overwrite(spoofed_host);

  client_->SendRequest("/large_response");
  QuicConnection* client_connection = GetClientConnection();
  QuicPacketCount num_packets_received =
      client_connection->GetStats().packets_received;

  while (client_->client()->WaitForEvents() && client_->connected()) {
    if (client_connection->GetStats().packets_received > num_packets_received) {
      // Ideally the client won't receive any packets till the server finds out
      // the new client address is not working. But there are 2 corner cases:
      // 1) Before the server received the packet from spoofed address, it might
      // send packets to the real client address. So the client will immediately
      // switch back to use the original address;
      // 2) Between the server fails reverse path validation and the client
      // receives packets again, the client might sent some packets with the
      // spoofed address and triggers another migration.
      // In both corner cases, the attempted migration should fail and fall back
      // to the working path.
      writer->set_self_address_to_overwrite(QuicIpAddress());
    }
  }
  client_->WaitForResponse();
  EXPECT_EQ(large_body, client_->response_body());
}

TEST_P(EndToEndTest, AsynchronousConnectionMigrationClientIPChanged) {
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfQuicFrames() ||
      !client_->client()->session()->connection()->use_path_validator()) {
    return;
  }
  client_.reset(CreateQuicClient(nullptr));

  SendSynchronousFooRequestAndCheckResponse();

  // Store the client IP address which was used to send the first request.
  QuicIpAddress old_host =
      client_->client()->network_helper()->GetLatestClientAddress().host();

  // Migrate socket to the new IP address.
  QuicIpAddress new_host = TestLoopback(2);
  EXPECT_NE(old_host, new_host);
  ASSERT_TRUE(client_->client()->ValidateAndMigrateSocket(new_host));

  while (client_->client()->HasPendingPathValidation()) {
    client_->client()->WaitForEvents();
  }
  EXPECT_EQ(new_host, client_->client()->session()->self_address().host());
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(client_connection->validate_client_address() ? 1u : 0,
            client_connection->GetStats().num_connectivity_probing_received);
  // Send a request using the new socket.
  SendSynchronousBarRequestAndCheckResponse();
}

TEST_P(EndToEndTest, ConnectionMigrationClientPortChanged) {
  // Tests that the client's port can change during an established QUIC
  // connection, and that doing so does not result in the connection being
  // closed by the server.
  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();

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
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionPeer::SetSelfAddress(
      client_connection,
      QuicSocketAddress(client_connection->self_address().host(), new_port));

  // Register the new FD for epoll events.
  int new_fd = client_->client()->GetLatestFD();
  QuicEpollServer* eps = client_->epoll_server();
  eps->RegisterFD(new_fd, client_->client()->epoll_network_helper(),
                  EPOLLIN | EPOLLOUT | EPOLLET);

  // Send a second request, using the new FD.
  SendSynchronousBarRequestAndCheckResponse();

  // Verify that the client's ephemeral port is different.
  QuicSocketAddress new_address =
      client_->client()->network_helper()->GetLatestClientAddress();
  EXPECT_EQ(old_address.host(), new_address.host());
  EXPECT_NE(old_address.port(), new_address.port());

  if (!version_.HasIetfQuicFrames() ||
      !GetClientConnection()->validate_client_address()) {
    return;
  }

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    EXPECT_FALSE(server_connection->HasPendingPathValidation());
    EXPECT_EQ(1u, server_connection->GetStats().num_validated_peer_migration);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, NegotiatedInitialCongestionWindow) {
  SetQuicReloadableFlag(quic_unified_iw_options, true);
  client_extra_copts_.push_back(kIW03);

  ASSERT_TRUE(Initialize());

  // Values are exchanged during crypto handshake, so wait for that to finish.
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  server_thread_->WaitForCryptoHandshakeConfirmed();
  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    QuicPacketCount cwnd =
        server_connection->sent_packet_manager().initial_congestion_window();
    EXPECT_EQ(3u, cwnd);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Open a data stream to make sure the stream level flow control is updated.
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  WriteHeadersOnStream(stream);
  stream->WriteOrBufferBody("hello", false);

  if (!version_.UsesTls()) {
    // IFWA only exists with QUIC_CRYPTO.
    // Client should have the right values for server's receive window.
    ASSERT_TRUE(client_->client()
                    ->client_session()
                    ->config()
                    ->HasReceivedInitialStreamFlowControlWindowBytes());
    EXPECT_EQ(kServerStreamIFCW,
              client_->client()
                  ->client_session()
                  ->config()
                  ->ReceivedInitialStreamFlowControlWindowBytes());
    ASSERT_TRUE(client_->client()
                    ->client_session()
                    ->config()
                    ->HasReceivedInitialSessionFlowControlWindowBytes());
    EXPECT_EQ(kServerSessionIFCW,
              client_->client()
                  ->client_session()
                  ->config()
                  ->ReceivedInitialSessionFlowControlWindowBytes());
  }
  EXPECT_EQ(kServerStreamIFCW, QuicStreamPeer::SendWindowOffset(stream));
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_EQ(kServerSessionIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                    client_session->flow_controller()));

  // Server should have the right values for client's receive window.
  server_thread_->Pause();
  QuicSpdySession* server_session = GetServerSession();
  if (server_session == nullptr) {
    ADD_FAILURE() << "Missing server session";
    server_thread_->Resume();
    return;
  }
  QuicConfig server_config = *server_session->config();
  EXPECT_EQ(kClientSessionIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                    server_session->flow_controller()));
  server_thread_->Resume();
  if (version_.UsesTls()) {
    // IFWA only exists with QUIC_CRYPTO.
    return;
  }
  ASSERT_TRUE(server_config.HasReceivedInitialStreamFlowControlWindowBytes());
  EXPECT_EQ(kClientStreamIFCW,
            server_config.ReceivedInitialStreamFlowControlWindowBytes());
  ASSERT_TRUE(server_config.HasReceivedInitialSessionFlowControlWindowBytes());
  EXPECT_EQ(kClientSessionIFCW,
            server_config.ReceivedInitialSessionFlowControlWindowBytes());
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  // Open a data stream to make sure the stream level flow control is updated.
  QuicSpdyClientStream* stream = client_->GetOrCreateStream();
  WriteHeadersOnStream(stream);
  stream->WriteOrBufferBody("hello", false);

  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);

  if (!version_.UsesTls()) {
    // IFWA only exists with QUIC_CRYPTO.
    // Client should have the right values for server's receive window.
    ASSERT_TRUE(client_session->config()
                    ->HasReceivedInitialStreamFlowControlWindowBytes());
    EXPECT_EQ(kExpectedStreamIFCW,
              client_session->config()
                  ->ReceivedInitialStreamFlowControlWindowBytes());
    ASSERT_TRUE(client_session->config()
                    ->HasReceivedInitialSessionFlowControlWindowBytes());
    EXPECT_EQ(kExpectedSessionIFCW,
              client_session->config()
                  ->ReceivedInitialSessionFlowControlWindowBytes());
  }
  EXPECT_EQ(kExpectedStreamIFCW, QuicStreamPeer::SendWindowOffset(stream));
  EXPECT_EQ(kExpectedSessionIFCW, QuicFlowControllerPeer::SendWindowOffset(
                                      client_session->flow_controller()));
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicCryptoStream* crypto_stream =
      QuicSessionPeer::GetMutableCryptoStream(client_session);
  ASSERT_TRUE(crypto_stream);
  // In v47 and later, the crypto handshake (sent in CRYPTO frames) is not
  // subject to flow control.
  if (!version_.UsesCryptoFrames()) {
    EXPECT_LT(QuicStreamPeer::SendWindowSize(crypto_stream), kStreamIFCW);
  }
  // When stream type is enabled, control streams will send settings and
  // contribute to flow control windows, so this expectation is no longer valid.
  if (!version_.UsesHttp3()) {
    EXPECT_EQ(kSessionIFCW, QuicFlowControllerPeer::SendWindowSize(
                                client_session->flow_controller()));
  }

  // Send a request with no body, and verify that the connection level window
  // has not been affected.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  // No headers stream in IETF QUIC.
  if (version_.UsesHttp3()) {
    return;
  }

  QuicHeadersStream* headers_stream =
      QuicSpdySessionPeer::GetHeadersStream(client_session);
  ASSERT_TRUE(headers_stream);
  EXPECT_LT(QuicStreamPeer::SendWindowSize(headers_stream), kStreamIFCW);
  EXPECT_EQ(kSessionIFCW, QuicFlowControllerPeer::SendWindowSize(
                              client_session->flow_controller()));

  // Server should be in a similar state: connection flow control window should
  // not have any bytes marked as received.
  server_thread_->Pause();
  QuicSession* server_session = GetServerSession();
  if (server_session != nullptr) {
    QuicFlowController* server_connection_flow_controller =
        server_session->flow_controller();
    EXPECT_EQ(kSessionIFCW, QuicFlowControllerPeer::ReceiveWindowSize(
                                server_connection_flow_controller));
  } else {
    ADD_FAILURE() << "Missing server session";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, FlowControlsSynced) {
  set_smaller_flow_control_receive_window();

  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  server_thread_->WaitForCryptoHandshakeConfirmed();

  QuicSpdySession* const client_session = GetClientSession();
  ASSERT_TRUE(client_session);

  if (version_.UsesHttp3()) {
    // Make sure that the client has received the initial SETTINGS frame, which
    // is sent in the first packet on the control stream.
    while (!QuicSpdySessionPeer::GetReceiveControlStream(client_session)) {
      client_->client()->WaitForEvents();
      ASSERT_TRUE(client_->connected());
    }
  }

  // Make sure that all data sent by the client has been received by the server
  // (and the ack received by the client).
  while (client_session->HasUnackedStreamData()) {
    client_->client()->WaitForEvents();
    ASSERT_TRUE(client_->connected());
  }

  server_thread_->Pause();

  QuicSpdySession* const server_session = GetServerSession();
  if (server_session == nullptr) {
    ADD_FAILURE() << "Missing server session";
    server_thread_->Resume();
    return;
  }
  ExpectFlowControlsSynced(client_session, server_session);

  // Check control streams.
  if (version_.UsesHttp3()) {
    ExpectFlowControlsSynced(
        QuicSpdySessionPeer::GetReceiveControlStream(client_session),
        QuicSpdySessionPeer::GetSendControlStream(server_session));
    ExpectFlowControlsSynced(
        QuicSpdySessionPeer::GetSendControlStream(client_session),
        QuicSpdySessionPeer::GetReceiveControlStream(server_session));
  }

  // Check crypto stream.
  if (!version_.UsesCryptoFrames()) {
    ExpectFlowControlsSynced(
        QuicSessionPeer::GetMutableCryptoStream(client_session),
        QuicSessionPeer::GetMutableCryptoStream(server_session));
  }

  // Check headers stream.
  if (!version_.UsesHttp3()) {
    SpdyFramer spdy_framer(SpdyFramer::ENABLE_COMPRESSION);
    SpdySettingsIR settings_frame;
    settings_frame.AddSetting(spdy::SETTINGS_MAX_HEADER_LIST_SIZE,
                              kDefaultMaxUncompressedHeaderSize);
    SpdySerializedFrame frame(spdy_framer.SerializeFrame(settings_frame));

    QuicHeadersStream* client_header_stream =
        QuicSpdySessionPeer::GetHeadersStream(client_session);
    QuicHeadersStream* server_header_stream =
        QuicSpdySessionPeer::GetHeadersStream(server_session);
    // Both client and server are sending this SETTINGS frame, and the send
    // window is consumed. But because of timing issue, the server may send or
    // not send the frame, and the client may send/ not send / receive / not
    // receive the frame.
    // TODO(fayang): Rewrite this part because it is hacky.
    QuicByteCount win_difference1 =
        QuicStreamPeer::ReceiveWindowSize(server_header_stream) -
        QuicStreamPeer::SendWindowSize(client_header_stream);
    if (win_difference1 != 0) {
      EXPECT_EQ(frame.size(), win_difference1);
    }

    QuicByteCount win_difference2 =
        QuicStreamPeer::ReceiveWindowSize(client_header_stream) -
        QuicStreamPeer::SendWindowSize(server_header_stream);
    if (win_difference2 != 0) {
      EXPECT_EQ(frame.size(), win_difference2);
    }

    // Client *may* have received the SETTINGs frame.
    // TODO(fayang): Rewrite this part because it is hacky.
    float ratio1 = static_cast<float>(QuicFlowControllerPeer::ReceiveWindowSize(
                       client_session->flow_controller())) /
                   QuicStreamPeer::ReceiveWindowSize(
                       QuicSpdySessionPeer::GetHeadersStream(client_session));
    float ratio2 = static_cast<float>(QuicFlowControllerPeer::ReceiveWindowSize(
                       client_session->flow_controller())) /
                   (QuicStreamPeer::ReceiveWindowSize(
                        QuicSpdySessionPeer::GetHeadersStream(client_session)) +
                    frame.size());
    EXPECT_TRUE(ratio1 == kSessionToStreamRatio ||
                ratio2 == kSessionToStreamRatio);
  }

  server_thread_->Resume();
}

TEST_P(EndToEndTest, RequestWithNoBodyWillNeverSendStreamFrameWithFIN) {
  // A stream created on receipt of a simple request with no body will never get
  // a stream frame with a FIN. Verify that we don't keep track of the stream in
  // the locally closed streams map: it will never be removed if so.
  ASSERT_TRUE(Initialize());

  // Send a simple headers only request, and receive response.
  SendSynchronousFooRequestAndCheckResponse();

  // Now verify that the server is not waiting for a final FIN or RST.
  server_thread_->Pause();
  QuicSession* server_session = GetServerSession();
  if (server_session != nullptr) {
    EXPECT_EQ(0u, QuicSessionPeer::GetLocallyClosedStreamsHighestOffset(
                      server_session)
                      .size());
  } else {
    ADD_FAILURE() << "Missing server session";
  }
  server_thread_->Resume();
}

// TestAckListener counts how many bytes are acked during its lifetime.
class TestAckListener : public QuicAckListenerInterface {
 public:
  TestAckListener() {}

  void OnPacketAcked(int acked_bytes,
                     QuicTime::Delta /*delta_largest_observed*/) override {
    total_bytes_acked_ += acked_bytes;
  }

  void OnPacketRetransmitted(int /*retransmitted_bytes*/) override {}

  int total_bytes_acked() const { return total_bytes_acked_; }

 protected:
  // Object is ref counted.
  ~TestAckListener() override {}

 private:
  int total_bytes_acked_ = 0;
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
  // socket, an AckNotifierDelegate will get informed that the data it is
  // interested in has been ACKed. This tests end-to-end ACK notification, and
  // demonstrates that retransmissions do not break this functionality.
  // Disable blackhole detection as this test is testing loss recovery.
  client_extra_copts_.push_back(kNBHD);
  SetPacketLossPercentage(5);
  ASSERT_TRUE(Initialize());
  // Wait for the server SHLO before upping the packet loss.
  EXPECT_TRUE(client_->client()->WaitForHandshakeConfirmed());
  SetPacketLossPercentage(30);
  client_writer_->set_fake_blocked_socket_percentage(10);

  // Wait for SETTINGS frame from server that sets QPACK dynamic table capacity
  // to make sure request headers will be compressed using the dynamic table.
  if (version_.UsesHttp3()) {
    while (true) {
      // Waits for up to 50 ms.
      client_->client()->WaitForEvents();
      ASSERT_TRUE(client_->connected());
      QuicSpdyClientSession* client_session = GetClientSession();
      if (client_session == nullptr) {
        ADD_FAILURE() << "Missing client session";
        return;
      }
      QpackEncoder* qpack_encoder = client_session->qpack_encoder();
      if (qpack_encoder == nullptr) {
        ADD_FAILURE() << "Missing QPACK encoder";
        return;
      }
      QpackHeaderTable* header_table =
          QpackEncoderPeer::header_table(qpack_encoder);
      if (header_table == nullptr) {
        ADD_FAILURE() << "Missing header table";
        return;
      }
      if (QpackHeaderTablePeer::dynamic_table_capacity(header_table) > 0) {
        break;
      }
    }
  }

  // Create a POST request and send the headers only.
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  client_->SendMessage(headers, "", /*fin=*/false);

  // Size of headers on the request stream. This is zero if headers are sent on
  // the header stream.
  size_t header_size = 0;
  if (version_.UsesHttp3()) {
    // Determine size of headers after QPACK compression.
    NoopDecoderStreamErrorDelegate decoder_stream_error_delegate;
    NoopQpackStreamSenderDelegate encoder_stream_sender_delegate;
    QpackEncoder qpack_encoder(&decoder_stream_error_delegate);
    qpack_encoder.set_qpack_stream_sender_delegate(
        &encoder_stream_sender_delegate);

    qpack_encoder.SetMaximumDynamicTableCapacity(
        kDefaultQpackMaxDynamicTableCapacity);
    qpack_encoder.SetDynamicTableCapacity(kDefaultQpackMaxDynamicTableCapacity);
    qpack_encoder.SetMaximumBlockedStreams(kDefaultMaximumBlockedStreams);

    std::string encoded_headers = qpack_encoder.EncodeHeaderList(
        /* stream_id = */ 0, headers, nullptr);
    header_size = encoded_headers.size();
  }

  // Test the AckNotifier's ability to track multiple packets by making the
  // request body exceed the size of a single packet.
  std::string request_string = "a request body bigger than one packet" +
                               std::string(kMaxOutgoingPacketSize, '.');

  const int expected_bytes_acked = header_size + request_string.length();

  // The TestAckListener will cause a failure if not notified.
  QuicReferenceCountedPointer<TestAckListener> ack_listener(
      new TestAckListener());

  // Send the request, and register the delegate for ACKs.
  client_->SendData(request_string, true, ack_listener);
  WaitForFooResponseAndCheckIt();

  // Send another request to flush out any pending ACKs on the server.
  SendSynchronousBarRequestAndCheckResponse();

  // Make sure the delegate does get the notification it expects.
  while (ack_listener->total_bytes_acked() < expected_bytes_acked) {
    // Waits for up to 50 ms.
    client_->client()->WaitForEvents();
    ASSERT_TRUE(client_->connected());
  }
  EXPECT_EQ(ack_listener->total_bytes_acked(), expected_bytes_acked)
      << " header_size " << header_size << " request length "
      << request_string.length();
}

// Send a public reset from the server.
TEST_P(EndToEndTest, ServerSendPublicReset) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  QuicSpdySession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicConfig* config = client_session->config();
  ASSERT_TRUE(config);
  EXPECT_TRUE(config->HasReceivedStatelessResetToken());
  QuicUint128 stateless_reset_token = config->ReceivedStatelessResetToken();

  // Send the public reset.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionId connection_id = client_connection->connection_id();
  QuicPublicResetPacket header;
  header.connection_id = connection_id;
  QuicFramer framer(server_supported_versions_, QuicTime::Zero(),
                    Perspective::IS_SERVER, kQuicDefaultConnectionIdLength);
  std::unique_ptr<QuicEncryptedPacket> packet;
  if (version_.HasIetfInvariantHeader()) {
    packet = framer.BuildIetfStatelessResetPacket(connection_id,
                                                  stateless_reset_token);
  } else {
    packet = framer.BuildPublicResetPacket(header);
  }
  // We must pause the server's thread in order to call WritePacket without
  // race conditions.
  server_thread_->Pause();
  auto client_address = client_connection->self_address();
  server_writer_->WritePacket(packet->data(), packet->length(),
                              server_address_.host(), client_address, nullptr);
  server_thread_->Resume();

  // The request should fail.
  EXPECT_EQ("", client_->SendSynchronousRequest("/foo"));
  EXPECT_TRUE(client_->response_headers()->empty());
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_PUBLIC_RESET));
}

// Send a public reset from the server for a different connection ID.
// It should be ignored.
TEST_P(EndToEndTest, ServerSendPublicResetWithDifferentConnectionId) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  QuicSpdySession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicConfig* config = client_session->config();
  ASSERT_TRUE(config);
  EXPECT_TRUE(config->HasReceivedStatelessResetToken());
  QuicUint128 stateless_reset_token = config->ReceivedStatelessResetToken();
  // Send the public reset.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionId incorrect_connection_id = TestConnectionId(
      TestConnectionIdToUInt64(client_connection->connection_id()) + 1);
  QuicPublicResetPacket header;
  header.connection_id = incorrect_connection_id;
  QuicFramer framer(server_supported_versions_, QuicTime::Zero(),
                    Perspective::IS_SERVER, kQuicDefaultConnectionIdLength);
  std::unique_ptr<QuicEncryptedPacket> packet;
  NiceMock<MockQuicConnectionDebugVisitor> visitor;
  client_connection->set_debug_visitor(&visitor);
  if (version_.HasIetfInvariantHeader()) {
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
  auto client_address = client_connection->self_address();
  server_writer_->WritePacket(packet->data(), packet->length(),
                              server_address_.host(), client_address, nullptr);
  server_thread_->Resume();

  if (version_.HasIetfInvariantHeader()) {
    // The request should fail. IETF stateless reset does not include connection
    // ID.
    EXPECT_EQ("", client_->SendSynchronousRequest("/foo"));
    EXPECT_TRUE(client_->response_headers()->empty());
    EXPECT_THAT(client_->connection_error(), IsError(QUIC_PUBLIC_RESET));
  } else {
    // The connection should be unaffected.
    SendSynchronousFooRequestAndCheckResponse();
  }

  client_connection->set_debug_visitor(nullptr);
}

// Send a public reset from the client for a different connection ID.
// It should be ignored.
TEST_P(EndToEndTest, ClientSendPublicResetWithDifferentConnectionId) {
  ASSERT_TRUE(Initialize());

  // Send the public reset.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionId incorrect_connection_id = TestConnectionId(
      TestConnectionIdToUInt64(client_connection->connection_id()) + 1);
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
  SendSynchronousFooRequestAndCheckResponse();
}

// Send a version negotiation packet from the server for a different
// connection ID.  It should be ignored.
TEST_P(EndToEndTest, ServerSendVersionNegotiationWithDifferentConnectionId) {
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  // Send the version negotiation packet.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionId incorrect_connection_id = TestConnectionId(
      TestConnectionIdToUInt64(client_connection->connection_id()) + 1);
  std::unique_ptr<QuicEncryptedPacket> packet(
      QuicFramer::BuildVersionNegotiationPacket(
          incorrect_connection_id, EmptyQuicConnectionId(),
          version_.HasIetfInvariantHeader(),
          version_.HasLengthPrefixedConnectionIds(),
          server_supported_versions_));
  NiceMock<MockQuicConnectionDebugVisitor> visitor;
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
  SendSynchronousFooRequestAndCheckResponse();

  client_connection->set_debug_visitor(nullptr);
}

// A bad header shouldn't tear down the connection, because the receiver can't
// tell the connection ID.
TEST_P(EndToEndTest, BadPacketHeaderTruncated) {
  ASSERT_TRUE(Initialize());

  // Start the connection.
  SendSynchronousFooRequestAndCheckResponse();

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
  SendSynchronousFooRequestAndCheckResponse();
}

// A bad header shouldn't tear down the connection, because the receiver can't
// tell the connection ID.
TEST_P(EndToEndTest, BadPacketHeaderFlags) {
  ASSERT_TRUE(Initialize());

  // Start the connection.
  SendSynchronousFooRequestAndCheckResponse();

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
  SendSynchronousFooRequestAndCheckResponse();
}

// Send a packet from the client with bad encrypted data.  The server should not
// tear down the connection.
TEST_P(EndToEndTest, BadEncryptedData) {
  ASSERT_TRUE(Initialize());

  // Start the connection.
  SendSynchronousFooRequestAndCheckResponse();

  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  std::unique_ptr<QuicEncryptedPacket> packet(ConstructEncryptedPacket(
      client_connection->connection_id(), EmptyQuicConnectionId(), false, false,
      1, "At least 20 characters.", CONNECTION_ID_PRESENT, CONNECTION_ID_ABSENT,
      PACKET_4BYTE_PACKET_NUMBER));
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
  if (dispatcher != nullptr) {
    EXPECT_THAT(QuicDispatcherPeer::GetAndClearLastError(dispatcher),
                IsQuicNoError());
  } else {
    ADD_FAILURE() << "Missing dispatcher";
  }
  server_thread_->Resume();

  // The connection should not be terminated.
  SendSynchronousFooRequestAndCheckResponse();
}

TEST_P(EndToEndTest, CanceledStreamDoesNotBecomeZombie) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
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
  ASSERT_TRUE(session);
  // Verify canceled stream does not become zombie.
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

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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
  CheckResponseHeaders("500");

  // Pause the server so we can access the server's internals without races.
  server_thread_->Pause();

  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  QuicSession* server_session =
      QuicDispatcherPeer::GetFirstSessionIfAny(dispatcher);
  EXPECT_TRUE(server_session != nullptr);

  // The stream is not waiting for the arrival of the peer's final offset.
  EXPECT_EQ(
      0u, QuicSessionPeer::GetLocallyClosedStreamsHighestOffset(server_session)
              .size());

  server_thread_->Resume();
}

TEST_P(EndToEndTest, Trailers) {
  // Test sending and receiving HTTP/2 Trailers (trailing HEADERS frames).
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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

  SendSynchronousRequestAndCheckResponse("/trailer_url", kBody);
  EXPECT_EQ(trailers, client_->response_trailers());
}

// TODO(b/151749109): Test server push for IETF QUIC.
class EndToEndTestServerPush : public EndToEndTest {
 protected:
  const size_t kNumMaxStreams = 10;

  EndToEndTestServerPush() : EndToEndTest() {
    SetQuicFlag(FLAGS_quic_enable_http3_server_push, true);
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
              : absl::StrCat("This is server push response body for ", url);
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
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(EndToEndTestServerPush, ServerPush) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

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
  QuicStreamSequencer* sequencer = nullptr;
  if (!version_.UsesHttp3()) {
    QuicSpdyClientSession* client_session = GetClientSession();
    ASSERT_TRUE(client_session);
    QuicHeadersStream* headers_stream =
        QuicSpdySessionPeer::GetHeadersStream(client_session);
    ASSERT_TRUE(headers_stream);
    sequencer = QuicStreamPeer::sequencer(headers_stream);
    ASSERT_TRUE(sequencer);
    // Headers stream's sequencer buffer shouldn't be released because server
    // push hasn't finished yet.
    EXPECT_TRUE(
        QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
  }

  for (const std::string& url : push_urls) {
    QUIC_DVLOG(1) << "send request for pushed stream on url " << url;
    std::string expected_body =
        absl::StrCat("This is server push response body for ", url);
    std::string response_body = client_->SendSynchronousRequest(url);
    QUIC_DVLOG(1) << "response body " << response_body;
    EXPECT_EQ(expected_body, response_body);
  }
  if (!version_.UsesHttp3()) {
    ASSERT_TRUE(sequencer);
    EXPECT_FALSE(
        QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
  }
}

TEST_P(EndToEndTestServerPush, ServerPushUnderLimit) {
  // Tests that sending a request which has 4 push resources will trigger server
  // to push those 4 resources and client can handle pushed resources and match
  // them with requests later.
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  if (version_.UsesHttp3()) {
    static_cast<QuicSpdySession*>(client_->client()->session())
        ->SetMaxPushId(kMaxQuicStreamId);
  }

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
        absl::StrCat("This is server push response body for ", url);
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
  if (version_.UsesHttp3()) {
    // TODO(b/142504641): Re-enable this test when we support push streams
    // arriving before the corresponding promises.
    ASSERT_TRUE(Initialize());
    return;
  }
  // Tests that when streams are not blocked by flow control or congestion
  // control, pushing even more resources than max number of open outgoing
  // streams should still work because all response streams get closed
  // immediately after pushing resources.
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  if (version_.UsesHttp3()) {
    static_cast<QuicSpdySession*>(client_->client()->session())
        ->SetMaxPushId(kMaxQuicStreamId);
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
    push_urls[i] = absl::StrCat("https://example.com/push_resources", i);
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
    EXPECT_EQ(absl::StrCat("This is server push response body for ", url),
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  if (version_.UsesHttp3()) {
    static_cast<QuicSpdySession*>(client_->client()->session())
        ->SetMaxPushId(kMaxQuicStreamId);
  }

  // Set reordering to ensure that body arriving before PUSH_PROMISE is ok.
  SetPacketSendDelay(QuicTime::Delta::FromMilliseconds(2));
  SetReorderPercentage(30);

  // Add a response with headers, body, and push resources.
  const std::string kBody = "body content";

  const size_t kNumResources = kNumMaxStreams + 1;
  std::string push_urls[11];
  for (size_t i = 0; i < kNumResources; ++i) {
    push_urls[i] = absl::StrCat("http://example.com/push_resources", i);
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
    ASSERT_TRUE(client_->connected());
  }

  // Check server session to see if it has max number of outgoing streams opened
  // though more resources need to be pushed.
  if (!version_.HasIetfQuicFrames()) {
    server_thread_->Pause();
    QuicSession* server_session = GetServerSession();
    if (server_session != nullptr) {
      EXPECT_EQ(kNumMaxStreams,
                QuicSessionPeer::GetStreamIdManager(server_session)
                    ->num_open_outgoing_streams());
    } else {
      ADD_FAILURE() << "Missing server session";
    }
    server_thread_->Resume();
  }

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

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
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
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  client_writer_->Initialize(
      QuicConnectionPeer::GetHelper(client_connection),
      QuicConnectionPeer::GetAlarmFactory(client_connection),
      std::make_unique<ClientDelegate>(client_->client()));
  initialized_ = true;
  ASSERT_TRUE(client_->client()->connected());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  SetPacketLossPercentage(1);
  client_->SendRequest("/huge_response");
  client_->WaitForResponse();
  VerifyCleanConnection(true);
}

// Regression test for b/111515567
TEST_P(EndToEndTest, AgreeOnStopWaiting) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    // Verify client and server connections agree on the value of
    // no_stop_waiting_frames.
    EXPECT_EQ(QuicConnectionPeer::GetNoStopWaitingFrames(client_connection),
              QuicConnectionPeer::GetNoStopWaitingFrames(server_connection));
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

// Regression test for b/111515567
TEST_P(EndToEndTest, AgreeOnStopWaitingWithNoStopWaitingOption) {
  QuicTagVector options;
  options.push_back(kNSTP);
  client_config_.SetConnectionOptionsToSend(options);
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    // Verify client and server connections agree on the value of
    // no_stop_waiting_frames.
    EXPECT_EQ(QuicConnectionPeer::GetNoStopWaitingFrames(client_connection),
              QuicConnectionPeer::GetNoStopWaitingFrames(server_connection));
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, ReleaseHeadersStreamBufferWhenIdle) {
  // Tests that when client side has no active request and no waiting
  // PUSH_PROMISE, its headers stream's sequencer buffer should be released.
  ASSERT_TRUE(Initialize());
  client_->SendSynchronousRequest("/foo");
  if (version_.UsesHttp3()) {
    return;
  }
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicHeadersStream* headers_stream =
      QuicSpdySessionPeer::GetHeadersStream(client_session);
  ASSERT_TRUE(headers_stream);
  QuicStreamSequencer* sequencer = QuicStreamPeer::sequencer(headers_stream);
  ASSERT_TRUE(sequencer);
  EXPECT_FALSE(QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(sequencer));
}

// A single large header value causes a different error than the total size of
// headers exceeding a smaller limit, tested at EndToEndTest.LargeHeaders.
TEST_P(EndToEndTest, WayTooLongRequestHeaders) {
  ASSERT_TRUE(Initialize());

  SpdyHeaderBlock headers;
  headers[":method"] = "GET";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;
  headers["key"] = std::string(2 * 1024 * 1024, 'a');

  client_->SendMessage(headers, "");
  client_->WaitForResponse();
  if (version_.UsesHttp3()) {
    EXPECT_THAT(client_->connection_error(),
                IsError(QUIC_QPACK_DECOMPRESSION_FAILED));
  } else {
    EXPECT_THAT(client_->connection_error(),
                IsError(QUIC_HPACK_VALUE_TOO_LONG));
  }
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

  void OnPingFrame(const QuicPingFrame& /*frame*/,
                   const QuicTime::Delta /*ping_received_delay*/) override {
    ++num_ping_frames_;
  }

 private:
  size_t num_window_update_frames_;
  size_t num_ping_frames_;
};

TEST_P(EndToEndTest, WindowUpdateInAck) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  WindowUpdateObserver observer;
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
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
  client_connection->set_debug_visitor(nullptr);
}

TEST_P(EndToEndTest, SendStatelessResetTokenInShlo) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicConfig* config = client_session->config();
  ASSERT_TRUE(config);
  EXPECT_TRUE(config->HasReceivedStatelessResetToken());
  QuicConnection* client_connection = client_session->connection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(QuicUtils::GenerateStatelessResetToken(
                client_connection->connection_id()),
            config->ReceivedStatelessResetToken());
  client_->Disconnect();
}

// Regression test for b/116200989.
TEST_P(EndToEndTest,
       SendStatelessResetIfServerConnectionClosedLocallyDuringHandshake) {
  connect_to_server_on_initialize_ = false;
  ASSERT_TRUE(Initialize());

  ASSERT_TRUE(server_thread_);
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  if (dispatcher == nullptr) {
    ADD_FAILURE() << "Missing dispatcher";
    server_thread_->Resume();
    return;
  }
  if (dispatcher->NumSessions() > 0) {
    ADD_FAILURE() << "Dispatcher session map not empty";
    server_thread_->Resume();
    return;
  }
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

  ASSERT_TRUE(server_thread_);
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  if (dispatcher == nullptr) {
    ADD_FAILURE() << "Missing dispatcher";
    server_thread_->Resume();
    return;
  }
  if (dispatcher->NumSessions() > 0) {
    ADD_FAILURE() << "Dispatcher session map not empty";
    server_thread_->Resume();
    return;
  }
  QuicDispatcherPeer::UseWriter(
      dispatcher,
      // This will cause an server write error with EPERM, while sending the
      // response for /big_response.
      new BadPacketWriter(/*packet_causing_write_error=*/20, EPERM));
  server_thread_->Resume();

  client_.reset(CreateQuicClient(client_writer_));

  // First, a /foo request with small response should succeed.
  SendSynchronousFooRequestAndCheckResponse();

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

  SendSynchronousFooRequestAndCheckResponse();

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

  if (version_.UsesTls()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    bool ok;
    EXPECT_QUIC_BUG(ok = Initialize(),
                    "QUIC client pre-shared keys not yet supported with TLS");
    EXPECT_FALSE(ok);
    return;
  }

  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
}

// TODO: reenable once we have a way to make this run faster.
TEST_P(EndToEndTest, QUIC_TEST_DISABLED_IN_CHROME(PreSharedKeyMismatch)) {
  client_config_.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  client_config_.set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  pre_shared_key_client_ = "foo";
  pre_shared_key_server_ = "bar";

  if (version_.UsesTls()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    bool ok;
    EXPECT_QUIC_BUG(ok = Initialize(),
                    "QUIC client pre-shared keys not yet supported with TLS");
    EXPECT_FALSE(ok);
    return;
  }

  // One of two things happens when Initialize() returns:
  // 1. Crypto handshake has completed, and it is unsuccessful. Initialize()
  //    returns false.
  // 2. Crypto handshake has not completed, Initialize() returns true. The call
  //    to WaitForCryptoHandshakeConfirmed() will wait for the handshake and
  //    return whether it is successful.
  ASSERT_FALSE(Initialize() && client_->client()->WaitForOneRttKeysAvailable());
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_HANDSHAKE_TIMEOUT));
}

// TODO: reenable once we have a way to make this run faster.
TEST_P(EndToEndTest, QUIC_TEST_DISABLED_IN_CHROME(PreSharedKeyNoClient)) {
  client_config_.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  client_config_.set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  pre_shared_key_server_ = "foobar";

  if (version_.UsesTls()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    bool ok;
    EXPECT_QUIC_BUG(ok = Initialize(),
                    "QUIC server pre-shared keys not yet supported with TLS");
    EXPECT_FALSE(ok);
    return;
  }

  ASSERT_FALSE(Initialize() && client_->client()->WaitForOneRttKeysAvailable());
  EXPECT_THAT(client_->connection_error(), IsError(QUIC_HANDSHAKE_TIMEOUT));
}

// TODO: reenable once we have a way to make this run faster.
TEST_P(EndToEndTest, QUIC_TEST_DISABLED_IN_CHROME(PreSharedKeyNoServer)) {
  client_config_.set_max_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  client_config_.set_max_idle_time_before_crypto_handshake(
      QuicTime::Delta::FromSeconds(1));
  pre_shared_key_client_ = "foobar";

  if (version_.UsesTls()) {
    // TODO(b/154162689) add PSK support to QUIC+TLS.
    bool ok;
    EXPECT_QUIC_BUG(ok = Initialize(),
                    "QUIC client pre-shared keys not yet supported with TLS");
    EXPECT_FALSE(ok);
    return;
  }

  ASSERT_FALSE(Initialize() && client_->client()->WaitForOneRttKeysAvailable());
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

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  client_->WaitForDelayedAcks();

  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  const QuicPacketCount packets_sent_before =
      client_connection->GetStats().packets_sent;

  client_->SendRequestAndRstTogether("/test_url");

  // Expect exactly one packet is sent from the block above.
  ASSERT_EQ(packets_sent_before + 1,
            client_connection->GetStats().packets_sent);

  // Wait for the connection to become idle.
  client_->WaitForDelayedAcks();

  // The real expectation is the test does not crash or timeout.
  EXPECT_THAT(client_->connection_error(), IsQuicNoError());
}

TEST_P(EndToEndTest, ResetStreamOnTtlExpires) {
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForHandshakeConfirmed());
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
  if (!version_.SupportsMessageFrames()) {
    Initialize();
    return;
  }
  ASSERT_TRUE(Initialize());
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  QuicSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicConnection* client_connection = client_session->connection();
  ASSERT_TRUE(client_connection);

  SetPacketLossPercentage(30);
  ASSERT_GT(kMaxOutgoingPacketSize,
            client_session->GetCurrentLargestMessagePayload());
  ASSERT_LT(0, client_session->GetCurrentLargestMessagePayload());

  std::string message_string(kMaxOutgoingPacketSize, 'a');
  absl::string_view message_buffer(message_string);
  QuicRandom* random =
      QuicConnectionPeer::GetHelper(client_connection)->GetRandomGenerator();
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);
  {
    QuicConnection::ScopedPacketFlusher flusher(client_session->connection());
    // Verify the largest message gets successfully sent.
    EXPECT_EQ(MessageResult(MESSAGE_STATUS_SUCCESS, 1),
              client_session->SendMessage(MakeSpan(
                  client_connection->helper()->GetStreamSendBufferAllocator(),
                  absl::string_view(
                      message_buffer.data(),
                      client_session->GetCurrentLargestMessagePayload()),
                  &storage)));
    // Send more messages with size (0, largest_payload] until connection is
    // write blocked.
    const int kTestMaxNumberOfMessages = 100;
    for (size_t i = 2; i <= kTestMaxNumberOfMessages; ++i) {
      size_t message_length =
          random->RandUint64() %
              client_session->GetGuaranteedLargestMessagePayload() +
          1;
      MessageResult result = client_session->SendMessage(MakeSpan(
          client_connection->helper()->GetStreamSendBufferAllocator(),
          absl::string_view(message_buffer.data(), message_length), &storage));
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
                    client_connection->helper()->GetStreamSendBufferAllocator(),
                    absl::string_view(
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
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(EndToEndPacketReorderingTest, ReorderedConnectivityProbing) {
  ASSERT_TRUE(Initialize());
  if (version_.HasIetfQuicFrames()) {
    return;
  }

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
  if (server_connection != nullptr) {
    EXPECT_EQ(1u,
              server_connection->GetStats().num_connectivity_probing_received);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();

  // Server definitely responded to the connectivity probing. Sometime it also
  // sends a padded ping that is not a connectivity probing, which is recognized
  // as connectivity probing because client's self address is ANY.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_LE(1u,
            client_connection->GetStats().num_connectivity_probing_received);
}

// A writer which holds the next packet to be sent till ReleasePacket() is
// called.
class PacketHoldingWriter : public QuicPacketWriterWrapper {
 public:
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override {
    if (!hold_next_packet_) {
      return QuicPacketWriterWrapper::WritePacket(buffer, buf_len, self_address,
                                                  peer_address, options);
    }
    QUIC_DLOG(INFO) << "Packet is held by the writer";
    packet_content_ = std::string(buffer, buf_len);
    self_address_ = self_address;
    peer_address_ = peer_address;
    options_ = (options == nullptr ? nullptr : options->Clone());
    hold_next_packet_ = false;
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

  void HoldNextPacket() {
    QUICHE_DCHECK(packet_content_.empty())
        << "There is already one packet on hold.";
    hold_next_packet_ = true;
  }

  void ReleasePacket() {
    QUIC_DLOG(INFO) << "Release packet";
    ASSERT_EQ(WRITE_STATUS_OK,
              QuicPacketWriterWrapper::WritePacket(
                  packet_content_.data(), packet_content_.length(),
                  self_address_, peer_address_, options_.release())
                  .status);
    packet_content_.clear();
  }

 private:
  bool hold_next_packet_{false};
  std::string packet_content_;
  QuicIpAddress self_address_;
  QuicSocketAddress peer_address_;
  std::unique_ptr<PerPacketOptions> options_;
};

TEST_P(EndToEndTest, ClientValidateNewNetwork) {
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfQuicFrames() ||
      !GetClientConnection()->validate_client_address()) {
    return;
  }
  client_.reset(EndToEndTest::CreateQuicClient(nullptr));
  SendSynchronousFooRequestAndCheckResponse();

  // Store the client IP address which was used to send the first request.
  QuicIpAddress old_host =
      client_->client()->network_helper()->GetLatestClientAddress().host();

  // Migrate socket to the new IP address.
  QuicIpAddress new_host = TestLoopback(2);
  EXPECT_NE(old_host, new_host);

  client_->client()->ValidateNewNetwork(new_host);
  // Send a request using the old socket.
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));
  // Client should have received a PATH_CHALLENGE.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(1u,
            client_connection->GetStats().num_connectivity_probing_received);

  // Send another request to make sure THE server will receive PATH_RESPONSE.
  client_->SendSynchronousRequest("/eep");

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    EXPECT_EQ(1u,
              server_connection->GetStats().num_connectivity_probing_received);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndPacketReorderingTest, ReorderedPathChallenge) {
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfQuicFrames() ||
      !client_->client()->session()->connection()->use_path_validator()) {
    return;
  }
  client_.reset(EndToEndTest::CreateQuicClient(nullptr));

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

  // Setup writer wrapper to hold the probing packet.
  auto holding_writer = new PacketHoldingWriter();
  client_->UseWriter(holding_writer);
  // Write a connectivity probing after the next /foo request.
  holding_writer->HoldNextPacket();

  // A packet with PATH_CHALLENGE will be held in the writer.
  client_->client()->ValidateNewNetwork(new_host);

  // Send (on-hold) PATH_CHALLENGE after this request.
  client_->SendRequest("/foo");
  holding_writer->ReleasePacket();

  client_->WaitForResponse();

  EXPECT_EQ(kFooResponseBody, client_->response_body());
  // Send yet another request after the PATH_CHALLENGE, when this request
  // returns, the probing is guaranteed to have been received by the server, and
  // the server's response to probing is guaranteed to have been received by the
  // client.
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));

  // Client should have received a PATH_CHALLENGE.
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(client_connection->validate_client_address() ? 1u : 0,
            client_connection->GetStats().num_connectivity_probing_received);

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    EXPECT_EQ(1u,
              server_connection->GetStats().num_connectivity_probing_received);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndPacketReorderingTest, PathValidationFailure) {
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfQuicFrames() ||
      !client_->client()->session()->connection()->use_path_validator()) {
    return;
  }

  client_.reset(CreateQuicClient(nullptr));
  // Finish one request to make sure handshake established.
  EXPECT_EQ(kFooResponseBody, client_->SendSynchronousRequest("/foo"));

  // Wait for the connection to become idle, to make sure the packet gets
  // delayed is the connectivity probing packet.
  client_->WaitForDelayedAcks();

  QuicSocketAddress old_addr = client_->client()->session()->self_address();

  // Migrate socket to the new IP address.
  QuicIpAddress new_host = TestLoopback(2);
  EXPECT_NE(old_addr.host(), new_host);

  // Drop PATH_RESPONSE packets to timeout the path validation.
  server_writer_->set_fake_packet_loss_percentage(100);
  ASSERT_TRUE(client_->client()->ValidateAndMigrateSocket(new_host));
  while (client_->client()->HasPendingPathValidation()) {
    client_->client()->WaitForEvents();
  }
  EXPECT_EQ(old_addr, client_->client()->session()->self_address());
  server_writer_->set_fake_packet_loss_percentage(0);
  EXPECT_EQ(kBarResponseBody, client_->SendSynchronousRequest("/bar"));

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection != nullptr) {
    EXPECT_EQ(3u,
              server_connection->GetStats().num_connectivity_probing_received);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
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
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
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
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  QuicConnectionStats client_stats = client_connection->GetStats();
  // Client sends CHLO in packet 1 and retransmitted in packet 2. Because of
  // the delay, server processes packet 2 and later drops packet 1. ACK is
  // bundled with SHLO, such that 1 can be detected loss by time threshold.
  EXPECT_LE(0u, client_stats.packets_lost);
  EXPECT_TRUE(client_->client()->EarlyDataAccepted());
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
  BadShloPacketWriter(ParsedQuicVersion version)
      : error_returned_(false), version_(version) {}
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
        TypeByteIsServerHello(type_byte)) {
      QUIC_DVLOG(1) << "Return write error for packet containing ServerHello";
      error_returned_ = true;
      return WriteResult(WRITE_STATUS_ERROR, QUIC_EMSGSIZE);
    }
    return result;
  }

  bool TypeByteIsServerHello(uint8_t type_byte) {
    if (version_.UsesQuicCrypto()) {
      // ENCRYPTION_ZERO_RTT packet.
      return ((type_byte & 0x30) >> 4) == 1;
    }
    // ENCRYPTION_HANDSHAKE packet.
    return ((type_byte & 0x30) >> 4) == 2;
  }

 private:
  bool error_returned_;
  ParsedQuicVersion version_;
};

TEST_P(EndToEndTest, ConnectionCloseBeforeHandshakeComplete) {
  if (!version_.HasIetfInvariantHeader()) {
    // Only runs for IETF QUIC header.
    Initialize();
    return;
  }
  // This test ensures ZERO_RTT_PROTECTED connection close could close a client
  // which has switched to forward secure.
  connect_to_server_on_initialize_ = false;
  ASSERT_TRUE(Initialize());
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  if (dispatcher == nullptr) {
    ADD_FAILURE() << "Missing dispatcher";
    server_thread_->Resume();
    return;
  }
  if (dispatcher->NumSessions() > 0) {
    ADD_FAILURE() << "Dispatcher session map not empty";
    server_thread_->Resume();
    return;
  }
  // Note: this writer will only used by the server connection, not the time
  // wait list.
  QuicDispatcherPeer::UseWriter(
      dispatcher,
      // This causes the first server sent ZERO_RTT_PROTECTED packet (i.e.,
      // SHLO) to be sent, but WRITE_ERROR is returned. Such that a
      // ZERO_RTT_PROTECTED connection close would be sent to a client with
      // encryption level FORWARD_SECURE.
      new BadShloPacketWriter(version_));
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
  connect_to_server_on_initialize_ = !version_.HasIetfInvariantHeader();
  ASSERT_TRUE(Initialize());
  if (!version_.HasIetfInvariantHeader()) {
    // Only runs for IETF QUIC header.
    return;
  }
  server_thread_->Pause();
  QuicDispatcher* dispatcher =
      QuicServerPeer::GetDispatcher(server_thread_->server());
  if (dispatcher == nullptr) {
    ADD_FAILURE() << "Missing dispatcher";
    server_thread_->Resume();
    return;
  }
  if (dispatcher->NumSessions() > 0) {
    ADD_FAILURE() << "Dispatcher session map not empty";
    server_thread_->Resume();
    return;
  }
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
  if (!version_.HasIetfQuicFrames()) {
    // Only runs for IETF QUIC.
    return;
  }
  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  std::string body(kMaxOutgoingPacketSize, 'a');
  SpdyHeaderBlock headers;
  headers[":method"] = "POST";
  headers[":path"] = "/foo";
  headers[":scheme"] = "https";
  headers[":authority"] = server_hostname_;

  // Force the client to write with a stream ID that exceeds the limit.
  QuicSpdySession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  QuicStreamIdManager* stream_id_manager =
      QuicSessionPeer::ietf_bidirectional_stream_id_manager(client_session);
  ASSERT_TRUE(stream_id_manager);
  QuicStreamCount max_number_of_streams =
      stream_id_manager->outgoing_max_streams();
  QuicSessionPeer::SetNextOutgoingBidirectionalStreamId(
      client_session,
      GetNthClientInitiatedBidirectionalId(max_number_of_streams + 1));
  client_->SendCustomSynchronousRequest(headers, body);
  EXPECT_THAT(client_->stream_error(),
              IsStreamError(QUIC_STREAM_CONNECTION_ERROR));
  EXPECT_THAT(client_session->error(), IsError(QUIC_INVALID_STREAM_ID));
  EXPECT_EQ(IETF_QUIC_TRANSPORT_CONNECTION_CLOSE, client_session->close_type());
  EXPECT_TRUE(
      IS_IETF_STREAM_FRAME(client_session->transport_close_frame_type()));
}

TEST_P(EndToEndTest, TestMaxPushId) {
  if (!version_.HasIetfQuicFrames()) {
    // MaxPushId is only implemented for IETF QUIC.
    Initialize();
    return;
  }
  SetQuicFlag(FLAGS_quic_enable_http3_server_push, true);
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  client_session->SetMaxPushId(kMaxQuicStreamId);

  client_->SendSynchronousRequest("/foo");

  EXPECT_TRUE(client_session->CanCreatePushStreamWithId(kMaxQuicStreamId));

  server_thread_->Pause();
  QuicSpdySession* server_session = GetServerSession();
  if (server_session != nullptr) {
    EXPECT_TRUE(server_session->CanCreatePushStreamWithId(kMaxQuicStreamId));
  } else {
    ADD_FAILURE() << "Missing server session";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, CustomTransportParameters) {
  if (!version_.UsesTls()) {
    // Custom transport parameters are only supported with TLS.
    ASSERT_TRUE(Initialize());
    return;
  }
  constexpr auto kCustomParameter =
      static_cast<TransportParameters::TransportParameterId>(0xff34);
  client_config_.custom_transport_parameters_to_send()[kCustomParameter] =
      "test";
  NiceMock<MockQuicConnectionDebugVisitor> visitor;
  connection_debug_visitor_ = &visitor;
  EXPECT_CALL(visitor, OnTransportParametersSent(_))
      .WillOnce(Invoke([kCustomParameter](
                           const TransportParameters& transport_parameters) {
        ASSERT_NE(transport_parameters.custom_parameters.find(kCustomParameter),
                  transport_parameters.custom_parameters.end());
        EXPECT_EQ(transport_parameters.custom_parameters.at(kCustomParameter),
                  "test");
      }));
  EXPECT_CALL(visitor, OnTransportParametersReceived(_)).Times(1);
  ASSERT_TRUE(Initialize());

  EXPECT_TRUE(client_->client()->WaitForOneRttKeysAvailable());

  server_thread_->Pause();
  QuicSpdySession* server_session = GetServerSession();
  QuicConfig* server_config = nullptr;
  if (server_session != nullptr) {
    server_config = server_session->config();
    EXPECT_EQ(server_session->user_agent_id().value_or("MissingUserAgent"),
              kTestUserAgentId);
  } else {
    ADD_FAILURE() << "Missing server session";
  }
  if (server_config != nullptr) {
    if (server_config->received_custom_transport_parameters().find(
            kCustomParameter) !=
        server_config->received_custom_transport_parameters().end()) {
      EXPECT_EQ(server_config->received_custom_transport_parameters().at(
                    kCustomParameter),
                "test");
    } else {
      ADD_FAILURE() << "Did not find custom parameter";
    }
  } else {
    ADD_FAILURE() << "Missing server config";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, LegacyVersionEncapsulation) {
  if (!version_.HasLongHeaderLengths()) {
    // Decapsulating Legacy Version Encapsulation packets from these versions
    // is not currently supported in QuicDispatcher.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetClientConnectionOptions(QuicTagVector{kQLVE});
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_GT(
      client_connection->GetStats().sent_legacy_version_encapsulated_packets,
      0u);
}

TEST_P(EndToEndTest, LegacyVersionEncapsulationWithMultiPacketChlo) {
  if (!version_.HasLongHeaderLengths()) {
    // Decapsulating Legacy Version Encapsulation packets from these versions
    // is not currently supported in QuicDispatcher.
    ASSERT_TRUE(Initialize());
    return;
  }
  if (!version_.UsesTls()) {
    // This test uses custom transport parameters to increase the size of the
    // CHLO, and those are only supported with TLS.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_config_.SetClientConnectionOptions(QuicTagVector{kQLVE});
  constexpr auto kCustomParameter =
      static_cast<TransportParameters::TransportParameterId>(0xff34);
  client_config_.custom_transport_parameters_to_send()[kCustomParameter] =
      std::string(2000, '?');
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_GT(
      client_connection->GetStats().sent_legacy_version_encapsulated_packets,
      0u);
}

TEST_P(EndToEndTest, LegacyVersionEncapsulationWithVersionNegotiation) {
  if (!version_.HasLongHeaderLengths()) {
    // Decapsulating Legacy Version Encapsulation packets from these versions
    // is not currently supported in QuicDispatcher.
    ASSERT_TRUE(Initialize());
    return;
  }
  client_supported_versions_.insert(client_supported_versions_.begin(),
                                    QuicVersionReservedForNegotiation());
  client_config_.SetClientConnectionOptions(QuicTagVector{kQLVE});
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_GT(
      client_connection->GetStats().sent_legacy_version_encapsulated_packets,
      0u);
}

TEST_P(EndToEndTest, LegacyVersionEncapsulationWithLoss) {
  if (!version_.HasLongHeaderLengths()) {
    // Decapsulating Legacy Version Encapsulation packets from these versions
    // is not currently supported in QuicDispatcher.
    ASSERT_TRUE(Initialize());
    return;
  }
  SetPacketLossPercentage(30);
  client_config_.SetClientConnectionOptions(QuicTagVector{kQLVE});
  // Disable blackhole detection as this test is testing loss recovery.
  client_extra_copts_.push_back(kNBHD);
  ASSERT_TRUE(Initialize());
  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_GT(
      client_connection->GetStats().sent_legacy_version_encapsulated_packets,
      0u);
}

TEST_P(EndToEndTest, KeyUpdateInitiatedByClient) {
  if (!version_.UsesTls()) {
    // Key Update is only supported in TLS handshake.
    ASSERT_TRUE(Initialize());
    return;
  }

  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(0u, client_connection->GetStats().key_update_count);

  EXPECT_TRUE(
      client_connection->InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(1u, client_connection->GetStats().key_update_count);

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(1u, client_connection->GetStats().key_update_count);

  EXPECT_TRUE(
      client_connection->InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));
  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(2u, client_connection->GetStats().key_update_count);

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection) {
    QuicConnectionStats server_stats = server_connection->GetStats();
    EXPECT_EQ(2u, server_stats.key_update_count);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, KeyUpdateInitiatedByServer) {
  if (!version_.UsesTls()) {
    // Key Update is only supported in TLS handshake.
    ASSERT_TRUE(Initialize());
    return;
  }

  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(0u, client_connection->GetStats().key_update_count);

  // Use WaitUntil to ensure the server had executed the key update predicate
  // before sending the Foo request, otherwise the test can be flaky if it
  // receives the Foo request before executing the key update.
  server_thread_->WaitUntil(
      [this]() {
        QuicConnection* server_connection = GetServerConnection();
        if (server_connection != nullptr) {
          if (!server_connection->IsKeyUpdateAllowed()) {
            // Server may not have received ack from client yet for the current
            // key phase, wait a bit and try again.
            return false;
          }
          EXPECT_TRUE(server_connection->InitiateKeyUpdate(
              KeyUpdateReason::kLocalForTests));
        } else {
          ADD_FAILURE() << "Missing server connection";
        }
        return true;
      },
      QuicTime::Delta::FromSeconds(5));

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(1u, client_connection->GetStats().key_update_count);

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(1u, client_connection->GetStats().key_update_count);

  server_thread_->WaitUntil(
      [this]() {
        QuicConnection* server_connection = GetServerConnection();
        if (server_connection != nullptr) {
          if (!server_connection->IsKeyUpdateAllowed()) {
            return false;
          }
          EXPECT_TRUE(server_connection->InitiateKeyUpdate(
              KeyUpdateReason::kLocalForTests));
        } else {
          ADD_FAILURE() << "Missing server connection";
        }
        return true;
      },
      QuicTime::Delta::FromSeconds(5));

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(2u, client_connection->GetStats().key_update_count);

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection) {
    QuicConnectionStats server_stats = server_connection->GetStats();
    EXPECT_EQ(2u, server_stats.key_update_count);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, KeyUpdateInitiatedByBoth) {
  if (!version_.UsesTls()) {
    // Key Update is only supported in TLS handshake.
    ASSERT_TRUE(Initialize());
    return;
  }

  ASSERT_TRUE(Initialize());

  SendSynchronousFooRequestAndCheckResponse();

  // Use WaitUntil to ensure the server had executed the key update predicate
  // before the client sends the Foo request, otherwise the Foo request from
  // the client could trigger the server key update before the server can
  // initiate the key update locally. That would mean the test is no longer
  // hitting the intended test state of both sides locally initiating a key
  // update before receiving a packet in the new key phase from the other side.
  // Additionally the test would fail since InitiateKeyUpdate() would not allow
  // to do another key update yet and return false.
  server_thread_->WaitUntil(
      [this]() {
        QuicConnection* server_connection = GetServerConnection();
        if (server_connection != nullptr) {
          if (!server_connection->IsKeyUpdateAllowed()) {
            // Server may not have received ack from client yet for the current
            // key phase, wait a bit and try again.
            return false;
          }
          EXPECT_TRUE(server_connection->InitiateKeyUpdate(
              KeyUpdateReason::kLocalForTests));
        } else {
          ADD_FAILURE() << "Missing server connection";
        }
        return true;
      },
      QuicTime::Delta::FromSeconds(5));
  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_TRUE(
      client_connection->InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(1u, client_connection->GetStats().key_update_count);

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(1u, client_connection->GetStats().key_update_count);

  server_thread_->WaitUntil(
      [this]() {
        QuicConnection* server_connection = GetServerConnection();
        if (server_connection != nullptr) {
          if (!server_connection->IsKeyUpdateAllowed()) {
            return false;
          }
          EXPECT_TRUE(server_connection->InitiateKeyUpdate(
              KeyUpdateReason::kLocalForTests));
        } else {
          ADD_FAILURE() << "Missing server connection";
        }
        return true;
      },
      QuicTime::Delta::FromSeconds(5));
  EXPECT_TRUE(
      client_connection->InitiateKeyUpdate(KeyUpdateReason::kLocalForTests));

  SendSynchronousFooRequestAndCheckResponse();
  EXPECT_EQ(2u, client_connection->GetStats().key_update_count);

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection) {
    QuicConnectionStats server_stats = server_connection->GetStats();
    EXPECT_EQ(2u, server_stats.key_update_count);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, KeyUpdateInitiatedByConfidentialityLimit) {
  SetQuicFlag(FLAGS_quic_key_update_confidentiality_limit, 4U);

  if (!version_.UsesTls()) {
    // Key Update is only supported in TLS handshake.
    ASSERT_TRUE(Initialize());
    return;
  }

  ASSERT_TRUE(Initialize());

  QuicConnection* client_connection = GetClientConnection();
  ASSERT_TRUE(client_connection);
  EXPECT_EQ(0u, client_connection->GetStats().key_update_count);

  server_thread_->WaitUntil(
      [this]() {
        QuicConnection* server_connection = GetServerConnection();
        if (server_connection != nullptr) {
          EXPECT_EQ(0u, server_connection->GetStats().key_update_count);
        } else {
          ADD_FAILURE() << "Missing server connection";
        }
        return true;
      },
      QuicTime::Delta::FromSeconds(5));

  SendSynchronousFooRequestAndCheckResponse();
  SendSynchronousFooRequestAndCheckResponse();
  SendSynchronousFooRequestAndCheckResponse();
  // Don't know exactly how many packets will be sent in each request/response,
  // so just test that at least one key update occurred.
  EXPECT_LE(1u, client_connection->GetStats().key_update_count);

  server_thread_->Pause();
  QuicConnection* server_connection = GetServerConnection();
  if (server_connection) {
    QuicConnectionStats server_stats = server_connection->GetStats();
    EXPECT_LE(1u, server_stats.key_update_count);
  } else {
    ADD_FAILURE() << "Missing server connection";
  }
  server_thread_->Resume();
}

TEST_P(EndToEndTest, TlsResumptionEnabledOnTheFly) {
  SetQuicFlag(FLAGS_quic_disable_server_tls_resumption, true);
  ASSERT_TRUE(Initialize());

  if (!version_.UsesTls()) {
    // This test is TLS specific.
    return;
  }

  // Send the first request. Client should not have a resumption ticket.
  SendSynchronousFooRequestAndCheckResponse();
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_EQ(client_session->GetCryptoStream()->EarlyDataReason(),
            ssl_early_data_no_session_offered);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  client_->Disconnect();

  SetQuicFlag(FLAGS_quic_disable_server_tls_resumption, false);

  // Send the second request. Client should still have no resumption ticket, but
  // it will receive one which can be used by the next request.
  client_->Connect();
  SendSynchronousFooRequestAndCheckResponse();

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_EQ(client_session->GetCryptoStream()->EarlyDataReason(),
            ssl_early_data_no_session_offered);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  client_->Disconnect();

  // Send the third request in 0RTT.
  client_->Connect();
  SendSynchronousFooRequestAndCheckResponse();

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  client_->Disconnect();
}

TEST_P(EndToEndTest, TlsResumptionDisabledOnTheFly) {
  SetQuicFlag(FLAGS_quic_disable_server_tls_resumption, false);
  ASSERT_TRUE(Initialize());

  if (!version_.UsesTls()) {
    // This test is TLS specific.
    return;
  }

  // Send the first request and then disconnect.
  SendSynchronousFooRequestAndCheckResponse();
  QuicSpdyClientSession* client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  client_->Disconnect();

  // Send the second request in 0RTT.
  client_->Connect();
  SendSynchronousFooRequestAndCheckResponse();

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_TRUE(client_session->EarlyDataAccepted());
  client_->Disconnect();

  SetQuicFlag(FLAGS_quic_disable_server_tls_resumption, true);

  // Send the third request. The client should try resumption but server should
  // decline it.
  client_->Connect();
  SendSynchronousFooRequestAndCheckResponse();

  client_session = GetClientSession();
  ASSERT_TRUE(client_session);
  EXPECT_FALSE(client_session->EarlyDataAccepted());
  EXPECT_EQ(client_session->GetCryptoStream()->EarlyDataReason(),
            ssl_early_data_session_not_resumed);
  client_->Disconnect();

  // Keep sending until the client runs out of resumption tickets.
  for (int i = 0; i < 10; ++i) {
    client_->Connect();
    SendSynchronousFooRequestAndCheckResponse();

    client_session = GetClientSession();
    ASSERT_TRUE(client_session);
    EXPECT_FALSE(client_session->EarlyDataAccepted());
    const auto early_data_reason =
        client_session->GetCryptoStream()->EarlyDataReason();
    client_->Disconnect();

    if (early_data_reason != ssl_early_data_session_not_resumed) {
      EXPECT_EQ(early_data_reason, ssl_early_data_no_session_offered);
      return;
    }
  }

  ADD_FAILURE() << "Client should not have 10 resumption tickets.";
}

}  // namespace
}  // namespace test
}  // namespace quic
