// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/spdy_test_util_common.h"

#include <stdint.h>

#include <cstddef>
#include <utility>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "net/base/host_port_pair.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/ct_policy_status.h"
#include "net/cert/do_nothing_ct_verifier.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/cert/signed_certificate_timestamp_and_status.h"
#include "net/http/http_cache.h"
#include "net/http/http_network_session.h"
#include "net/http/http_network_transaction.h"
#include "net/http/http_server_properties_impl.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket_test_util.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/transport_client_socket_pool.h"
#include "net/spdy/chromium/buffered_spdy_framer.h"
#include "net/spdy/chromium/spdy_http_utils.h"
#include "net/spdy/chromium/spdy_session.h"
#include "net/spdy/chromium/spdy_session_pool.h"
#include "net/spdy/chromium/spdy_stream.h"
#include "net/spdy/core/spdy_alt_svc_wire_format.h"
#include "net/spdy/core/spdy_framer.h"
#include "net/test/gtest_util.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "testing/gmock/include/gmock/gmock.h"

using net::test::IsOk;

namespace net {

namespace {

// Parses a URL into the scheme, host, and path components required for a
// SPDY request.
void ParseUrl(SpdyStringPiece url,
              SpdyString* scheme,
              SpdyString* host,
              SpdyString* path) {
  GURL gurl(url);
  path->assign(gurl.PathForRequest());
  scheme->assign(gurl.scheme());
  host->assign(gurl.host());
  if (gurl.has_port()) {
    host->append(":");
    host->append(gurl.port());
  }
}

}  // namespace

// Chop a frame into an array of MockWrites.
// |frame| is the frame to chop.
// |num_chunks| is the number of chunks to create.
std::unique_ptr<MockWrite[]> ChopWriteFrame(const SpdySerializedFrame& frame,
                                            int num_chunks) {
  auto chunks = std::make_unique<MockWrite[]>(num_chunks);
  int chunk_size = frame.size() / num_chunks;
  for (int index = 0; index < num_chunks; index++) {
    const char* ptr = frame.data() + (index * chunk_size);
    if (index == num_chunks - 1)
      chunk_size +=
          frame.size() % chunk_size;  // The last chunk takes the remainder.
    chunks[index] = MockWrite(ASYNC, ptr, chunk_size);
  }
  return chunks;
}

// Adds headers and values to a map.
// |extra_headers| is an array of { name, value } pairs, arranged as strings
// where the even entries are the header names, and the odd entries are the
// header values.
// |headers| gets filled in from |extra_headers|.
void AppendToHeaderBlock(const char* const extra_headers[],
                         int extra_header_count,
                         SpdyHeaderBlock* headers) {
  SpdyString this_header;
  SpdyString this_value;

  if (!extra_header_count)
    return;

  // Sanity check: Non-NULL header list.
  DCHECK(NULL != extra_headers) << "NULL header value pair list";
  // Sanity check: Non-NULL header map.
  DCHECK(NULL != headers) << "NULL header map";
  // Copy in the headers.
  for (int i = 0; i < extra_header_count; i++) {
    // Sanity check: Non-empty header.
    DCHECK_NE('\0', *extra_headers[i * 2]) << "Empty header value pair";
    this_header = extra_headers[i * 2];
    SpdyString::size_type header_len = this_header.length();
    if (!header_len)
      continue;
    this_value = extra_headers[1 + (i * 2)];
    SpdyString new_value;
    if (headers->find(this_header) != headers->end()) {
      // More than one entry in the header.
      // Don't add the header again, just the append to the value,
      // separated by a NULL character.

      // Adjust the value.
      new_value = (*headers)[this_header].as_string();
      // Put in a NULL separator.
      new_value.append(1, '\0');
      // Append the new value.
      new_value += this_value;
    } else {
      // Not a duplicate, just write the value.
      new_value = this_value;
    }
    (*headers)[this_header] = new_value;
  }
}

// Create a MockWrite from the given SpdySerializedFrame.
MockWrite CreateMockWrite(const SpdySerializedFrame& req) {
  return MockWrite(ASYNC, req.data(), req.size());
}

// Create a MockWrite from the given SpdySerializedFrame and sequence number.
MockWrite CreateMockWrite(const SpdySerializedFrame& req, int seq) {
  return CreateMockWrite(req, seq, ASYNC);
}

// Create a MockWrite from the given SpdySerializedFrame and sequence number.
MockWrite CreateMockWrite(const SpdySerializedFrame& req,
                          int seq,
                          IoMode mode) {
  return MockWrite(mode, req.data(), req.size(), seq);
}

// Create a MockRead from the given SpdySerializedFrame.
MockRead CreateMockRead(const SpdySerializedFrame& resp) {
  return MockRead(ASYNC, resp.data(), resp.size());
}

// Create a MockRead from the given SpdySerializedFrame and sequence number.
MockRead CreateMockRead(const SpdySerializedFrame& resp, int seq) {
  return CreateMockRead(resp, seq, ASYNC);
}

// Create a MockRead from the given SpdySerializedFrame and sequence number.
MockRead CreateMockRead(const SpdySerializedFrame& resp, int seq, IoMode mode) {
  return MockRead(mode, resp.data(), resp.size(), seq);
}

// Combines the given vector of SpdySerializedFrame into a single frame.
SpdySerializedFrame CombineFrames(
    std::vector<const SpdySerializedFrame*> frames) {
  int total_size = 0;
  for (const auto* frame : frames) {
    total_size += frame->size();
  }
  auto data = std::make_unique<char[]>(total_size);
  char* ptr = data.get();
  for (const auto* frame : frames) {
    memcpy(ptr, frame->data(), frame->size());
    ptr += frame->size();
  }
  return SpdySerializedFrame(data.release(), total_size,
                             /* owns_buffer = */ true);
}

namespace {

class PriorityGetter : public BufferedSpdyFramerVisitorInterface {
 public:
  PriorityGetter() : priority_(0) {}
  ~PriorityGetter() override {}

