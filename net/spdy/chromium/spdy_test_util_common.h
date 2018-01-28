// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_SPDY_TEST_UTIL_COMMON_H_
#define NET_SPDY_CHROMIUM_SPDY_TEST_UTIL_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "crypto/ec_private_key.h"
#include "crypto/ec_signature_creator.h"
#include "net/base/completion_callback.h"
#include "net/base/proxy_delegate.h"
#include "net/base/request_priority.h"
#include "net/base/test_completion_callback.h"
#include "net/cert/cert_verifier.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_network_session.h"
#include "net/http/http_response_info.h"
#include "net/http/http_server_properties_impl.h"
#include "net/http/transport_security_state.h"
#include "net/proxy/proxy_server.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/socket_test_util.h"
#include "net/spdy/core/spdy_protocol.h"
#include "net/spdy/platform/api/spdy_string.h"
#include "net/spdy/platform/api/spdy_string_piece.h"
#include "net/ssl/ssl_config_service_defaults.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_storage.h"
#include "testing/gtest/include/gtest/gtest.h"

class GURL;

namespace net {

class CTVerifier;
class CTPolicyEnforcer;
class HostPortPair;
class NetLogWithSource;
class SpdySession;
class SpdySessionKey;
class SpdySessionPool;
class SpdyStream;
class SpdyStreamRequest;

// Default upload data used by both, mock objects and framer when creating
// data frames.
const char kDefaultUrl[] = "https://www.example.org/";
const char kUploadData[] = "hello!";
const int kUploadDataSize = arraysize(kUploadData)-1;

// While HTTP/2 protocol defines default SETTINGS_MAX_HEADER_LIST_SIZE_FOR_TEST
// to be unlimited, BufferedSpdyFramer constructor requires a value.
const uint32_t kMaxHeaderListSizeForTest = 1024;

// Chop a SpdySerializedFrame into an array of MockWrites.
// |frame| is the frame to chop.
// |num_chunks| is the number of chunks to create.
std::unique_ptr<MockWrite[]> ChopWriteFrame(const SpdySerializedFrame& frame,
                                            int num_chunks);

// Adds headers and values to a map.
// |extra_headers| is an array of { name, value } pairs, arranged as strings
// where the even entries are the header names, and the odd entries are the
// header values.
// |headers| gets filled in from |extra_headers|.
void AppendToHeaderBlock(const char* const extra_headers[],
                         int extra_header_count,
                         SpdyHeaderBlock* headers);

// Create an async MockWrite from the given SpdySerializedFrame.
MockWrite CreateMockWrite(const SpdySerializedFrame& req);

// Create an async MockWrite from the given SpdySerializedFrame and sequence
// number.
MockWrite CreateMockWrite(const SpdySerializedFrame& req, int seq);

MockWrite CreateMockWrite(const SpdySerializedFrame& req, int seq, IoMode mode);

// Create a MockRead from the given SpdySerializedFrame.
MockRead CreateMockRead(const SpdySerializedFrame& resp);

// Create a MockRead from the given SpdySerializedFrame and sequence number.
MockRead CreateMockRead(const SpdySerializedFrame& resp, int seq);

MockRead CreateMockRead(const SpdySerializedFrame& resp, int seq, IoMode mode);

// Combines the given vector of SpdySerializedFrame into a single frame.
SpdySerializedFrame CombineFrames(
    std::vector<const SpdySerializedFrame*> frames);

// Returns the SpdyPriority embedded in the given frame.  Returns true
// and fills in |priority| on success.
bool GetSpdyPriority(const SpdySerializedFrame& frame, SpdyPriority* priority);

// Tries to create a stream in |session| synchronously. Returns NULL
// on failure.
base::WeakPtr<SpdyStream> CreateStreamSynchronously(
    SpdyStreamType type,
    const base::WeakPtr<SpdySession>& session,
    const GURL& url,
    RequestPriority priority,
    const NetLogWithSource& net_log);

// Helper class used by some tests to release a stream as soon as it's
// created.
class StreamReleaserCallback : public TestCompletionCallbackBase {
 public:
  StreamReleaserCallback();

