// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A client specific QuicSession subclass.  This class owns the underlying
// QuicConnection and QuicConnectionHelper objects.  The connection stores
// a non-owning pointer to the helper so this session needs to ensure that
// the helper outlives the connection.

#ifndef NET_QUIC_CHROMIUM_QUIC_CHROMIUM_CLIENT_SESSION_H_
#define NET_QUIC_CHROMIUM_QUIC_CHROMIUM_CLIENT_SESSION_H_

#include <stddef.h>

#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/containers/mru_cache.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "net/base/completion_callback.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_error_details.h"
#include "net/base/net_export.h"
#include "net/cert/ct_verify_result.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy/proxy_server.h"
#include "net/quic/chromium/quic_chromium_client_stream.h"
#include "net/quic/chromium/quic_chromium_packet_reader.h"
#include "net/quic/chromium/quic_chromium_packet_writer.h"
#include "net/quic/chromium/quic_connection_logger.h"
#include "net/quic/core/quic_client_push_promise_index.h"
#include "net/quic/core/quic_crypto_client_stream.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/core/quic_spdy_client_session_base.h"
#include "net/quic/core/quic_time.h"
#include "net/socket/socket_performance_watcher.h"
#include "net/spdy/chromium/multiplexed_session.h"
#include "net/spdy/chromium/server_push_delegate.h"