  SpdyPriority priority() const {
    return priority_;
  }

  void OnError(
      Http2DecoderAdapter::SpdyFramerError spdy_framer_error) override {}
  void OnStreamError(SpdyStreamId stream_id,
                     const SpdyString& description) override {}
  void OnHeaders(SpdyStreamId stream_id,
                 bool has_priority,
                 int weight,
                 SpdyStreamId parent_stream_id,
                 bool exclusive,
                 bool fin,
                 SpdyHeaderBlock headers) override {
    if (has_priority) {
      priority_ = Http2WeightToSpdy3Priority(weight);
    }
  }
  void OnDataFrameHeader(SpdyStreamId stream_id,
                         size_t length,
                         bool fin) override {}
  void OnStreamFrameData(SpdyStreamId stream_id,
                         const char* data,
                         size_t len) override {}
  void OnStreamEnd(SpdyStreamId stream_id) override {}
  void OnStreamPadding(SpdyStreamId stream_id, size_t len) override {}
  void OnSettings() override {}
  void OnSettingsAck() override {}
  void OnSetting(SpdySettingsIds id, uint32_t value) override {}
  void OnSettingsEnd() override {}
  void OnPing(SpdyPingId unique_id, bool is_ack) override {}
  void OnRstStream(SpdyStreamId stream_id, SpdyErrorCode error_code) override {}
  void OnGoAway(SpdyStreamId last_accepted_stream_id,
                SpdyErrorCode error_code,
                SpdyStringPiece debug_data) override {}
  void OnWindowUpdate(SpdyStreamId stream_id, int delta_window_size) override {}
  void OnPushPromise(SpdyStreamId stream_id,
                     SpdyStreamId promised_stream_id,
                     SpdyHeaderBlock headers) override {}
  void OnAltSvc(SpdyStreamId stream_id,
                SpdyStringPiece origin,
                const SpdyAltSvcWireFormat::AlternativeServiceVector&
                    altsvc_vector) override {}
  bool OnUnknownFrame(SpdyStreamId stream_id, uint8_t frame_type) override {
    return false;
  }