  ~StreamReleaserCallback() override;

  // Returns a callback that releases |request|'s stream.
  CompletionCallback MakeCallback(SpdyStreamRequest* request);

 private:
  void OnComplete(SpdyStreamRequest* request, int result);
};

// This struct holds information used to construct spdy control and data frames.
struct SpdyHeaderInfo {
  SpdyFrameType kind;
  SpdyStreamId id;
  SpdyStreamId assoc_id;
  SpdyPriority priority;
  int weight;
  SpdyControlFlags control_flags;
  SpdyErrorCode error_code;
  const char* data;
  uint32_t data_length;
  SpdyDataFlags data_flags;
};

// An ECSignatureCreator that returns deterministic signatures.
class MockECSignatureCreator : public crypto::ECSignatureCreator {
 public:
  explicit MockECSignatureCreator(crypto::ECPrivateKey* key);

  // crypto::ECSignatureCreator
  bool Sign(const uint8_t* data,
            int data_len,
            std::vector<uint8_t>* signature) override;
  bool DecodeSignature(const std::vector<uint8_t>& signature,
                       std::vector<uint8_t>* out_raw_sig) override;

 private:
  crypto::ECPrivateKey* key_;

  DISALLOW_COPY_AND_ASSIGN(MockECSignatureCreator);
};

// An ECSignatureCreatorFactory creates MockECSignatureCreator.
class MockECSignatureCreatorFactory : public crypto::ECSignatureCreatorFactory {
 public:
  MockECSignatureCreatorFactory();
  ~MockECSignatureCreatorFactory() override;

  // crypto::ECSignatureCreatorFactory
  std::unique_ptr<crypto::ECSignatureCreator> Create(
      crypto::ECPrivateKey* key) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockECSignatureCreatorFactory);
};

// Helper to manage the lifetimes of the dependencies for a
// HttpNetworkTransaction.
struct SpdySessionDependencies {
  // Default set of dependencies -- "null" proxy service.
  SpdySessionDependencies();

  // Custom proxy service dependency.
  explicit SpdySessionDependencies(std::unique_ptr<ProxyService> proxy_service);

  ~SpdySessionDependencies();

  static std::unique_ptr<HttpNetworkSession> SpdyCreateSession(
      SpdySessionDependencies* session_deps);

  // Variant that ignores session_deps->socket_factory, and uses the passed in
  // |factory| instead.
  static std::unique_ptr<HttpNetworkSession> SpdyCreateSessionWithSocketFactory(
      SpdySessionDependencies* session_deps,
      ClientSocketFactory* factory);
  static HttpNetworkSession::Params CreateSessionParams(
      SpdySessionDependencies* session_deps);
  static HttpNetworkSession::Context CreateSessionContext(
      SpdySessionDependencies* session_deps);

  // NOTE: host_resolver must be ordered before http_auth_handler_factory.
  std::unique_ptr<MockHostResolverBase> host_resolver;
  std::unique_ptr<CertVerifier> cert_verifier;
  std::unique_ptr<ChannelIDService> channel_id_service;
  std::unique_ptr<TransportSecurityState> transport_security_state;
  std::unique_ptr<CTVerifier> cert_transparency_verifier;
  std::unique_ptr<CTPolicyEnforcer> ct_policy_enforcer;
  std::unique_ptr<ProxyService> proxy_service;
  scoped_refptr<SSLConfigService> ssl_config_service;
  std::unique_ptr<MockClientSocketFactory> socket_factory;
  std::unique_ptr<HttpAuthHandlerFactory> http_auth_handler_factory;
  std::unique_ptr<HttpServerPropertiesImpl> http_server_properties;
  bool enable_ip_pooling;
  bool enable_ping;
  bool enable_user_alternate_protocol_ports;
  bool enable_quic;
  bool enable_server_push_cancellation;
  size_t session_max_recv_window_size;
  SettingsMap http2_settings;
  SpdySession::TimeFunc time_func;
  std::unique_ptr<ProxyDelegate> proxy_delegate;
  bool enable_http2_alternative_service;
  NetLog* net_log;
  bool http_09_on_non_default_ports_enabled;
};

