// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SSL_CLIENT_SOCKET_H_
#define NET_SOCKET_SSL_CLIENT_SOCKET_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/strings/string_piece.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/socket/ssl_socket.h"
#include "net/socket/stream_socket.h"

namespace net {

class CTPolicyEnforcer;
class CertVerifier;
class CTVerifier;
class SSLClientSessionCache;
class SSLKeyLogger;
class TransportSecurityState;

// This struct groups together several fields which are used by various
// classes related to SSLClientSocket.
struct SSLClientSocketContext {
  SSLClientSocketContext() = default;
  SSLClientSocketContext(CertVerifier* cert_verifier_arg,
                         TransportSecurityState* transport_security_state_arg,
                         CTVerifier* cert_transparency_verifier_arg,
                         CTPolicyEnforcer* ct_policy_enforcer_arg,
                         SSLClientSessionCache* ssl_client_session_cache_arg)
      : cert_verifier(cert_verifier_arg),
        transport_security_state(transport_security_state_arg),
        cert_transparency_verifier(cert_transparency_verifier_arg),
        ct_policy_enforcer(ct_policy_enforcer_arg),
        ssl_client_session_cache(ssl_client_session_cache_arg) {}

  CertVerifier* cert_verifier = nullptr;
  TransportSecurityState* transport_security_state = nullptr;
  CTVerifier* cert_transparency_verifier = nullptr;
  CTPolicyEnforcer* ct_policy_enforcer = nullptr;
  SSLClientSessionCache* ssl_client_session_cache = nullptr;
};

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

}  // namespace net

#endif  // NET_SOCKET_SSL_CLIENT_SOCKET_H_