 private:
  SpdyPriority priority_;
};

}  // namespace

bool GetSpdyPriority(const SpdySerializedFrame& frame, SpdyPriority* priority) {
  NetLogWithSource net_log;
  BufferedSpdyFramer framer(kMaxHeaderListSizeForTest, net_log);
  PriorityGetter priority_getter;
  framer.set_visitor(&priority_getter);
  size_t frame_size = frame.size();
  if (framer.ProcessInput(frame.data(), frame_size) != frame_size) {
    return false;
  }
  *priority = priority_getter.priority();
  return true;
}

base::WeakPtr<SpdyStream> CreateStreamSynchronously(
    SpdyStreamType type,
    const base::WeakPtr<SpdySession>& session,
    const GURL& url,
    RequestPriority priority,
    const NetLogWithSource& net_log) {
  SpdyStreamRequest stream_request;
  int rv = stream_request.StartRequest(type, session, url, priority, net_log,
                                       CompletionCallback());
  return
      (rv == OK) ? stream_request.ReleaseStream() : base::WeakPtr<SpdyStream>();
}

StreamReleaserCallback::StreamReleaserCallback() {}

StreamReleaserCallback::~StreamReleaserCallback() {}

CompletionCallback StreamReleaserCallback::MakeCallback(
    SpdyStreamRequest* request) {
  return base::Bind(&StreamReleaserCallback::OnComplete,
                    base::Unretained(this),
                    request);
}

void StreamReleaserCallback::OnComplete(
    SpdyStreamRequest* request, int result) {
  if (result == OK)
    request->ReleaseStream()->Cancel();
  SetResult(result);
}

MockECSignatureCreator::MockECSignatureCreator(crypto::ECPrivateKey* key)
    : key_(key) {
}

bool MockECSignatureCreator::Sign(const uint8_t* data,
                                  int data_len,
                                  std::vector<uint8_t>* signature) {
  std::vector<uint8_t> private_key;
  if (!key_->ExportPrivateKey(&private_key))
    return false;
  SpdyString head = "fakesignature";
  SpdyString tail = "/fakesignature";

  signature->clear();
  signature->insert(signature->end(), head.begin(), head.end());
  signature->insert(signature->end(), private_key.begin(), private_key.end());
  signature->insert(signature->end(), '-');
  signature->insert(signature->end(), data, data + data_len);
  signature->insert(signature->end(), tail.begin(), tail.end());
  return true;
}

bool MockECSignatureCreator::DecodeSignature(
    const std::vector<uint8_t>& signature,
    std::vector<uint8_t>* out_raw_sig) {
  *out_raw_sig = signature;
  return true;
}

MockECSignatureCreatorFactory::MockECSignatureCreatorFactory() {
  crypto::ECSignatureCreator::SetFactoryForTesting(this);
}

MockECSignatureCreatorFactory::~MockECSignatureCreatorFactory() {
  crypto::ECSignatureCreator::SetFactoryForTesting(nullptr);
}

std::unique_ptr<crypto::ECSignatureCreator>
MockECSignatureCreatorFactory::Create(crypto::ECPrivateKey* key) {
  return std::make_unique<MockECSignatureCreator>(key);
}

SpdySessionDependencies::SpdySessionDependencies()
    : SpdySessionDependencies(ProxyService::CreateDirect()) {}

SpdySessionDependencies::SpdySessionDependencies(
    std::unique_ptr<ProxyService> proxy_service)
    : host_resolver(new MockCachingHostResolver),
      cert_verifier(new MockCertVerifier),
      channel_id_service(nullptr),
      transport_security_state(new TransportSecurityState),
      cert_transparency_verifier(new DoNothingCTVerifier),
      ct_policy_enforcer(new CTPolicyEnforcer),
      proxy_service(std::move(proxy_service)),
      ssl_config_service(new SSLConfigServiceDefaults),
      socket_factory(new MockClientSocketFactory),
      http_auth_handler_factory(
          HttpAuthHandlerFactory::CreateDefault(host_resolver.get())),
      http_server_properties(new HttpServerPropertiesImpl),
      enable_ip_pooling(true),
      enable_ping(false),
      enable_user_alternate_protocol_ports(false),
      enable_quic(false),
      enable_server_push_cancellation(false),
      session_max_recv_window_size(kDefaultInitialWindowSize),
      time_func(&base::TimeTicks::Now),
      enable_http2_alternative_service(false),
      net_log(nullptr),
      http_09_on_non_default_ports_enabled(false) {
  // Note: The CancelledTransaction test does cleanup by running all
  // tasks in the message loop (RunAllPending).  Unfortunately, that
  // doesn't clean up tasks on the host resolver thread; and
  // TCPConnectJob is currently not cancellable.  Using synchronous
  // lookups allows the test to shutdown cleanly.  Until we have
  // cancellable TCPConnectJobs, use synchronous lookups.
  host_resolver->set_synchronous_mode(true);
  http2_settings[SETTINGS_INITIAL_WINDOW_SIZE] = kDefaultInitialWindowSize;
}

SpdySessionDependencies::~SpdySessionDependencies() {}

// static
std::unique_ptr<HttpNetworkSession> SpdySessionDependencies::SpdyCreateSession(
    SpdySessionDependencies* session_deps) {
  return SpdyCreateSessionWithSocketFactory(session_deps,
                                            session_deps->socket_factory.get());
}

// static
std::unique_ptr<HttpNetworkSession>
SpdySessionDependencies::SpdyCreateSessionWithSocketFactory(
    SpdySessionDependencies* session_deps,
    ClientSocketFactory* factory) {
  HttpNetworkSession::Params session_params = CreateSessionParams(session_deps);
  HttpNetworkSession::Context session_context =
      CreateSessionContext(session_deps);
  session_context.client_socket_factory = factory;
  auto http_session =
      std::make_unique<HttpNetworkSession>(session_params, session_context);
  SpdySessionPoolPeer pool_peer(http_session->spdy_session_pool());
  pool_peer.SetEnableSendingInitialData(false);
  return http_session;
}

// static
HttpNetworkSession::Params SpdySessionDependencies::CreateSessionParams(
    SpdySessionDependencies* session_deps) {
  HttpNetworkSession::Params params;
  params.enable_spdy_ping_based_connection_checking = session_deps->enable_ping;
  params.enable_user_alternate_protocol_ports =
      session_deps->enable_user_alternate_protocol_ports;
  params.enable_quic = session_deps->enable_quic;
  params.enable_server_push_cancellation =
      session_deps->enable_server_push_cancellation;
  params.spdy_session_max_recv_window_size =
      session_deps->session_max_recv_window_size;
  params.http2_settings = session_deps->http2_settings;
  params.time_func = session_deps->time_func;
  params.enable_http2_alternative_service =
      session_deps->enable_http2_alternative_service;
  params.http_09_on_non_default_ports_enabled =
      session_deps->http_09_on_non_default_ports_enabled;
  return params;
}

HttpNetworkSession::Context SpdySessionDependencies::CreateSessionContext(
    SpdySessionDependencies* session_deps) {
  HttpNetworkSession::Context context;
  context.client_socket_factory = session_deps->socket_factory.get();
  context.host_resolver = session_deps->host_resolver.get();
  context.cert_verifier = session_deps->cert_verifier.get();
  context.channel_id_service = session_deps->channel_id_service.get();
  context.transport_security_state =
      session_deps->transport_security_state.get();
  context.cert_transparency_verifier =
      session_deps->cert_transparency_verifier.get();
  context.ct_policy_enforcer = session_deps->ct_policy_enforcer.get();
  context.proxy_service = session_deps->proxy_service.get();
  context.ssl_config_service = session_deps->ssl_config_service.get();
  context.http_auth_handler_factory =
      session_deps->http_auth_handler_factory.get();
  context.http_server_properties = session_deps->http_server_properties.get();
  context.proxy_delegate = session_deps->proxy_delegate.get();
  context.net_log = session_deps->net_log;
  return context;
}

class AllowAnyCertCTPolicyEnforcer : public CTPolicyEnforcer {
 public:
  AllowAnyCertCTPolicyEnforcer() {}
  ~AllowAnyCertCTPolicyEnforcer() override = default;

