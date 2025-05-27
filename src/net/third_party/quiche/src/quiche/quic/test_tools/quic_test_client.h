// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_CLIENT_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/tools/quic_default_client.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_linked_hash_map.h"

namespace quic {

class ProofVerifier;
class QuicPacketWriterWrapper;

namespace test {

class MockableQuicClientDefaultNetworkHelper
    : public QuicClientDefaultNetworkHelper {
 public:
  using QuicClientDefaultNetworkHelper::QuicClientDefaultNetworkHelper;
  ~MockableQuicClientDefaultNetworkHelper() override = default;

  void ProcessPacket(const QuicSocketAddress& self_address,
                     const QuicSocketAddress& peer_address,
                     const QuicReceivedPacket& packet) override;

  SocketFd CreateUDPSocket(QuicSocketAddress server_address,
                           bool* overflow_supported) override;

  QuicPacketWriter* CreateQuicPacketWriter() override;

  void set_socket_fd_configurator(
      quiche::MultiUseCallback<void(SocketFd)> socket_fd_configurator);

  const QuicReceivedPacket* last_incoming_packet();

  void set_track_last_incoming_packet(bool track);

  void UseWriter(QuicPacketWriterWrapper* writer);

  void set_peer_address(const QuicSocketAddress& address);

 private:
  QuicPacketWriterWrapper* test_writer_ = nullptr;
  // The last incoming packet, iff |track_last_incoming_packet_| is true.
  std::unique_ptr<QuicReceivedPacket> last_incoming_packet_;
  // If true, copy each packet from ProcessPacket into |last_incoming_packet_|
  bool track_last_incoming_packet_ = false;
  // If set, |socket_fd_configurator_| will be called after a socket fd is
  // created.
  quiche::MultiUseCallback<void(SocketFd)> socket_fd_configurator_;
};

// A quic client which allows mocking out reads and writes.
class MockableQuicClient : public QuicDefaultClient {
 public:
  MockableQuicClient(QuicSocketAddress server_address,
                     const QuicServerId& server_id,
                     const ParsedQuicVersionVector& supported_versions,
                     QuicEventLoop* event_loop);

  MockableQuicClient(QuicSocketAddress server_address,
                     const QuicServerId& server_id, const QuicConfig& config,
                     const ParsedQuicVersionVector& supported_versions,
                     QuicEventLoop* event_loop);

  MockableQuicClient(QuicSocketAddress server_address,
                     const QuicServerId& server_id, const QuicConfig& config,
                     const ParsedQuicVersionVector& supported_versions,
                     QuicEventLoop* event_loop,
                     std::unique_ptr<ProofVerifier> proof_verifier);

  MockableQuicClient(QuicSocketAddress server_address,
                     const QuicServerId& server_id, const QuicConfig& config,
                     const ParsedQuicVersionVector& supported_versions,
                     QuicEventLoop* event_loop,
                     std::unique_ptr<ProofVerifier> proof_verifier,
                     std::unique_ptr<SessionCache> session_cache);
  MockableQuicClient(const MockableQuicClient&) = delete;
  MockableQuicClient& operator=(const MockableQuicClient&) = delete;

  ~MockableQuicClient() override;

  QuicConnectionId GetClientConnectionId() override;
  void UseClientConnectionId(QuicConnectionId client_connection_id);
  void UseClientConnectionIdLength(int client_connection_id_length);

  void UseWriter(QuicPacketWriterWrapper* writer);
  void set_peer_address(const QuicSocketAddress& address);
  // The last incoming packet, iff |track_last_incoming_packet| is true.
  const QuicReceivedPacket* last_incoming_packet();
  // If true, copy each packet from ProcessPacket into |last_incoming_packet|
  void set_track_last_incoming_packet(bool track);

  // Casts the network helper to a MockableQuicClientDefaultNetworkHelper.
  MockableQuicClientDefaultNetworkHelper* mockable_network_helper();
  const MockableQuicClientDefaultNetworkHelper* mockable_network_helper() const;

