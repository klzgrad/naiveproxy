// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_H_

#include <memory>
#include <string>

#include "net/third_party/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quic/core/quic_crypto_stream.h"
#include "net/third_party/quic/core/quic_error_codes.h"
#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/quartc/quartc_packet_writer.h"
#include "net/third_party/quic/quartc/quartc_stream.h"

namespace quic {

// QuartcSession owns and manages a QUIC connection.
class QuartcSession : public QuicSession,
                      public QuartcPacketTransport::Delegate {
 public:
  QuartcSession(std::unique_ptr<QuicConnection> connection,
                Visitor* visitor,
                const QuicConfig& config,
                const ParsedQuicVersionVector& supported_versions,
                const QuicClock* clock);
  QuartcSession(const QuartcSession&) = delete;
  QuartcSession& operator=(const QuartcSession&) = delete;
  ~QuartcSession() override;

  // QuicSession overrides.
  QuartcStream* CreateOutgoingBidirectionalStream();

  // Sends short unreliable message using quic message frame (message must fit
  // in one quic packet). If connection is blocked by congestion control,
  // message will be queued and resent later after receiving an OnCanWrite
  // notification.
  //
  // Message size must be <= GetLargestMessagePayload().
  //
  // Supported in quic version 45 or later.
  //
  // Returns false and logs error if message is too long or session does not
  // support SendMessage API. Other unexpected errors during send will not be
  // returned, because messages can be sent later if connection is congestion
  // controlled.
  bool SendOrQueueMessage(QuicString message);

  // Returns largest message payload acceptable in SendQuartcMessage.
  QuicPacketLength GetLargestMessagePayload() const {
    return connection()->GetLargestMessagePayload();
  }

  // Return true if transport support message frame.
  bool CanSendMessage() const {
    return connection()->transport_version() > QUIC_VERSION_44;
  }

  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;

  // QuicConnectionVisitorInterface overrides.
  void OnCongestionWindowChange(QuicTime now) override;
  bool ShouldKeepConnectionAlive() const override;

  void OnCanWrite() override;

  void OnConnectionClosed(QuicErrorCode error,
                          const QuicString& error_details,
                          ConnectionCloseSource source) override;

  // QuartcSession methods.
  virtual void StartCryptoHandshake() = 0;

  // Closes the connection with the given human-readable error details.
  // The connection closes with the QUIC_CONNECTION_CANCELLED error code to
  // indicate the application closed it.
  //
  // Informs the peer that the connection has been closed.  This prevents the
  // peer from waiting until the connection times out.
  //
  // Cleans up the underlying QuicConnection's state.  Closing the connection
  // makes it safe to delete the QuartcSession.
  void CloseConnection(const QuicString& details);

  // If the given stream is still open, sends a reset frame to cancel it.
  // Note:  This method cancels a stream by QuicStreamId rather than by pointer
  // (or by a method on QuartcStream) because QuartcSession (and not
  // the caller) owns the streams.  Streams may finish and be deleted before the
  // caller tries to cancel them, rendering the caller's pointers invalid.
  void CancelStream(QuicStreamId stream_id);

  // Callbacks called by the QuartcSession to notify the user of the
  // QuartcSession of certain events.
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Called when the crypto handshake is complete. Crypto handshake on the
    // client is only completed _after_ SHLO is received, but we can actually
    // start sending media data right after CHLO is sent.
    virtual void OnCryptoHandshakeComplete() = 0;

    // Connection can be writable even before crypto handshake is complete.
    // In particular, on the client, we can start sending data after sending
    // full CHLO, without waiting for SHLO. This reduces a send delay by 1-rtt.
    //
    // This may be called multiple times.
    virtual void OnConnectionWritable() = 0;

    // Called when a new stream is received from the remote endpoint.
    virtual void OnIncomingStream(QuartcStream* stream) = 0;

    // Called when network parameters change in response to an ack frame.
    virtual void OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                           QuicBandwidth pacing_rate,
                                           QuicTime::Delta latest_rtt) = 0;

    // Called when the connection is closed. This means all of the streams will
    // be closed and no new streams can be created.
    virtual void OnConnectionClosed(QuicErrorCode error_code,
                                    const QuicString& error_details,
                                    ConnectionCloseSource source) = 0;

    // Called when message (sent as SendMessage) is received.
    virtual void OnMessageReceived(QuicStringPiece message) = 0;