namespace net {

class CertVerifyResult;
class DatagramClientSocket;
class NetLog;
class QuicCryptoClientStreamFactory;
class QuicServerInfo;
class QuicStreamFactory;
class SSLInfo;
class TransportSecurityState;

using TokenBindingSignatureMap =
    base::MRUCache<std::pair<TokenBindingType, std::string>,
                   std::vector<uint8_t>>;

namespace test {
class QuicChromiumClientSessionPeer;
}  // namespace test

class NET_EXPORT_PRIVATE QuicChromiumClientSession
    : public QuicSpdyClientSessionBase,
      public MultiplexedSession,
      public QuicChromiumPacketReader::Visitor,
      public QuicChromiumPacketWriter::Delegate {
 public:
  class StreamRequest;

  // Wrapper for interacting with the session in a restricted fashion which
  // hides the details of the underlying session's lifetime. All methods of
  // the Handle are safe to use even after the underlying session is destroyed.
  class NET_EXPORT_PRIVATE Handle
      : public MultiplexedSessionHandle,
        public QuicClientPushPromiseIndex::Delegate {
   public:
    explicit Handle(const base::WeakPtr<QuicChromiumClientSession>& session);
    Handle(const Handle& other) = delete;
    ~Handle() override;

    // Returns true if the session is still connected.
    bool IsConnected() const;

    // Returns true if the handshake has been confirmed.
    bool IsCryptoHandshakeConfirmed() const;

    // Starts a request to rendezvous with a promised a stream.  If OK is
    // returned, then |push_stream_| will be updated with the promised
    // stream.  If ERR_IO_PENDING is returned, then when the rendezvous is
    // eventually completed |callback| will be called.
    int RendezvousWithPromised(const SpdyHeaderBlock& headers,
                               const CompletionCallback& callback);

    // Starts a request to create a stream.  If OK is returned, then
    // |stream_| will be updated with the newly created stream.  If
    // ERR_IO_PENDING is returned, then when the request is eventuallly
    // complete |callback| will be called.
    int RequestStream(bool requires_confirmation,
                      const CompletionCallback& callback);

    // Releases |stream_| to the caller. Returns nullptr if the underlying
    // QuicChromiumClientSession is closed.
    std::unique_ptr<QuicChromiumClientStream::Handle> ReleaseStream();

    // Releases |push_stream_| to the caller.
    std::unique_ptr<QuicChromiumClientStream::Handle> ReleasePromisedStream();

    // Sends Rst for the stream, and makes sure that future calls to
    // IsClosedStream(id) return true, which ensures that any subsequent
    // frames related to this stream will be ignored (modulo flow
    // control accounting).
    void ResetPromised(QuicStreamId id, QuicRstStreamErrorCode error_code);

    // Returns a new packet bundler while will cause writes to be batched up
    // until a packet is full, or the last bundler is destroyed.
    std::unique_ptr<QuicConnection::ScopedPacketBundler> CreatePacketBundler(
        QuicConnection::AckBundling bundling_mode);

    // Populates network error details for this session.
    void PopulateNetErrorDetails(NetErrorDetails* details) const;

    // Returns the connection timing for the handshake of this session.
    const LoadTimingInfo::ConnectTiming& GetConnectTiming();

    // Signs the exported keying material used for Token Binding using key
    // |*key| and puts the signature in |*out|. Returns a net error code.
    Error GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                   TokenBindingType tb_type,
                                   std::vector<uint8_t>* out);

    // Returns true if |other| is a handle to the same session as this handle.
    bool SharesSameSession(const Handle& other) const;

    // Returns the QUIC version used by the session.
    QuicTransportVersion GetQuicVersion() const;

    // Copies the remote udp address into |address| and returns a net error
    // code.
    int GetPeerAddress(IPEndPoint* address) const;

    // Copies the local udp address into |address| and returns a net error
    // code.
    int GetSelfAddress(IPEndPoint* address) const;

    // Returns the push promise index associated with the session.
    QuicClientPushPromiseIndex* GetPushPromiseIndex();

    // Returns the session's server ID.
    QuicServerId server_id() const { return server_id_; }

    // Returns the session's net log.
    const NetLogWithSource& net_log() const { return net_log_; }

    // QuicClientPushPromiseIndex::Delegate implementation
    bool CheckVary(const SpdyHeaderBlock& client_request,
                   const SpdyHeaderBlock& promise_request,
                   const SpdyHeaderBlock& promise_response) override;
    void OnRendezvousResult(QuicSpdyStream* stream) override;

    // Returns true if the session's connection has sent or received any bytes.
    bool WasEverUsed() const;

   private:
    friend class QuicChromiumClientSession;
    friend class QuicChromiumClientSession::StreamRequest;

    // Waits for the handshake to be confirmed and invokes |callback| when
    // that happens. If the handshake has already been confirmed, returns OK.
    // If the connection has already been closed, returns a net error. If the
    // connection closes before the handshake is confirmed, |callback| will
    // be invoked with an error.
    int WaitForHandshakeConfirmation(const CompletionCallback& callback);

    // Called when the handshake is confirmed.
    void OnCryptoHandshakeConfirmed();

    // Called when the session is closed with a net error.
    void OnSessionClosed(QuicTransportVersion quic_version,
                         int net_error,
                         QuicErrorCode quic_error,
                         bool port_migration_detected,
                         LoadTimingInfo::ConnectTiming connect_timing,
                         bool was_ever_used);

    // Called by |request| to create a stream.
    int TryCreateStream(StreamRequest* request);

    // Called by |request| to cancel stream request.
    void CancelRequest(StreamRequest* request);

    // Underlying session which may be destroyed before this handle.
    base::WeakPtr<QuicChromiumClientSession> session_;

    // Stream request created by |RequestStream()|.
    std::unique_ptr<StreamRequest> stream_request_;

    // Information saved from the session which can be used even after the
    // session is destroyed.
    NetLogWithSource net_log_;
    bool was_handshake_confirmed_;
    int net_error_;
    QuicErrorCode quic_error_;
    bool port_migration_detected_;
    QuicServerId server_id_;
    QuicTransportVersion quic_version_;
    LoadTimingInfo::ConnectTiming connect_timing_;
    QuicClientPushPromiseIndex* push_promise_index_;

    // |QuicClientPromisedInfo| owns this. It will be set when |Try()|
    // is asynchronous, i.e. it returned QUIC_PENDING, and remains valid
    // until |OnRendezvouResult()| fires or |push_handle_->Cancel()| is
    // invoked.
    QuicClientPushPromiseIndex::TryHandle* push_handle_;
    CompletionCallback push_callback_;
    std::unique_ptr<QuicChromiumClientStream::Handle> push_stream_;

    bool was_ever_used_;
  };

  // A helper class used to manage a request to create a stream.
  class NET_EXPORT_PRIVATE StreamRequest {
   public:
    // Cancels any pending stream creation request and resets |stream_| if
    // it has not yet been released.
    ~StreamRequest();

    // Starts a request to create a stream.  If OK is returned, then
    // |stream_| will be updated with the newly created stream.  If
    // ERR_IO_PENDING is returned, then when the request is eventuallly
    // complete |callback| will be called.
    int StartRequest(const CompletionCallback& callback);

    // Releases |stream_| to the caller.
    std::unique_ptr<QuicChromiumClientStream::Handle> ReleaseStream();

   private:
    friend class QuicChromiumClientSession;

    enum State {
      STATE_NONE,
      STATE_WAIT_FOR_CONFIRMATION,
      STATE_WAIT_FOR_CONFIRMATION_COMPLETE,
      STATE_REQUEST_STREAM,
      STATE_REQUEST_STREAM_COMPLETE,
    };

