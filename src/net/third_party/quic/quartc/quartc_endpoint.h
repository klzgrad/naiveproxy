// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_ENDPOINT_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_ENDPOINT_H_

#include "net/third_party/quic/core/quic_alarm_factory.h"
#include "net/third_party/quic/core/quic_error_codes.h"
#include "net/third_party/quic/platform/api/quic_clock.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/quartc/quartc_connection_helper.h"
#include "net/third_party/quic/quartc/quartc_crypto_helpers.h"
#include "net/third_party/quic/quartc/quartc_dispatcher.h"
#include "net/third_party/quic/quartc/quartc_factory.h"

namespace quic {

// Private implementation of QuartcEndpoint.  Enables different implementations
// for client and server endpoints.
class QuartcEndpointImpl {
 public:
  virtual ~QuartcEndpointImpl() = default;

  virtual QuicStringPiece server_crypto_config() const = 0;
};

// Endpoint (client or server) in a peer-to-peer Quartc connection.
class QuartcEndpoint {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when an endpoint creates a new session, before any packets are
    // processed or sent.  The callee should perform any additional
    // configuration required, such as setting a session delegate, before
    // returning.  |session| is owned by the endpoint, but remains safe to use
    // until another call to |OnSessionCreated| occurs, at which point previous
    // session is destroyed.
    virtual void OnSessionCreated(QuartcSession* session) = 0;

    // Called if the endpoint fails to establish a session after a call to
    // Connect.  (The most likely cause is a network idle timeout.)
    virtual void OnConnectError(QuicErrorCode error,
                                const QuicString& error_details) = 0;
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
class QuartcClientEndpoint : public QuartcEndpoint {
 public:
  // |alarm_factory|, |clock|, and |delegate| are owned by the caller and must
  // outlive the endpoint.
  QuartcClientEndpoint(
      QuicAlarmFactory* alarm_factory,
      const QuicClock* clock,
      Delegate* delegate,
      const QuartcSessionConfig& config,
      QuicStringPiece serialized_server_config,
      std::unique_ptr<QuicVersionManager> version_manager = nullptr);

  void Connect(QuartcPacketTransport* packet_transport) override;

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
  const QuicString serialized_server_config_;

  // Version manager.  May be injected to control version negotiation in tests.
  std::unique_ptr<QuicVersionManager> version_manager_;

  // Alarm for creating sessions asynchronously.  The alarm is set when
  // Connect() is called.  When it fires, the endpoint creates a session and
  // calls the delegate.
  std::unique_ptr<QuicAlarm> create_session_alarm_;

  // QuartcFactory used by this endpoint to create sessions.  This is an
  // implementation detail of the QuartcEndpoint, and will eventually be
  // replaced by a dispatcher (for servers) or version-negotiation agent (for
  // clients).
  std::unique_ptr<QuartcFactory> factory_;

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
      QuartcEndpoint::Delegate* delegate,
      const QuartcSessionConfig& config,
      std::unique_ptr<QuicVersionManager> version_manager = nullptr);

  // Implements QuartcEndpoint.
  void Connect(QuartcPacketTransport* packet_transport) override;

  // Implements QuartcDispatcher::Delegate.
  void OnSessionCreated(QuartcSession* session) override;

  // Accessor to retrieve the server crypto config.  May only be called after
  // Connect().
  QuicStringPiece server_crypto_config() const {
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

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_ENDPOINT_H_