  ct::CertPolicyCompliance DoesConformToCertPolicy(
      X509Certificate* cert,
      const SCTList& verified_scts,
      const NetLogWithSource& net_log) override {
    return ct::CertPolicyCompliance::CERT_POLICY_COMPLIES_VIA_SCTS;
  }
};

SpdyURLRequestContext::SpdyURLRequestContext() : storage_(this) {
  storage_.set_host_resolver(
      std::unique_ptr<HostResolver>(new MockHostResolver));
  storage_.set_cert_verifier(std::make_unique<MockCertVerifier>());
  storage_.set_transport_security_state(
      std::make_unique<TransportSecurityState>());
  storage_.set_proxy_service(ProxyService::CreateDirect());
  storage_.set_ct_policy_enforcer(
      std::make_unique<AllowAnyCertCTPolicyEnforcer>());
  storage_.set_cert_transparency_verifier(
      std::make_unique<DoNothingCTVerifier>());
  storage_.set_ssl_config_service(new SSLConfigServiceDefaults);
  storage_.set_http_auth_handler_factory(
      HttpAuthHandlerFactory::CreateDefault(host_resolver()));
  storage_.set_http_server_properties(
      std::unique_ptr<HttpServerProperties>(new HttpServerPropertiesImpl()));
  storage_.set_job_factory(std::make_unique<URLRequestJobFactoryImpl>());
  HttpNetworkSession::Params session_params;
  session_params.enable_spdy_ping_based_connection_checking = false;

  HttpNetworkSession::Context session_context;
  session_context.client_socket_factory = &socket_factory_;
  session_context.host_resolver = host_resolver();
  session_context.cert_verifier = cert_verifier();
  session_context.transport_security_state = transport_security_state();
  session_context.proxy_service = proxy_service();
  session_context.ct_policy_enforcer = ct_policy_enforcer();
  session_context.cert_transparency_verifier = cert_transparency_verifier();
  session_context.ssl_config_service = ssl_config_service();
  session_context.http_auth_handler_factory = http_auth_handler_factory();
  session_context.http_server_properties = http_server_properties();
  storage_.set_http_network_session(
      std::make_unique<HttpNetworkSession>(session_params, session_context));
  SpdySessionPoolPeer pool_peer(
      storage_.http_network_session()->spdy_session_pool());
  pool_peer.SetEnableSendingInitialData(false);
  storage_.set_http_transaction_factory(std::make_unique<HttpCache>(
      storage_.http_network_session(), HttpCache::DefaultBackend::InMemory(0),
      false));
}

SpdyURLRequestContext::~SpdyURLRequestContext() {
  AssertNoURLRequests();
}

bool HasSpdySession(SpdySessionPool* pool, const SpdySessionKey& key) {
  return static_cast<bool>(pool->FindAvailableSession(
      key, /* enable_ip_based_pooling = */ true, NetLogWithSource()));
}

namespace {

base::WeakPtr<SpdySession> CreateSpdySessionHelper(
    HttpNetworkSession* http_session,
    const SpdySessionKey& key,
    const NetLogWithSource& net_log,
    Error expected_status,
    bool enable_ip_based_pooling) {
  EXPECT_FALSE(http_session->spdy_session_pool()->FindAvailableSession(
      key, enable_ip_based_pooling, NetLogWithSource()));

  scoped_refptr<TransportSocketParams> transport_params(
      new TransportSocketParams(
          key.host_port_pair(), false, OnHostResolutionCallback(),
          TransportSocketParams::COMBINE_CONNECT_AND_WRITE_DEFAULT));

  auto connection = std::make_unique<ClientSocketHandle>();
  TestCompletionCallback callback;

  int rv = ERR_UNEXPECTED;
  SSLConfig ssl_config;
  scoped_refptr<SSLSocketParams> ssl_params(
      new SSLSocketParams(transport_params, NULL, NULL, key.host_port_pair(),
                          ssl_config, key.privacy_mode(), 0, false));
  rv = connection->Init(
      key.host_port_pair().ToString(), ssl_params, MEDIUM,
      ClientSocketPool::RespectLimits::ENABLED, callback.callback(),
      http_session->GetSSLSocketPool(HttpNetworkSession::NORMAL_SOCKET_POOL),
      net_log);

  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();

  EXPECT_THAT(rv, IsOk());

  base::WeakPtr<SpdySession> spdy_session =
      http_session->spdy_session_pool()->CreateAvailableSessionFromSocket(
          key, std::move(connection), net_log);
  // Failure is reported asynchronously.
  EXPECT_TRUE(spdy_session);
  EXPECT_TRUE(HasSpdySession(http_session->spdy_session_pool(), key));
  return spdy_session;
}

}  // namespace

base::WeakPtr<SpdySession> TryCreateSpdySessionExpectingFailure(
    HttpNetworkSession* http_session,
    const SpdySessionKey& key,
    Error expected_error,
    const NetLogWithSource& net_log) {
  DCHECK_LT(expected_error, ERR_IO_PENDING);
  return CreateSpdySessionHelper(http_session, key, net_log, expected_error,
                                 /* enable_ip_based_pooling = */ true);
}

base::WeakPtr<SpdySession> CreateSpdySession(HttpNetworkSession* http_session,
                                             const SpdySessionKey& key,
                                             const NetLogWithSource& net_log) {
  return CreateSpdySessionHelper(http_session, key, net_log, OK,
                                 /* enable_ip_based_pooling = */ true);
}

base::WeakPtr<SpdySession> CreateSpdySessionWithIpBasedPoolingDisabled(
    HttpNetworkSession* http_session,
    const SpdySessionKey& key,
    const NetLogWithSource& net_log) {
  return CreateSpdySessionHelper(http_session, key, net_log, OK,
                                 /* enable_ip_based_pooling = */ false);
}

namespace {

// A ClientSocket used for CreateFakeSpdySession() below.
class FakeSpdySessionClientSocket : public MockClientSocket {
 public:
  explicit FakeSpdySessionClientSocket(int read_result)
      : MockClientSocket(NetLogWithSource()), read_result_(read_result) {}