 private:
  // Client connection ID to use, if client_connection_id_overridden_.
  // TODO(wub): Move client_connection_id_(length_) overrides to QuicClientBase.
  QuicConnectionId override_client_connection_id_;
  bool client_connection_id_overridden_;
  int override_client_connection_id_length_ = -1;
  CachedNetworkParameters cached_network_paramaters_;
};

// A toy QUIC client used for testing.
class QuicTestClient : public QuicSpdyStream::Visitor {
 public:
  QuicTestClient(QuicSocketAddress server_address,
                 const std::string& server_hostname,
                 const ParsedQuicVersionVector& supported_versions);
  QuicTestClient(QuicSocketAddress server_address,
                 const std::string& server_hostname, const QuicConfig& config,
                 const ParsedQuicVersionVector& supported_versions);
  QuicTestClient(QuicSocketAddress server_address,
                 const std::string& server_hostname, const QuicConfig& config,
                 const ParsedQuicVersionVector& supported_versions,
                 std::unique_ptr<ProofVerifier> proof_verifier);
  QuicTestClient(QuicSocketAddress server_address,
                 const std::string& server_hostname, const QuicConfig& config,
                 const ParsedQuicVersionVector& supported_versions,
                 std::unique_ptr<ProofVerifier> proof_verifier,
                 std::unique_ptr<SessionCache> session_cache);
  QuicTestClient(QuicSocketAddress server_address,
                 const std::string& server_hostname, const QuicConfig& config,
                 const ParsedQuicVersionVector& supported_versions,
                 std::unique_ptr<ProofVerifier> proof_verifier,
                 std::unique_ptr<SessionCache> session_cache,
                 std::unique_ptr<QuicEventLoop> event_loop);

  ~QuicTestClient() override;

  // Sets the |user_agent_id| of the |client_|.
  void SetUserAgentID(const std::string& user_agent_id);

  // Sets the preferred TLS key exchange groups of the |client_|.
  void SetPreferredGroups(const std::vector<uint16_t>& preferred_groups);