    // |session| must outlive this request.
    StreamRequest(QuicChromiumClientSession::Handle* session,
                  bool requires_confirmation);

    void OnIOComplete(int rv);
    void DoCallback(int rv);

    int DoLoop(int rv);
    int DoWaitForConfirmation();
    int DoWaitForConfirmationComplete(int rv);
    int DoRequestStream();
    int DoRequestStreamComplete(int rv);

    // Called by |session_| for an asynchronous request when the stream
    // request has finished successfully.
    void OnRequestCompleteSuccess(
        std::unique_ptr<QuicChromiumClientStream::Handle> stream);

    // Called by |session_| for an asynchronous request when the stream
    // request has finished with an error. Also called with ERR_ABORTED
    // if |session_| is destroyed while the stream request is still pending.
    void OnRequestCompleteFailure(int rv);

    QuicChromiumClientSession::Handle* session_;
    const bool requires_confirmation_;
    CompletionCallback callback_;
    std::unique_ptr<QuicChromiumClientStream::Handle> stream_;
    // For tracking how much time pending stream requests wait.
    base::TimeTicks pending_start_time_;
    State next_state_;

    base::WeakPtrFactory<StreamRequest> weak_factory_;

    DISALLOW_COPY_AND_ASSIGN(StreamRequest);
  };

  // Constructs a new session which will own |connection|, but not
  // |stream_factory|, which must outlive this session.
  // TODO(rch): decouple the factory from the session via a Delegate interface.
  QuicChromiumClientSession(
      QuicConnection* connection,
      std::unique_ptr<DatagramClientSocket> socket,
      QuicStreamFactory* stream_factory,
      QuicCryptoClientStreamFactory* crypto_client_stream_factory,
      QuicClock* clock,
      TransportSecurityState* transport_security_state,
      std::unique_ptr<QuicServerInfo> server_info,
      const QuicServerId& server_id,
      bool require_confirmation,
      int yield_after_packets,
      QuicTime::Delta yield_after_duration,
      int cert_verify_flags,
      const QuicConfig& config,
      QuicCryptoClientConfig* crypto_config,
      const char* const connection_description,
      base::TimeTicks dns_resolution_start_time,
      base::TimeTicks dns_resolution_end_time,
      QuicClientPushPromiseIndex* push_promise_index,
      ServerPushDelegate* push_delegate,
      base::TaskRunner* task_runner,
      std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
      NetLog* net_log);
  ~QuicChromiumClientSession() override;

  void Initialize() override;

  void AddHandle(Handle* handle);
  void RemoveHandle(Handle* handle);

  // Waits for the handshake to be confirmed and invokes |callback| when
  // that happens. If the handshake has already been confirmed, returns OK.
  // If the connection has already been closed, returns a net error. If the
  // connection closes before the handshake is confirmed, |callback| will
  // be invoked with an error.
  int WaitForHandshakeConfirmation(const CompletionCallback& callback);

  // Attempts to create a new stream.  If the stream can be
  // created immediately, returns OK.  If the open stream limit
  // has been reached, returns ERR_IO_PENDING, and |request|
  // will be added to the stream requets queue and will
  // be completed asynchronously.
  // TODO(rch): remove |stream| from this and use setter on |request|
  // and fix in spdy too.
  int TryCreateStream(StreamRequest* request);

  // Cancels the pending stream creation request.
  void CancelRequest(StreamRequest* request);

  // QuicChromiumPacketWriter::Delegate override.
  int HandleWriteError(int error_code,
                       scoped_refptr<QuicChromiumPacketWriter::ReusableIOBuffer>
                           last_packet) override;
  void OnWriteError(int error_code) override;
  void OnWriteUnblocked() override;

  // QuicSpdySession methods:
  void OnHeadersHeadOfLineBlocking(QuicTime::Delta delta) override;

  // QuicSession methods:
  void OnStreamFrame(const QuicStreamFrame& frame) override;
  QuicChromiumClientStream* CreateOutgoingDynamicStream() override;
  const QuicCryptoClientStream* GetCryptoStream() const override;
  QuicCryptoClientStream* GetMutableCryptoStream() override;
  void CloseStream(QuicStreamId stream_id) override;
  void SendRstStream(QuicStreamId id,
                     QuicRstStreamErrorCode error,
                     QuicStreamOffset bytes_written) override;
  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;
  void OnCryptoHandshakeMessageSent(
      const CryptoHandshakeMessage& message) override;
  void OnCryptoHandshakeMessageReceived(
      const CryptoHandshakeMessage& message) override;
  void OnGoAway(const QuicGoAwayFrame& frame) override;
  void OnRstStream(const QuicRstStreamFrame& frame) override;