  ~FakeSpdySessionClientSocket() override {}

  int Read(IOBuffer* buf,
           int buf_len,
           const CompletionCallback& callback) override {
    return read_result_;
  }

  int Write(IOBuffer* buf,
            int buf_len,
            const CompletionCallback& callback) override {
    return ERR_IO_PENDING;
  }

  // Return kProtoUnknown to use the pool's default protocol.
  NextProto GetNegotiatedProtocol() const override { return kProtoUnknown; }

  // The functions below are not expected to be called.

  int Connect(const CompletionCallback& callback) override {
    ADD_FAILURE();
    return ERR_UNEXPECTED;
  }

  bool WasEverUsed() const override {
    ADD_FAILURE();
    return false;
  }

  bool WasAlpnNegotiated() const override {
    ADD_FAILURE();
    return false;
  }

  bool GetSSLInfo(SSLInfo* ssl_info) override {
    ADD_FAILURE();
    return false;
  }

  int64_t GetTotalReceivedBytes() const override {
    NOTIMPLEMENTED();
    return 0;
  }

 private:
  int read_result_;
};

base::WeakPtr<SpdySession> CreateFakeSpdySessionHelper(
    SpdySessionPool* pool,
    const SpdySessionKey& key,
    Error expected_status) {
  EXPECT_NE(expected_status, ERR_IO_PENDING);
  EXPECT_FALSE(HasSpdySession(pool, key));
  auto handle = std::make_unique<ClientSocketHandle>();
  handle->SetSocket(std::make_unique<FakeSpdySessionClientSocket>(
      expected_status == OK ? ERR_IO_PENDING : expected_status));
  base::WeakPtr<SpdySession> spdy_session =
      pool->CreateAvailableSessionFromSocket(key, std::move(handle),
                                             NetLogWithSource());
  // Failure is reported asynchronously.
  EXPECT_TRUE(spdy_session);
  EXPECT_TRUE(HasSpdySession(pool, key));
  return spdy_session;
}

}  // namespace

base::WeakPtr<SpdySession> CreateFakeSpdySession(SpdySessionPool* pool,
                                                 const SpdySessionKey& key) {
  return CreateFakeSpdySessionHelper(pool, key, OK);
}

base::WeakPtr<SpdySession> TryCreateFakeSpdySessionExpectingFailure(
    SpdySessionPool* pool,
    const SpdySessionKey& key,
    Error expected_error) {
  DCHECK_LT(expected_error, ERR_IO_PENDING);
  return CreateFakeSpdySessionHelper(pool, key, expected_error);
}

SpdySessionPoolPeer::SpdySessionPoolPeer(SpdySessionPool* pool) : pool_(pool) {
}

void SpdySessionPoolPeer::RemoveAliases(const SpdySessionKey& key) {
  pool_->RemoveAliases(key);
}

void SpdySessionPoolPeer::SetEnableSendingInitialData(bool enabled) {
  pool_->enable_sending_initial_data_ = enabled;
}

SpdyTestUtil::SpdyTestUtil()
    : headerless_spdy_framer_(SpdyFramer::ENABLE_COMPRESSION),
      request_spdy_framer_(SpdyFramer::ENABLE_COMPRESSION),
      response_spdy_framer_(SpdyFramer::ENABLE_COMPRESSION),
      default_url_(GURL(kDefaultUrl)) {}

SpdyTestUtil::~SpdyTestUtil() {}

void SpdyTestUtil::AddUrlToHeaderBlock(SpdyStringPiece url,
                                       SpdyHeaderBlock* headers) const {
  SpdyString scheme, host, path;
  ParseUrl(url, &scheme, &host, &path);
  (*headers)[kHttp2AuthorityHeader] = host;
  (*headers)[kHttp2SchemeHeader] = scheme;
  (*headers)[kHttp2PathHeader] = path;
}

// static
SpdyHeaderBlock SpdyTestUtil::ConstructGetHeaderBlock(SpdyStringPiece url) {
  return ConstructHeaderBlock("GET", url, NULL);
}

// static
SpdyHeaderBlock SpdyTestUtil::ConstructGetHeaderBlockForProxy(
    SpdyStringPiece url) {
  return ConstructGetHeaderBlock(url);
}

// static
SpdyHeaderBlock SpdyTestUtil::ConstructHeadHeaderBlock(SpdyStringPiece url,
                                                       int64_t content_length) {
  return ConstructHeaderBlock("HEAD", url, nullptr);
}

// static
SpdyHeaderBlock SpdyTestUtil::ConstructPostHeaderBlock(SpdyStringPiece url,
                                                       int64_t content_length) {
  return ConstructHeaderBlock("POST", url, &content_length);
}

// static
SpdyHeaderBlock SpdyTestUtil::ConstructPutHeaderBlock(SpdyStringPiece url,
                                                      int64_t content_length) {
  return ConstructHeaderBlock("PUT", url, &content_length);
}

SpdyString SpdyTestUtil::ConstructSpdyReplyString(
    const SpdyHeaderBlock& headers) const {
  SpdyString reply_string;
  for (SpdyHeaderBlock::const_iterator it = headers.begin();
       it != headers.end(); ++it) {
    SpdyString key = it->first.as_string();
    // Remove leading colon from pseudo headers.
    if (key[0] == ':')
      key = key.substr(1);
    for (const SpdyString& value :
         base::SplitString(it->second, SpdyStringPiece("\0", 1),
                           base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
      reply_string += key + ": " + value + "\n";
    }
  }
  return reply_string;
}

// TODO(jgraettinger): Eliminate uses of this method in tests (prefer
// SpdySettingsIR).
SpdySerializedFrame SpdyTestUtil::ConstructSpdySettings(
    const SettingsMap& settings) {
  SpdySettingsIR settings_ir;
  for (SettingsMap::const_iterator it = settings.begin(); it != settings.end();
       ++it) {
    settings_ir.AddSetting(it->first, it->second);
  }
  return SpdySerializedFrame(
      headerless_spdy_framer_.SerializeFrame(settings_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdySettingsAck() {
  SpdySettingsIR settings_ir;
  settings_ir.set_is_ack(true);
  return SpdySerializedFrame(
      headerless_spdy_framer_.SerializeFrame(settings_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyPing(uint32_t ping_id,
                                                    bool is_ack) {
  SpdyPingIR ping_ir(ping_id);
  ping_ir.set_is_ack(is_ack);
  return SpdySerializedFrame(headerless_spdy_framer_.SerializeFrame(ping_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyGoAway() {
  return ConstructSpdyGoAway(0);
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyGoAway(
    SpdyStreamId last_good_stream_id) {
  SpdyGoAwayIR go_ir(last_good_stream_id, ERROR_CODE_NO_ERROR, "go away");
  return SpdySerializedFrame(headerless_spdy_framer_.SerializeFrame(go_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyGoAway(
    SpdyStreamId last_good_stream_id,
    SpdyErrorCode error_code,
    const SpdyString& desc) {
  SpdyGoAwayIR go_ir(last_good_stream_id, error_code, desc);
  return SpdySerializedFrame(headerless_spdy_framer_.SerializeFrame(go_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyWindowUpdate(
    const SpdyStreamId stream_id,
    uint32_t delta_window_size) {
  SpdyWindowUpdateIR update_ir(stream_id, delta_window_size);
  return SpdySerializedFrame(headerless_spdy_framer_.SerializeFrame(update_ir));
}

// TODO(jgraettinger): Eliminate uses of this method in tests (prefer
// SpdyRstStreamIR).
SpdySerializedFrame SpdyTestUtil::ConstructSpdyRstStream(
    SpdyStreamId stream_id,
    SpdyErrorCode error_code) {
  SpdyRstStreamIR rst_ir(stream_id, error_code);
  return SpdySerializedFrame(
      headerless_spdy_framer_.SerializeRstStream(rst_ir));
}

// TODO(jgraettinger): Eliminate uses of this method in tests (prefer
// SpdyPriorityIR).
SpdySerializedFrame SpdyTestUtil::ConstructSpdyPriority(
    SpdyStreamId stream_id,
    SpdyStreamId parent_stream_id,
    RequestPriority request_priority,
    bool exclusive) {
  int weight = Spdy3PriorityToHttp2Weight(
      ConvertRequestPriorityToSpdyPriority(request_priority));
  SpdyPriorityIR ir(stream_id, parent_stream_id, weight, exclusive);
  return SpdySerializedFrame(headerless_spdy_framer_.SerializePriority(ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyGet(
    const char* const url,
    SpdyStreamId stream_id,
    RequestPriority request_priority) {
  SpdyHeaderBlock block(ConstructGetHeaderBlock(url));
  return ConstructSpdyHeaders(stream_id, std::move(block), request_priority,
                              true);
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyGet(
    const char* const extra_headers[],
    int extra_header_count,
    int stream_id,
    RequestPriority request_priority,
    bool direct) {
  SpdyHeaderBlock block;
  block[kHttp2MethodHeader] = "GET";
  AddUrlToHeaderBlock(default_url_.spec(), &block);
  AppendToHeaderBlock(extra_headers, extra_header_count, &block);
  return ConstructSpdyHeaders(stream_id, std::move(block), request_priority,
                              true);
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyConnect(
    const char* const extra_headers[],
    int extra_header_count,
    int stream_id,
    RequestPriority priority,
    const HostPortPair& host_port_pair) {
  SpdyHeaderBlock block;
  block[kHttp2MethodHeader] = "CONNECT";
  block[kHttp2AuthorityHeader] = host_port_pair.ToString();
  AppendToHeaderBlock(extra_headers, extra_header_count, &block);
  return ConstructSpdyHeaders(stream_id, std::move(block), priority, false);
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyPush(
    const char* const extra_headers[],
    int extra_header_count,
    int stream_id,
    int associated_stream_id,
    const char* url) {
  SpdyHeaderBlock push_promise_header_block;
  push_promise_header_block[kHttp2MethodHeader] = "GET";
  AddUrlToHeaderBlock(url, &push_promise_header_block);
  SpdyPushPromiseIR push_promise(associated_stream_id, stream_id,
                                 std::move(push_promise_header_block));
  SpdySerializedFrame push_promise_frame(
      response_spdy_framer_.SerializeFrame(push_promise));

  SpdyHeaderBlock headers_header_block;
  headers_header_block[kHttp2StatusHeader] = "200";
  headers_header_block["hello"] = "bye";
  AppendToHeaderBlock(extra_headers, extra_header_count, &headers_header_block);
  SpdyHeadersIR headers(stream_id, std::move(headers_header_block));
  SpdySerializedFrame headers_frame(
      response_spdy_framer_.SerializeFrame(headers));

  return CombineFrames({&push_promise_frame, &headers_frame});
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyPush(
    const char* const extra_headers[],
    int extra_header_count,
    int stream_id,
    int associated_stream_id,
    const char* url,
    const char* status,
    const char* location) {
  SpdyHeaderBlock push_promise_header_block;
  push_promise_header_block[kHttp2MethodHeader] = "GET";
  AddUrlToHeaderBlock(url, &push_promise_header_block);
  SpdyPushPromiseIR push_promise(associated_stream_id, stream_id,
                                 std::move(push_promise_header_block));
  SpdySerializedFrame push_promise_frame(
      response_spdy_framer_.SerializeFrame(push_promise));

  SpdyHeaderBlock headers_header_block;
  headers_header_block["hello"] = "bye";
  headers_header_block[kHttp2StatusHeader] = status;
  headers_header_block["location"] = location;
  AppendToHeaderBlock(extra_headers, extra_header_count, &headers_header_block);
  SpdyHeadersIR headers(stream_id, std::move(headers_header_block));
  SpdySerializedFrame headers_frame(
      response_spdy_framer_.SerializeFrame(headers));

  return CombineFrames({&push_promise_frame, &headers_frame});
}

SpdySerializedFrame SpdyTestUtil::ConstructInitialSpdyPushFrame(
    SpdyHeaderBlock headers,
    int stream_id,
    int associated_stream_id) {
  SpdyPushPromiseIR push_promise(associated_stream_id, stream_id,
                                 std::move(headers));
  return SpdySerializedFrame(
      response_spdy_framer_.SerializeFrame(push_promise));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyPushHeaders(
    int stream_id,
    const char* const extra_headers[],
    int extra_header_count) {
  SpdyHeaderBlock header_block;
  header_block[kHttp2StatusHeader] = "200";
  AppendToHeaderBlock(extra_headers, extra_header_count, &header_block);
  SpdyHeadersIR headers(stream_id, std::move(header_block));
  return SpdySerializedFrame(response_spdy_framer_.SerializeFrame(headers));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyResponseHeaders(
    int stream_id,
    SpdyHeaderBlock headers,
    bool fin) {
  SpdyHeadersIR spdy_headers(stream_id, std::move(headers));
  spdy_headers.set_fin(fin);
  return SpdySerializedFrame(
      response_spdy_framer_.SerializeFrame(spdy_headers));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyHeaders(int stream_id,
                                                       SpdyHeaderBlock block,
                                                       RequestPriority priority,
                                                       bool fin) {
  // Get the stream id of the next highest priority request
  // (most recent request of the same priority, or last request of
  // an earlier priority).
  // Note that this is a duplicate of the logic in Http2PriorityDependencies
  // (slightly transformed as this is based on RequestPriority and that logic
  // on SpdyPriority, but only slightly transformed) and hence tests using
  // this function do not effectively test that logic.
  // That logic is tested by the Http2PriorityDependencies unit tests.
  int parent_stream_id = 0;
  for (int q = priority; q <= HIGHEST; ++q) {
    if (!priority_to_stream_id_list_[q].empty()) {
      parent_stream_id = priority_to_stream_id_list_[q].back();
      break;
    }
  }

  priority_to_stream_id_list_[priority].push_back(stream_id);

  SpdyHeadersIR headers(stream_id, std::move(block));
  headers.set_has_priority(true);
  headers.set_weight(Spdy3PriorityToHttp2Weight(
      ConvertRequestPriorityToSpdyPriority(priority)));
  headers.set_parent_stream_id(parent_stream_id);
  headers.set_exclusive(true);
  headers.set_fin(fin);
  return SpdySerializedFrame(request_spdy_framer_.SerializeFrame(headers));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyReply(int stream_id,
                                                     SpdyHeaderBlock headers) {
  SpdyHeadersIR reply(stream_id, std::move(headers));
  return SpdySerializedFrame(response_spdy_framer_.SerializeFrame(reply));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyReplyError(
    const char* const status,
    const char* const* const extra_headers,
    int extra_header_count,
    int stream_id) {
  SpdyHeaderBlock block;
  block[kHttp2StatusHeader] = status;
  block["hello"] = "bye";
  AppendToHeaderBlock(extra_headers, extra_header_count, &block);

  return ConstructSpdyReply(stream_id, std::move(block));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyReplyError(int stream_id) {
  return ConstructSpdyReplyError("500", NULL, 0, 1);
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyGetReply(
    const char* const extra_headers[],
    int extra_header_count,
    int stream_id) {
  SpdyHeaderBlock block;
  block[kHttp2StatusHeader] = "200";
  block["hello"] = "bye";
  AppendToHeaderBlock(extra_headers, extra_header_count, &block);

  return ConstructSpdyReply(stream_id, std::move(block));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyPost(
    const char* url,
    SpdyStreamId stream_id,
    int64_t content_length,
    RequestPriority priority,
    const char* const extra_headers[],
    int extra_header_count) {
  SpdyHeaderBlock block(ConstructPostHeaderBlock(url, content_length));
  AppendToHeaderBlock(extra_headers, extra_header_count, &block);
  return ConstructSpdyHeaders(stream_id, std::move(block), priority, false);
}

SpdySerializedFrame SpdyTestUtil::ConstructChunkedSpdyPost(
    const char* const extra_headers[],
    int extra_header_count) {
  SpdyHeaderBlock block;
  block[kHttp2MethodHeader] = "POST";
  AddUrlToHeaderBlock(default_url_.spec(), &block);
  AppendToHeaderBlock(extra_headers, extra_header_count, &block);
  return ConstructSpdyHeaders(1, std::move(block), LOWEST, false);
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyPostReply(
    const char* const extra_headers[],
    int extra_header_count) {
  // TODO(jgraettinger): Remove this method.
  return ConstructSpdyGetReply(extra_headers, extra_header_count, 1);
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyDataFrame(int stream_id,
                                                         bool fin) {
  SpdyDataIR data_ir(stream_id, SpdyStringPiece(kUploadData, kUploadDataSize));
  data_ir.set_fin(fin);
  return SpdySerializedFrame(headerless_spdy_framer_.SerializeData(data_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyDataFrame(int stream_id,
                                                         const char* data,
                                                         uint32_t len,
                                                         bool fin) {
  SpdyDataIR data_ir(stream_id, SpdyStringPiece(data, len));
  data_ir.set_fin(fin);
  return SpdySerializedFrame(headerless_spdy_framer_.SerializeData(data_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructSpdyDataFrame(int stream_id,
                                                         const char* data,
                                                         uint32_t len,
                                                         bool fin,
                                                         int padding_length) {
  SpdyDataIR data_ir(stream_id, SpdyStringPiece(data, len));
  data_ir.set_fin(fin);
  data_ir.set_padding_len(padding_length);
  return SpdySerializedFrame(headerless_spdy_framer_.SerializeData(data_ir));
}

SpdySerializedFrame SpdyTestUtil::ConstructWrappedSpdyFrame(
    const SpdySerializedFrame& frame,
    int stream_id) {
  return ConstructSpdyDataFrame(stream_id, frame.data(), frame.size(), false);
}

SpdySerializedFrame SpdyTestUtil::SerializeFrame(const SpdyFrameIR& frame_ir) {
  return headerless_spdy_framer_.SerializeFrame(frame_ir);
}

void SpdyTestUtil::UpdateWithStreamDestruction(int stream_id) {
  for (auto priority_it = priority_to_stream_id_list_.begin();
       priority_it != priority_to_stream_id_list_.end(); ++priority_it) {
    for (auto stream_it = priority_it->second.begin();
         stream_it != priority_it->second.end(); ++stream_it) {
      if (*stream_it == stream_id) {
        priority_it->second.erase(stream_it);
        return;
      }
    }
  }
  NOTREACHED();
}

// static
SpdyHeaderBlock SpdyTestUtil::ConstructHeaderBlock(SpdyStringPiece method,
                                                   SpdyStringPiece url,
                                                   int64_t* content_length) {
  SpdyString scheme, host, path;
  ParseUrl(url, &scheme, &host, &path);
  SpdyHeaderBlock headers;
  headers[kHttp2MethodHeader] = method.as_string();
  headers[kHttp2AuthorityHeader] = host.c_str();
  headers[kHttp2SchemeHeader] = scheme.c_str();
  headers[kHttp2PathHeader] = path.c_str();
  if (content_length) {
    SpdyString length_str = base::Int64ToString(*content_length);
    headers["content-length"] = length_str;
  }
  return headers;
}

}  // namespace net