class SpdyURLRequestContext : public URLRequestContext {
 public:
  SpdyURLRequestContext();
  ~SpdyURLRequestContext() override;

  MockClientSocketFactory& socket_factory() { return socket_factory_; }

 private:
  MockClientSocketFactory socket_factory_;
  URLRequestContextStorage storage_;
};

// Equivalent to pool->GetIfExists(spdy_session_key, NetLogWithSource()) !=
// NULL.
bool HasSpdySession(SpdySessionPool* pool, const SpdySessionKey& key);

// Tries to create a SPDY session for the given key but expects the
// attempt to fail with the given error. A SPDY session for |key| must
// not already exist. The session will be created but close in the
// next event loop iteration.
base::WeakPtr<SpdySession> TryCreateSpdySessionExpectingFailure(
    HttpNetworkSession* http_session,
    const SpdySessionKey& key,
    Error expected_error,
    const NetLogWithSource& net_log);

// Creates a SPDY session for the given key and puts it in the SPDY
// session pool in |http_session|. A SPDY session for |key| must not
// already exist.
base::WeakPtr<SpdySession> CreateSpdySession(HttpNetworkSession* http_session,
                                             const SpdySessionKey& key,
                                             const NetLogWithSource& net_log);

// Like CreateSpdySession(), but does not fail if there is already an IP
// pooled session for |key|.
base::WeakPtr<SpdySession> CreateSpdySessionWithIpBasedPoolingDisabled(
    HttpNetworkSession* http_session,
    const SpdySessionKey& key,
    const NetLogWithSource& net_log);

// Creates an insecure SPDY session for the given key and puts it in
// |pool|. The returned session will neither receive nor send any
// data. A SPDY session for |key| must not already exist.
base::WeakPtr<SpdySession> CreateFakeSpdySession(SpdySessionPool* pool,
                                                 const SpdySessionKey& key);

// Tries to create an insecure SPDY session for the given key but
// expects the attempt to fail with the given error. The session will
// neither receive nor send any data. A SPDY session for |key| must
// not already exist. The session will be created but close in the
// next event loop iteration.
base::WeakPtr<SpdySession> TryCreateFakeSpdySessionExpectingFailure(
    SpdySessionPool* pool,
    const SpdySessionKey& key,
    Error expected_error);

class SpdySessionPoolPeer {
 public:
  explicit SpdySessionPoolPeer(SpdySessionPool* pool);

  void RemoveAliases(const SpdySessionKey& key);
  void SetEnableSendingInitialData(bool enabled);

 private:
  SpdySessionPool* const pool_;

  DISALLOW_COPY_AND_ASSIGN(SpdySessionPoolPeer);
};

class SpdyTestUtil {
 public:
  SpdyTestUtil();
  ~SpdyTestUtil();

  // Add the appropriate headers to put |url| into |block|.
  void AddUrlToHeaderBlock(SpdyStringPiece url, SpdyHeaderBlock* headers) const;

  static SpdyHeaderBlock ConstructGetHeaderBlock(SpdyStringPiece url);
  static SpdyHeaderBlock ConstructGetHeaderBlockForProxy(SpdyStringPiece url);
  static SpdyHeaderBlock ConstructHeadHeaderBlock(SpdyStringPiece url,
                                                  int64_t content_length);
  static SpdyHeaderBlock ConstructPostHeaderBlock(SpdyStringPiece url,
                                                  int64_t content_length);
  static SpdyHeaderBlock ConstructPutHeaderBlock(SpdyStringPiece url,
                                                 int64_t content_length);

  // Construct an expected SPDY reply string from the given headers.
  SpdyString ConstructSpdyReplyString(const SpdyHeaderBlock& headers) const;

  // Construct an expected SPDY SETTINGS frame.
  // |settings| are the settings to set.
  // Returns the constructed frame.  The caller takes ownership of the frame.
  SpdySerializedFrame ConstructSpdySettings(const SettingsMap& settings);