  // QuicClientSessionBase methods:
  void OnConfigNegotiated() override;
  void OnProofValid(const QuicCryptoClientConfig::CachedState& cached) override;
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& verify_details) override;

  // QuicConnectionVisitorInterface methods:
  void OnConnectionClosed(QuicErrorCode error,
                          const std::string& error_details,
                          ConnectionCloseSource source) override;
  void OnSuccessfulVersionNegotiation(
      const QuicTransportVersion& version) override;
  void OnPathDegrading() override;
  bool HasOpenDynamicStreams() const override;

  // QuicChromiumPacketReader::Visitor methods:
  void OnReadError(int result, const DatagramClientSocket* socket) override;
  bool OnPacket(const QuicReceivedPacket& packet,
                const QuicSocketAddress& local_address,
                const QuicSocketAddress& peer_address) override;

  // MultiplexedSession methods:
  bool GetRemoteEndpoint(IPEndPoint* endpoint) override;
  bool GetSSLInfo(SSLInfo* ssl_info) const override;
  Error GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                 TokenBindingType tb_type,
                                 std::vector<uint8_t>* out) override;

  // Performs a crypto handshake with the server.
  int CryptoConnect(const CompletionCallback& callback);

  // Causes the QuicConnectionHelper to start reading from all sockets
  // and passing the data along to the QuicConnection.
  void StartReading();

  // Close the session because of |net_error| and notifies the factory
  // that this session has been closed, which will delete the session.
  void CloseSessionOnError(int net_error, QuicErrorCode quic_error);

  std::unique_ptr<base::Value> GetInfoAsValue(
      const std::set<HostPortPair>& aliases);

  const NetLogWithSource& net_log() const { return net_log_; }

  // Returns a Handle to this session.
  std::unique_ptr<QuicChromiumClientSession::Handle> CreateHandle();

  // Returns the number of client hello messages that have been sent on the
  // crypto stream. If the handshake has completed then this is one greater
  // than the number of round-trips needed for the handshake.
  int GetNumSentClientHellos() const;

  // Returns true if |hostname| may be pooled onto this session.  If this
  // is a secure QUIC session, then |hostname| must match the certificate
  // presented during the handshake.
  bool CanPool(const std::string& hostname, PrivacyMode privacy_mode) const;

  const QuicServerId& server_id() const { return server_id_; }

  // Attempts to migrate session when a write error is encountered.
  void MigrateSessionOnWriteError(int error_code);

  // Helper method that writes a packet on the new socket after
  // migration completes. If not null, the packet_ member is written,
  // otherwise a PING packet is written.
  void WriteToNewSocket();

  // Migrates session onto new socket, i.e., starts reading from
  // |socket| in addition to any previous sockets, and sets |writer|
  // to be the new default writer. Returns true if socket was
  // successfully added to the session and the session was
  // successfully migrated to using the new socket. Returns true on
  // successful migration, or false if number of migrations exceeds
  // kMaxReadersPerQuicSession. Takes ownership of |socket|, |reader|,
  // and |writer|.
  bool MigrateToSocket(std::unique_ptr<DatagramClientSocket> socket,
                       std::unique_ptr<QuicChromiumPacketReader> reader,
                       std::unique_ptr<QuicChromiumPacketWriter> writer);

  // Called when NetworkChangeNotifier notifies observers of a newly
  // connected network. Migrates this session to the newly connected
  // network if the session has a pending migration.
  void OnNetworkConnected(NetworkChangeNotifier::NetworkHandle network,
                          const NetLogWithSource& net_log);

  // Schedules a migration alarm to wait for a new network.
  void OnNoNewNetwork();

  // Called when migration alarm fires. If migration has not occurred
  // since alarm was set, closes session with error.
  void OnMigrationTimeout(size_t num_sockets);

  // Populates network error details for this session.
  void PopulateNetErrorDetails(NetErrorDetails* details) const;

  // Returns current default socket. This is the socket over which all
  // QUIC packets are sent. This default socket can change, so do not store the
  // returned socket.
  const DatagramClientSocket* GetDefaultSocket() const;

  bool IsAuthorized(const std::string& hostname) override;

  // Returns true if session has one ore more streams marked as non-migratable.
  bool HasNonMigratableStreams() const;

  bool HandlePromised(QuicStreamId associated_id,
                      QuicStreamId promised_id,
                      const SpdyHeaderBlock& headers) override;

  void DeletePromised(QuicClientPromisedInfo* promised) override;

  void OnPushStreamTimedOut(QuicStreamId stream_id) override;

  // Cancels the push if the push stream for |url| has not been claimed and is
  // still active. Otherwise, no-op.
  void CancelPush(const GURL& url);

  const LoadTimingInfo::ConnectTiming& GetConnectTiming();

  QuicTransportVersion GetQuicVersion() const;

  // Returns the estimate of dynamically allocated memory in bytes.
  // See base/trace_event/memory_usage_estimator.h.
  // TODO(xunjieli): It only tracks |packet_readers_|. Write a better estimate.
  size_t EstimateMemoryUsage() const;

  bool require_confirmation() const { return require_confirmation_; }

 protected:
  // QuicSession methods:
  bool ShouldCreateIncomingDynamicStream(QuicStreamId id) override;
  bool ShouldCreateOutgoingDynamicStream() override;

  QuicChromiumClientStream* CreateIncomingDynamicStream(
      QuicStreamId id) override;

 private:
  friend class test::QuicChromiumClientSessionPeer;

  typedef std::set<Handle*> HandleSet;
  typedef std::list<StreamRequest*> StreamRequestQueue;

  bool WasConnectionEverUsed();

  QuicChromiumClientStream* CreateOutgoingReliableStreamImpl();
  QuicChromiumClientStream* CreateIncomingReliableStreamImpl(QuicStreamId id);
  // A completion callback invoked when a read completes.
  void OnReadComplete(int result);

  void OnClosedStream();

  void CloseAllStreams(int net_error);
  void CloseAllHandles(int net_error);
  void CancelAllRequests(int net_error);
  void NotifyRequestsOfConfirmation(int net_error);

  // Notifies the factory that this session is going away and no more streams
  // should be created from it.  This needs to be called before closing any
  // streams, because closing a stream may cause a new stream to be created.
  void NotifyFactoryOfSessionGoingAway();

  // Posts a task to notify the factory that this session has been closed.
  void NotifyFactoryOfSessionClosedLater();

  // Notifies the factory that this session has been closed which will
  // delete |this|.
  void NotifyFactoryOfSessionClosed();

  QuicServerId server_id_;
  bool require_confirmation_;
  std::unique_ptr<QuicCryptoClientStream> crypto_stream_;
  QuicStreamFactory* stream_factory_;
  std::vector<std::unique_ptr<DatagramClientSocket>> sockets_;
  TransportSecurityState* transport_security_state_;
  std::unique_ptr<QuicServerInfo> server_info_;
  std::unique_ptr<CertVerifyResult> cert_verify_result_;
  std::unique_ptr<ct::CTVerifyResult> ct_verify_result_;
  std::string pinning_failure_log_;
  bool pkp_bypassed_;
  HandleSet handles_;
  StreamRequestQueue stream_requests_;
  std::vector<CompletionCallback> waiting_for_confirmation_callbacks_;
  CompletionCallback callback_;
  size_t num_total_streams_;
  base::TaskRunner* task_runner_;
  NetLogWithSource net_log_;
  std::vector<std::unique_ptr<QuicChromiumPacketReader>> packet_readers_;
  LoadTimingInfo::ConnectTiming connect_timing_;
  std::unique_ptr<QuicConnectionLogger> logger_;
  // True when the session is going away, and streams may no longer be created
  // on this session. Existing stream will continue to be processed.
  bool going_away_;
  // True when the session receives a go away from server due to port migration.
  bool port_migration_detected_;
  TokenBindingSignatureMap token_binding_signatures_;
  // Not owned. |push_delegate_| outlives the session and handles server pushes
  // received by session.
  ServerPushDelegate* push_delegate_;
  // UMA histogram counters for streams pushed to this session.
  int streams_pushed_count_;
  int streams_pushed_and_claimed_count_;
  uint64_t bytes_pushed_count_;
  uint64_t bytes_pushed_and_unclaimed_count_;
  // Stores packet that witnesses socket write error. This packet is
  // written to a new socket after migration completes.
  scoped_refptr<QuicChromiumPacketWriter::ReusableIOBuffer> packet_;
  // TODO(jri): Replace use of migration_pending_ sockets_.size().
  // When a task is posted for MigrateSessionOnError, pass in
  // sockets_.size(). Then in MigrateSessionOnError, check to see if
  // the current sockets_.size() == the passed in value.
  bool migration_pending_;  // True while migration is underway.
  base::WeakPtrFactory<QuicChromiumClientSession> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(QuicChromiumClientSession);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_CHROMIUM_CLIENT_SESSION_H_