    // TODO(zhihuang): Add proof verification.
  };

  // The |delegate| is not owned by QuartcSession.
  void SetDelegate(Delegate* session_delegate);

  // Called when CanWrite() changes from false to true.
  void OnTransportCanWrite() override;

  // Called when a packet has been received and should be handled by the
  // QuicConnection.
  void OnTransportReceived(const char* data, size_t data_len) override;

  void OnMessageReceived(QuicStringPiece message) override;

  // Returns number of queued (not sent) messages submitted by
  // SendOrQueueMessage. Messages are queued if connection is congestion
  // controlled.
  size_t send_message_queue_size() const { return send_message_queue_.size(); }

 protected:
  // QuicSession override.
  QuicStream* CreateIncomingStream(QuicStreamId id) override;
  QuicStream* CreateIncomingStream(PendingStream pending) override;

  std::unique_ptr<QuartcStream> CreateDataStream(QuicStreamId id,
                                                 spdy::SpdyPriority priority);
  std::unique_ptr<QuartcStream> CreateDataStream(PendingStream pending,
                                                 spdy::SpdyPriority priority);
  // Activates a QuartcStream.  The session takes ownership of the stream, but
  // returns an unowned pointer to the stream for convenience.
  QuartcStream* ActivateDataStream(std::unique_ptr<QuartcStream> stream);

  void ResetStream(QuicStreamId stream_id, QuicRstStreamErrorCode error);

  const QuicClock* clock() { return clock_; }

 private:
  std::unique_ptr<QuartcStream> InitializeDataStream(
      std::unique_ptr<QuartcStream> stream,
      spdy::SpdyPriority priority);

  void ProcessSendMessageQueue();

  // Take ownership of the QuicConnection.  Note: if |connection_| changes,
  // the new value of |connection_| must be given to |packet_writer_| before any
  // packets are written.  Otherwise, |packet_writer_| will crash.
  std::unique_ptr<QuicConnection> connection_;

  // For recording packet receipt time
  const QuicClock* clock_;

  // Not owned by QuartcSession.
  Delegate* session_delegate_ = nullptr;

  // Options passed to the packet writer for each packet.
  std::unique_ptr<QuartcPerPacketOptions> per_packet_options_;

  // Queue of pending messages sent by SendQuartcMessage that were not sent
  // yet or blocked by congestion control. Messages are queued in the order
  // of sent by SendOrQueueMessage().
  QuicDeque<QuicString> send_message_queue_;
};

class QuartcClientSession : public QuartcSession,
                            public QuicCryptoClientStream::ProofHandler {
 public:
  QuartcClientSession(
      std::unique_ptr<QuicConnection> connection,
      const QuicConfig& config,
      const ParsedQuicVersionVector& supported_versions,
      const QuicClock* clock,
      std::unique_ptr<QuartcPacketWriter> packet_writer,
      std::unique_ptr<QuicCryptoClientConfig> client_crypto_config,
      QuicStringPiece server_crypto_config);
  QuartcClientSession(const QuartcClientSession&) = delete;
  QuartcClientSession& operator=(const QuartcClientSession&) = delete;

  ~QuartcClientSession() override;

  // Initialize should not be called on a QuartcSession.  Instead, call
  // StartCryptoHandshake().
  // TODO(mellem): Move creation of the crypto stream into Initialize() and
  // remove StartCryptoHandshake() to bring QuartcSession in line with other
  // implementations of QuicSession, which can be started by calling
  // Initialize().
  void Initialize() override;

  // Accessors for the client crypto stream.
  QuicCryptoStream* GetMutableCryptoStream() override;
  const QuicCryptoStream* GetCryptoStream() const override;

  // Initializes the session and sends a handshake.
  void StartCryptoHandshake() override;

  // ProofHandler overrides.
  void OnProofValid(const QuicCryptoClientConfig::CachedState& cached) override;

  // Called by the client crypto handshake when proof verification details
  // become available, either because proof verification is complete, or when
  // cached details are used.
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& verify_details) override;

 private:
  // Packet writer used by |connection_|.
  std::unique_ptr<QuartcPacketWriter> packet_writer_;

  // Config for QUIC crypto stream.
  std::unique_ptr<QuicCryptoClientConfig> client_crypto_config_;

  // Client perspective crypto stream.
  std::unique_ptr<QuicCryptoClientStream> crypto_stream_;

  const QuicString server_config_;
};

class QuartcServerSession : public QuartcSession {
 public:
  QuartcServerSession(std::unique_ptr<QuicConnection> connection,
                      Visitor* visitor,
                      const QuicConfig& config,
                      const ParsedQuicVersionVector& supported_versions,
                      const QuicClock* clock,
                      const QuicCryptoServerConfig* server_crypto_config,
                      QuicCompressedCertsCache* const compressed_certs_cache,
                      QuicCryptoServerStream::Helper* const stream_helper);
  QuartcServerSession(const QuartcServerSession&) = delete;
  QuartcServerSession& operator=(const QuartcServerSession&) = delete;

  // Accessors for the server crypto stream.
  QuicCryptoStream* GetMutableCryptoStream() override;
  const QuicCryptoStream* GetCryptoStream() const override;

  // Initializes the session and prepares to receive a handshake.
  void StartCryptoHandshake() override;

 private:
  // Config for QUIC crypto stream.
  const QuicCryptoServerConfig* server_crypto_config_;

  // Used by QUIC crypto server stream to track most recently compressed certs.
  QuicCompressedCertsCache* const compressed_certs_cache_;

  // This helper is needed to create QuicCryptoServerStream.
  QuicCryptoServerStream::Helper* const stream_helper_;

  // Server perspective crypto stream.
  std::unique_ptr<QuicCryptoServerStream> crypto_stream_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_SESSION_H_