  // Constructs an expected SPDY SETTINGS acknowledgement frame.
  SpdySerializedFrame ConstructSpdySettingsAck();

  // Construct a SPDY PING frame.
  // Returns the constructed frame.  The caller takes ownership of the frame.
  SpdySerializedFrame ConstructSpdyPing(uint32_t ping_id, bool is_ack);

  // Construct a SPDY GOAWAY frame with last_good_stream_id = 0.
  // Returns the constructed frame.  The caller takes ownership of the frame.
  SpdySerializedFrame ConstructSpdyGoAway();

  // Construct a SPDY GOAWAY frame with the specified last_good_stream_id.
  // Returns the constructed frame.  The caller takes ownership of the frame.
  SpdySerializedFrame ConstructSpdyGoAway(SpdyStreamId last_good_stream_id);

  // Construct a SPDY GOAWAY frame with the specified last_good_stream_id,
  // status, and description. Returns the constructed frame. The caller takes
  // ownership of the frame.
  SpdySerializedFrame ConstructSpdyGoAway(SpdyStreamId last_good_stream_id,
                                          SpdyErrorCode error_code,
                                          const SpdyString& desc);

  // Construct a SPDY WINDOW_UPDATE frame.
  // Returns the constructed frame.  The caller takes ownership of the frame.
  SpdySerializedFrame ConstructSpdyWindowUpdate(SpdyStreamId stream_id,
                                                uint32_t delta_window_size);

  // Construct a SPDY RST_STREAM frame.
  // Returns the constructed frame.  The caller takes ownership of the frame.
  SpdySerializedFrame ConstructSpdyRstStream(SpdyStreamId stream_id,
                                             SpdyErrorCode error_code);

  // Construct a PRIORITY frame. The weight is derived from |request_priority|.
  // Returns the constructed frame.  The caller takes ownership of the frame.
  SpdySerializedFrame ConstructSpdyPriority(SpdyStreamId stream_id,
                                            SpdyStreamId parent_stream_id,
                                            RequestPriority request_priority,
                                            bool exclusive);

