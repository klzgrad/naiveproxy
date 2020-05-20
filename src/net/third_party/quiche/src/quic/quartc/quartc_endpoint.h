// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_QUARTC_ENDPOINT_H_
#define QUICHE_QUIC_QUARTC_QUARTC_ENDPOINT_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_clock.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_connection_helper.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_crypto_helpers.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_dispatcher.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_factory.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// Endpoint (client or server) in a peer-to-peer Quartc connection.
class QuartcEndpoint {
 public:
  class Delegate : public QuartcSession::Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when an endpoint creates a new session, before any packets are
    // processed or sent.  The callee should perform any additional
    // configuration required, such as setting up congestion control, before
    // returning.  |session| is owned by the endpoint, but remains safe to use
    // until another call to |OnSessionCreated| or |OnConnectionClosed| occurs,
    // at which point previous session may be destroyed.
    //
    // Callees must not change the |session|'s delegate.  The Endpoint itself
    // manages the delegate and will forward calls.
    //
    // New calls to |OnSessionCreated| will only occur prior to
    // |OnConnectionWritable|, during initial connection negotiation.
    virtual void OnSessionCreated(QuartcSession* session) = 0;
  };

  virtual ~QuartcEndpoint() = default;

  // Connects the endpoint using the given session config.  After |Connect| is
  // called, the endpoint will asynchronously create a session, then call
  // |Delegate::OnSessionCreated|.
  virtual void Connect(QuartcPacketTransport* packet_transport) = 0;
};

// Implementation of QuartcEndpoint which immediately (but asynchronously)
// creates a session by scheduling a QuicAlarm.  Only suitable for use with the
// client perspective.
class QuartcClientEndpoint : public QuartcEndpoint,
                             public QuartcSession::Delegate {
 public:
  // |alarm_factory|, |clock|, and |delegate| are owned by the caller and must
  // outlive the endpoint.
  QuartcClientEndpoint(
      QuicAlarmFactory* alarm_factory,
      const QuicClock* clock,
      QuicRandom* random,
      QuartcEndpoint::Delegate* delegate,
      const QuartcSessionConfig& config,
      quiche::QuicheStringPiece serialized_server_config,
      std::unique_ptr<QuicVersionManager> version_manager = nullptr);

  void Connect(QuartcPacketTransport* packet_transport) override;

  // QuartcSession::Delegate overrides.
  void OnCryptoHandshakeComplete() override;
  void OnConnectionWritable() override;
  void OnIncomingStream(QuartcStream* stream) override;
  void OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                 QuicBandwidth pacing_rate,
                                 QuicTime::Delta latest_rtt) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;
  void OnMessageReceived(quiche::QuicheStringPiece message) override;
  void OnMessageSent(int64_t datagram_id) override;
  void OnMessageAcked(int64_t datagram_id, QuicTime receive_timestamp) override;
  void OnMessageLost(int64_t datagram_id) override;

 private:
  friend class CreateSessionDelegate;
  class CreateSessionDelegate : public QuicAlarm::Delegate {
   public:
    CreateSessionDelegate(QuartcClientEndpoint* endpoint)
        : endpoint_(endpoint) {}

    void OnAlarm() override { endpoint_->OnCreateSessionAlarm(); }

   private:
    QuartcClientEndpoint* endpoint_;
  };

  // Callback which occurs when |create_session_alarm_| fires.
  void OnCreateSessionAlarm();

  // Implementation of QuicAlarmFactory used by this endpoint.  Unowned.
  QuicAlarmFactory* alarm_factory_;

  // Implementation of QuicClock used by this endpoint.  Unowned.
  const QuicClock* clock_;

  // Delegate which receives callbacks for newly created sessions.
  QuartcEndpoint::Delegate* delegate_;

  // Server config.  If valid, used to perform a 0-RTT connection.
  const std::string serialized_server_config_;

  // Version manager.  May be injected to control version negotiation in tests.
  std::unique_ptr<QuicVersionManager> version_manager_;

  // Versions to be used when the next session is created.  The session will
  // choose one of these versions for its connection attempt.
  //
  // If the connection does not succeed, the client session MAY try again using
  // another version from this list, or it MAY simply fail with a
  // QUIC_INVALID_VERSION error.  The latter occurs when it is not possible to
  // upgrade a connection in-place (for example, if the way stream ids are
  // allocated changes between versions).  This failure mode is handled by
  // narrowing |current_versions_| to one of that is mutually-supported and
  // reconnecting (with a new session).
  ParsedQuicVersionVector current_versions_;

  // Alarm for creating sessions asynchronously.  The alarm is set when
  // Connect() is called.  When it fires, the endpoint creates a session and
  // calls the delegate.
  std::unique_ptr<QuicAlarm> create_session_alarm_;

  // Helper used by QuicConnection.
  std::unique_ptr<QuicConnectionHelperInterface> connection_helper_;

  // Config to be used for new sessions.
  QuartcSessionConfig config_;

  // The currently-active session.  Nullptr until |Connect| and
  // |Delegate::OnSessionCreated| are called.
  std::unique_ptr<QuartcSession> session_;

  QuartcPacketTransport* packet_transport_;
};

// Implementation of QuartcEndpoint which uses a QuartcDispatcher to listen for
// an incoming CHLO and create a session when one arrives.  Only suitable for
// use with the server perspective.
class QuartcServerEndpoint : public QuartcEndpoint,
                             public QuartcDispatcher::Delegate {
 public:
  QuartcServerEndpoint(
      QuicAlarmFactory* alarm_factory,
      const QuicClock* clock,
      QuicRandom* random,
      QuartcEndpoint::Delegate* delegate,
      const QuartcSessionConfig& config,
      std::unique_ptr<QuicVersionManager> version_manager = nullptr);

  // Implements QuartcEndpoint.
  void Connect(QuartcPacketTransport* packet_transport) override;

  // Implements QuartcDispatcher::Delegate.
  void OnSessionCreated(QuartcSession* session) override;

  // Accessor to retrieve the server crypto config.  May only be called after
  // Connect().
  quiche::QuicheStringPiece server_crypto_config() const {
    return crypto_config_.serialized_crypto_config;
  }

  const std::vector<ParsedQuicVersion> GetSupportedQuicVersions() const {
    return version_manager_->GetSupportedVersions();
  }

 private:
  // Implementation of QuicAlarmFactory used by this endpoint.  Unowned.
  QuicAlarmFactory* alarm_factory_;

  // Delegate which receives callbacks for newly created sessions.
  QuartcEndpoint::Delegate* delegate_;

  // Config to be used for new sessions.
  QuartcSessionConfig config_;

  // Version manager.  May be injected to control version negotiation in tests.
  std::unique_ptr<QuicVersionManager> version_manager_;

  // QuartcDispatcher waits for an incoming CHLO, then either rejects it or
  // creates a session to respond to it.  The dispatcher owns all sessions it
  // creates.
  std::unique_ptr<QuartcDispatcher> dispatcher_;

  // This field is only available before connection was started.
  std::unique_ptr<QuartcConnectionHelper> pre_connection_helper_;

  // A configuration, containing public key, that may need to be passed to the
  // client to enable 0rtt.
  CryptoServerConfig crypto_config_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_QUARTC_ENDPOINT_H_