  // Wraps data in a quic packet and sends it.
  int64_t SendData(const std::string& data, bool last_data);
  // As above, but |delegate| will be notified when |data| is ACKed.
  int64_t SendData(
      const std::string& data, bool last_data,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  // Clears any outstanding state and sends a simple GET of 'uri' to the
  // server.  Returns 0 if the request failed and no bytes were written.
  int64_t SendRequest(const std::string& uri);
  // Send a request R and a RST_FRAME which resets R, in the same packet.
  int64_t SendRequestAndRstTogether(const std::string& uri);
  // Sends requests for all the urls and waits for the responses.  To process
  // the individual responses as they are returned, the caller should use the
  // set the response_listener on the client().
  void SendRequestsAndWaitForResponses(
      const std::vector<std::string>& url_list);
  // Sends a request containing |headers| and |body| and returns the number of
  // bytes sent (the size of the serialized request headers and body).
  int64_t SendMessage(const quiche::HttpHeaderBlock& headers,
                      absl::string_view body);
  // Sends a request containing |headers| and |body| with the fin bit set to
  // |fin| and returns the number of bytes sent (the size of the serialized
  // request headers and body).
  int64_t SendMessage(const quiche::HttpHeaderBlock& headers,
                      absl::string_view body, bool fin);
  // Sends a request containing |headers| and |body| with the fin bit set to
  // |fin| and returns the number of bytes sent (the size of the serialized
  // request headers and body). If |flush| is true, will wait for the message to
  // be flushed before returning.
  int64_t SendMessage(const quiche::HttpHeaderBlock& headers,
                      absl::string_view body, bool fin, bool flush);
  // Sends a request containing |headers| and |body|, waits for the response,
  // and returns the response body.
  std::string SendCustomSynchronousRequest(
      const quiche::HttpHeaderBlock& headers, const std::string& body);
  // Sends a GET request for |uri|, waits for the response, and returns the
  // response body.
  std::string SendSynchronousRequest(const std::string& uri);
  void SendConnectivityProbing();
  void Connect();
  void ResetConnection();
  void Disconnect();
  QuicSocketAddress local_address() const;
  void ClearPerRequestState();
  bool WaitUntil(int timeout_ms,
                 std::optional<quiche::UnretainedCallback<bool()>> trigger);
  int64_t Send(absl::string_view data);
  bool connected() const;
  bool buffer_body() const;
  void set_buffer_body(bool buffer_body);

  // Getters for stream state that only get updated once a complete response is
  // received.
  const quiche::HttpHeaderBlock& response_trailers() const;
  bool response_complete() const;
  int64_t response_body_size() const;
  const std::string& response_body() const;
  // Getters for stream state that return state of the oldest active stream that
  // have received a partial response.
  bool response_headers_complete() const;
  const quiche::HttpHeaderBlock* response_headers() const;
  int64_t response_size() const;
  size_t bytes_read() const;
  size_t bytes_written() const;

  // Returns response body received so far by the stream that has been most
  // recently opened among currently open streams.  To query response body
  // received by a stream that is already closed, use `response_body()` instead.
  absl::string_view partial_response_body() const;

  // Returns once at least one complete response or a connection close has been
  // received from the server. If responses are received for multiple (say 2)
  // streams, next WaitForResponse will return immediately.
  void WaitForResponse() { WaitForResponseForMs(-1); }

  // Returns once some data is received on any open streams or at least one
  // complete response is received from the server.
  void WaitForInitialResponse() { WaitForInitialResponseForMs(-1); }

  // Returns once at least one complete response or a connection close has been
  // received from the server, or once the timeout expires.
  // Passing in a timeout value of -1 disables the timeout. If multiple
  // responses are received while the client is waiting, subsequent calls to
  // this function will return immediately.
  void WaitForResponseForMs(int timeout_ms) {
    WaitUntil(timeout_ms, [this]() {
      return !HaveActiveStream() || !closed_stream_states_.empty();
    });
    if (response_complete()) {
      QUIC_VLOG(1) << "Client received response:"
                   << response_headers()->DebugString() << response_body();
    }
  }

  // Returns once a goaway a connection close has been
  // received from the server, or once the timeout expires.
  // Passing in a timeout value of -1 disables the timeout.
  void WaitForGoAway(int timeout_ms) {
    WaitUntil(timeout_ms, [this]() { return client()->goaway_received(); });
  }

  // Returns once some data is received on any open streams or at least one
  // complete response is received from the server, or once the timeout
  // expires. -1 means no timeout.
  void WaitForInitialResponseForMs(int timeout_ms) {
    WaitUntil(timeout_ms,
              [this]() { return !HaveActiveStream() || response_size() != 0; });
  }

  // Migrate local address to <|new_host|, a random port>.
  // Return whether the migration succeeded.
  bool MigrateSocket(const QuicIpAddress& new_host);
  // Migrate local address to <|new_host|, |port|>.
  // Return whether the migration succeeded.
  bool MigrateSocketWithSpecifiedPort(const QuicIpAddress& new_host, int port);
  QuicIpAddress bind_to_address() const;
  void set_bind_to_address(QuicIpAddress address);
  const QuicSocketAddress& address() const;

  // From QuicSpdyStream::Visitor
  void OnClose(QuicSpdyStream* stream) override;

  // Configures client_ to take ownership of and use the writer.
  // Must be called before initial connect.
  void UseWriter(QuicPacketWriterWrapper* writer);
  // Configures client_ to use a specific server connection ID instead of a
  // random one.
  void UseConnectionId(QuicConnectionId server_connection_id);
  // Configures client_ to use a specific server connection ID length instead
  // of the default of kQuicDefaultConnectionIdLength.
  void UseConnectionIdLength(uint8_t server_connection_id_length);
  // Configures client_ to use a specific client connection ID instead of an
  // empty one.
  void UseClientConnectionId(QuicConnectionId client_connection_id);
  // Configures client_ to use a specific client connection ID length instead
  // of the default of zero.
  void UseClientConnectionIdLength(uint8_t client_connection_id_length);

  // Returns nullptr if the maximum number of streams have already been created.
  QuicSpdyClientStream* GetOrCreateStream();

  // Calls GetOrCreateStream(), sends the request on the stream, and
  // stores the request in case it needs to be resent.  If |headers| is
  // null, only the body will be sent on the stream.
  int64_t GetOrCreateStreamAndSendRequest(
      const quiche::HttpHeaderBlock* headers, absl::string_view body, bool fin,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  QuicRstStreamErrorCode stream_error() { return stream_error_; }
  QuicErrorCode connection_error() const;

  MockableQuicClient* client() { return client_.get(); }
  const MockableQuicClient* client() const { return client_.get(); }

  // cert_common_name returns the common name value of the server's certificate,
  // or the empty std::string if no certificate was presented.
  const std::string& cert_common_name() const;

  // cert_sct returns the signed timestamp of the server's certificate,
  // or the empty std::string if no signed timestamp was presented.
  const std::string& cert_sct() const;

  // Get the server config map.  Server config must exist.
  const QuicTagValueMap& GetServerConfig() const;

  void set_auto_reconnect(bool reconnect) { auto_reconnect_ = reconnect; }

  void set_priority(spdy::SpdyPriority priority) { priority_ = priority; }

  void WaitForWriteToFlush();

  QuicEventLoop* event_loop() { return event_loop_.get(); }

  size_t num_requests() const { return num_requests_; }

  size_t num_responses() const { return num_responses_; }

  void set_server_address(const QuicSocketAddress& server_address) {
    client_->set_server_address(server_address);
  }

  void set_peer_address(const QuicSocketAddress& address) {
    client_->set_peer_address(address);
  }

  // Explicitly set the SNI value for this client, overriding the default
  // behavior which extracts the SNI value from the request URL.
  void OverrideSni(const std::string& sni) {
    override_sni_set_ = true;
    override_sni_ = sni;
  }

  void Initialize();

  void set_client(std::unique_ptr<MockableQuicClient> client) {
    client_ = std::move(client);
  }

  // Given |uri|, populates the fields in |headers| for a simple GET
  // request. If |uri| is a relative URL, the QuicServerId will be
  // use to specify the authority.
  bool PopulateHeaderBlockFromUrl(const std::string& uri,
                                  quiche::HttpHeaderBlock* headers);

  // Waits for a period of time that is long enough to receive all delayed acks
  // sent by peer.
  void WaitForDelayedAcks();

  QuicSpdyClientStream* latest_created_stream() {
    return latest_created_stream_;
  }

 protected:
  QuicTestClient();
  QuicTestClient(const QuicTestClient&) = delete;
  QuicTestClient(const QuicTestClient&&) = delete;
  QuicTestClient& operator=(const QuicTestClient&) = delete;
  QuicTestClient& operator=(const QuicTestClient&&) = delete;

 private:
  // PerStreamState of a stream is updated when it is closed.
  struct PerStreamState {
    PerStreamState(const PerStreamState& other);
    PerStreamState(QuicRstStreamErrorCode stream_error, bool response_complete,
                   bool response_headers_complete,
                   const quiche::HttpHeaderBlock& response_headers,
                   const std::string& response,
                   const quiche::HttpHeaderBlock& response_trailers,
                   uint64_t bytes_read, uint64_t bytes_written,
                   int64_t response_body_size);
    ~PerStreamState();

    QuicRstStreamErrorCode stream_error;
    bool response_complete;
    bool response_headers_complete;
    quiche::HttpHeaderBlock response_headers;
    std::string response;
    quiche::HttpHeaderBlock response_trailers;
    uint64_t bytes_read;
    uint64_t bytes_written;
    int64_t response_body_size;
  };

  bool HaveActiveStream();

  // Read oldest received response and remove it from closed_stream_states_.
  void ReadNextResponse();

  // Clear open_streams_, closed_stream_states_ and reset
  // latest_created_stream_.
  void ClearPerConnectionState();

  // Update latest_created_stream_, add |stream| to open_streams_ and starts
  // tracking its state.
  void SetLatestCreatedStream(QuicSpdyClientStream* stream);

  std::unique_ptr<QuicEventLoop> event_loop_;
  std::unique_ptr<MockableQuicClient> client_;  // The actual client
  QuicSpdyClientStream* latest_created_stream_;
  std::map<QuicStreamId, QuicSpdyClientStream*> open_streams_;
  // Received responses of closed streams.
  quiche::QuicheLinkedHashMap<QuicStreamId, PerStreamState>
      closed_stream_states_;

  QuicRstStreamErrorCode stream_error_;

  bool response_complete_;
  bool response_headers_complete_;
  mutable quiche::HttpHeaderBlock response_headers_;

  // Parsed response trailers (if present), copied from the stream in OnClose.
  quiche::HttpHeaderBlock response_trailers_;

  spdy::SpdyPriority priority_;
  std::string response_;
  // bytes_read_ and bytes_written_ are updated only when stream_ is released;
  // prefer bytes_read() and bytes_written() member functions.
  uint64_t bytes_read_;
  uint64_t bytes_written_;
  // The number of HTTP body bytes received.
  int64_t response_body_size_;
  // True if we tried to connect already since the last call to Disconnect().
  bool connect_attempted_;
  // The client will auto-connect exactly once before sending data.  If
  // something causes a connection reset, it will not automatically reconnect
  // unless auto_reconnect_ is true.
  bool auto_reconnect_;
  // Should we buffer the response body? Defaults to true.
  bool buffer_body_;
  // Number of requests/responses this client has sent/received.
  size_t num_requests_;
  size_t num_responses_;

  // If set, this value is used for the connection SNI, overriding the usual
  // logic which extracts the SNI from the request URL.
  bool override_sni_set_ = false;
  std::string override_sni_;
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_CLIENT_H_