  // Constructs a standard SPDY GET HEADERS frame for |url| with header
  // compression.
  // |extra_headers| are the extra header-value pairs, which typically
  // will vary the most between calls.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyGet(const char* const url,
                                       SpdyStreamId stream_id,
                                       RequestPriority request_priority);

  // Constructs a standard SPDY GET HEADERS frame with header compression.
  // |extra_headers| are the extra header-value pairs, which typically
  // will vary the most between calls.  If |direct| is false, the
  // the full url will be used instead of simply the path.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyGet(const char* const extra_headers[],
                                       int extra_header_count,
                                       int stream_id,
                                       RequestPriority request_priority,
                                       bool direct);

  // Constructs a SPDY HEADERS frame for a CONNECT request.
  SpdySerializedFrame ConstructSpdyConnect(const char* const extra_headers[],
                                           int extra_header_count,
                                           int stream_id,
                                           RequestPriority priority,
                                           const HostPortPair& host_port_pair);

  // Constructs a SPDY PUSH_PROMISE frame.
  // |extra_headers| are the extra header-value pairs, which typically
  // will vary the most between calls.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyPush(const char* const extra_headers[],
                                        int extra_header_count,
                                        int stream_id,
                                        int associated_stream_id,
                                        const char* url);
  SpdySerializedFrame ConstructSpdyPush(const char* const extra_headers[],
                                        int extra_header_count,
                                        int stream_id,
                                        int associated_stream_id,
                                        const char* url,
                                        const char* status,
                                        const char* location);

  SpdySerializedFrame ConstructInitialSpdyPushFrame(SpdyHeaderBlock headers,
                                                    int stream_id,
                                                    int associated_stream_id);

  SpdySerializedFrame ConstructSpdyPushHeaders(
      int stream_id,
      const char* const extra_headers[],
      int extra_header_count);

  // Constructs a HEADERS frame with the request header compression context with
  // END_STREAM flag set to |fin|.
  SpdySerializedFrame ConstructSpdyResponseHeaders(int stream_id,
                                                   SpdyHeaderBlock headers,
                                                   bool fin);

  // Construct a HEADERS frame carrying exactly the given headers and priority.
  SpdySerializedFrame ConstructSpdyHeaders(int stream_id,
                                           SpdyHeaderBlock headers,
                                           RequestPriority priority,
                                           bool fin);

  // Construct a reply HEADERS frame carrying exactly the given headers and the
  // default priority.
  SpdySerializedFrame ConstructSpdyReply(int stream_id,
                                         SpdyHeaderBlock headers);

  // Constructs a standard SPDY HEADERS frame to match the SPDY GET.
  // |extra_headers| are the extra header-value pairs, which typically
  // will vary the most between calls.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyGetReply(const char* const extra_headers[],
                                            int extra_header_count,
                                            int stream_id);

  // Constructs a standard SPDY HEADERS frame with an Internal Server
  // Error status code.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyReplyError(int stream_id);

  // Constructs a standard SPDY HEADERS frame with the specified status code.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyReplyError(
      const char* const status,
      const char* const* const extra_headers,
      int extra_header_count,
      int stream_id);

  // Constructs a standard SPDY POST HEADERS frame.
  // |extra_headers| are the extra header-value pairs, which typically
  // will vary the most between calls.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyPost(const char* url,
                                        SpdyStreamId stream_id,
                                        int64_t content_length,
                                        RequestPriority priority,
                                        const char* const extra_headers[],
                                        int extra_header_count);

  // Constructs a chunked transfer SPDY POST HEADERS frame.
  // |extra_headers| are the extra header-value pairs, which typically
  // will vary the most between calls.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructChunkedSpdyPost(
      const char* const extra_headers[],
      int extra_header_count);

  // Constructs a standard SPDY HEADERS frame to match the SPDY POST.
  // |extra_headers| are the extra header-value pairs, which typically
  // will vary the most between calls.
  // Returns a SpdySerializedFrame.
  SpdySerializedFrame ConstructSpdyPostReply(const char* const extra_headers[],
                                             int extra_header_count);

  // Constructs a single SPDY data frame with the contents "hello!"
  SpdySerializedFrame ConstructSpdyDataFrame(int stream_id, bool fin);

  // Constructs a single SPDY data frame with the given content.
  SpdySerializedFrame ConstructSpdyDataFrame(int stream_id,
                                             const char* data,
                                             uint32_t len,
                                             bool fin);

  // Constructs a single SPDY data frame with the given content and padding.
  SpdySerializedFrame ConstructSpdyDataFrame(int stream_id,
                                             const char* data,
                                             uint32_t len,
                                             bool fin,
                                             int padding_length);

  // Wraps |frame| in the payload of a data frame in stream |stream_id|.
  SpdySerializedFrame ConstructWrappedSpdyFrame(
      const SpdySerializedFrame& frame,
      int stream_id);

  // Serialize a SpdyFrameIR with |headerless_spdy_framer_|.
  SpdySerializedFrame SerializeFrame(const SpdyFrameIR& frame_ir);

  // Called when necessary (when it will affect stream dependency specification
  // when setting dependencies based on priorioties) to notify the utility
  // class of stream destruction.
  void UpdateWithStreamDestruction(int stream_id);

  void set_default_url(const GURL& url) { default_url_ = url; }

 private:
  // |content_length| may be NULL, in which case the content-length
  // header will be omitted.
  static SpdyHeaderBlock ConstructHeaderBlock(SpdyStringPiece method,
                                              SpdyStringPiece url,
                                              int64_t* content_length);

  // Multiple SpdyFramers are required to keep track of header compression
  // state.
  // Use to serialize frames (request or response) without headers.
  SpdyFramer headerless_spdy_framer_;
  // Use to serialize request frames with headers.
  SpdyFramer request_spdy_framer_;
  // Use to serialize response frames with headers.
  SpdyFramer response_spdy_framer_;

  GURL default_url_;

  // Track a FIFO list of the stream_id of all created requests by priority.
  std::map<int, std::vector<int>> priority_to_stream_id_list_;
};

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_SPDY_TEST_UTIL_COMMON_H_
