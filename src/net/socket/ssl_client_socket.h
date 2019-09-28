// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SSL_CLIENT_SOCKET_H_
#define NET_SOCKET_SSL_CLIENT_SOCKET_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/socket/ssl_socket.h"

namespace net {

class CTPolicyEnforcer;
class CertVerifier;
class CTVerifier;
class HostPortPair;
class SSLClientSessionCache;
struct SSLConfig;
class SSLKeyLogger;
class StreamSocket;
class TransportSecurityState;

// A client socket that uses SSL as the transport layer.
//
// NOTE: The SSL handshake occurs within the Connect method after a TCP
// connection is established.  If a SSL error occurs during the handshake,
// Connect will fail.
//
class NET_EXPORT SSLClientSocket : public SSLSocket {
 public:
  SSLClientSocket();

  // Log SSL key material to |logger|. Must be called before any
  // SSLClientSockets are created.
  //
  // TODO(davidben): Switch this to a parameter on the SSLClientSocketContext
  // once https://crbug.com/458365 is resolved.
  static void SetSSLKeyLogger(std::unique_ptr<SSLKeyLogger> logger);

 protected:
  void set_signed_cert_timestamps_received(
      bool signed_cert_timestamps_received) {
    signed_cert_timestamps_received_ = signed_cert_timestamps_received;
  }

  void set_stapled_ocsp_response_received(bool stapled_ocsp_response_received) {
    stapled_ocsp_response_received_ = stapled_ocsp_response_received;
  }

  // Serialize |next_protos| in the wire format for ALPN and NPN: protocols are
  // listed in order, each prefixed by a one-byte length.
  static std::vector<uint8_t> SerializeNextProtos(
      const NextProtoVector& next_protos);

 private:
  FRIEND_TEST_ALL_PREFIXES(SSLClientSocket, SerializeNextProtos);
  // For signed_cert_timestamps_received_ and stapled_ocsp_response_received_.
  FRIEND_TEST_ALL_PREFIXES(SSLClientSocketTest,
                           ConnectSignedCertTimestampsTLSExtension);
  FRIEND_TEST_ALL_PREFIXES(SSLClientSocketTest,
                           ConnectSignedCertTimestampsEnablesOCSP);
  FRIEND_TEST_ALL_PREFIXES(SSLClientSocketTest,
                           ConnectSignedCertTimestampsDisabled);
  FRIEND_TEST_ALL_PREFIXES(SSLClientSocketTest,
                           VerifyServerChainProperlyOrdered);

  // True if SCTs were received via a TLS extension.
  bool signed_cert_timestamps_received_;
  // True if a stapled OCSP response was received.
  bool stapled_ocsp_response_received_;
};

// Shared state and configuration across multiple SSLClientSockets.
class NET_EXPORT SSLClientContext {
 public:
  // Creates a new SSLClientContext with the specified parameters. The
  // SSLClientContext may not outlive the input parameters.
  //
  // |ssl_client_session_cache| may be null to disable session caching.
  SSLClientContext(CertVerifier* cert_verifier,
                   TransportSecurityState* transport_security_state,
                   CTVerifier* cert_transparency_verifier,
                   CTPolicyEnforcer* ct_policy_enforcer,
                   SSLClientSessionCache* ssl_client_session_cache);
  ~SSLClientContext();

  CertVerifier* cert_verifier() { return cert_verifier_; }
  TransportSecurityState* transport_security_state() {
    return transport_security_state_;
  }
  CTVerifier* cert_transparency_verifier() {
    return cert_transparency_verifier_;
  }
  CTPolicyEnforcer* ct_policy_enforcer() { return ct_policy_enforcer_; }
  SSLClientSessionCache* ssl_client_session_cache() {
    return ssl_client_session_cache_;
  }

  // Creates a new SSLClientSocket which can then be used to establish an SSL
  // connection to |host_and_port| over the already-connected |stream_socket|.
  std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      std::unique_ptr<StreamSocket> stream_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config);

 private:
  CertVerifier* cert_verifier_;
  TransportSecurityState* transport_security_state_;
  CTVerifier* cert_transparency_verifier_;
  CTPolicyEnforcer* ct_policy_enforcer_;
  SSLClientSessionCache* ssl_client_session_cache_;

  DISALLOW_COPY_AND_ASSIGN(SSLClientContext);
};

}  // namespace net

#endif  // NET_SOCKET_SSL_CLIENT_SOCKET_H_
