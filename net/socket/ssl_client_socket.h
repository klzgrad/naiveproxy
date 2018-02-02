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
#include "net/base/completion_callback.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/socket/ssl_socket.h"
#include "net/socket/stream_socket.h"
#include "net/ssl/token_binding.h"

namespace base {
class FilePath;
}

namespace crypto {
class ECPrivateKey;
}

namespace net {

class CTPolicyEnforcer;
class CertVerifier;
class ChannelIDService;
class CTVerifier;
class SSLCertRequestInfo;
class TransportSecurityState;

// This struct groups together several fields which are used by various
// classes related to SSLClientSocket.
struct SSLClientSocketContext {
  SSLClientSocketContext() = default;
  SSLClientSocketContext(CertVerifier* cert_verifier_arg,
                         ChannelIDService* channel_id_service_arg,
                         TransportSecurityState* transport_security_state_arg,
                         CTVerifier* cert_transparency_verifier_arg,
                         CTPolicyEnforcer* ct_policy_enforcer_arg,
                         const std::string& ssl_session_cache_shard_arg)
      : cert_verifier(cert_verifier_arg),
        channel_id_service(channel_id_service_arg),
        transport_security_state(transport_security_state_arg),
        cert_transparency_verifier(cert_transparency_verifier_arg),
        ct_policy_enforcer(ct_policy_enforcer_arg),
        ssl_session_cache_shard(ssl_session_cache_shard_arg) {}

  CertVerifier* cert_verifier = nullptr;
  ChannelIDService* channel_id_service = nullptr;
  TransportSecurityState* transport_security_state = nullptr;
  CTVerifier* cert_transparency_verifier = nullptr;
  CTPolicyEnforcer* ct_policy_enforcer = nullptr;
  // ssl_session_cache_shard is an opaque string that identifies a shard of the
  // SSL session cache. SSL sockets with the same ssl_session_cache_shard may
  // resume each other's SSL sessions but we'll never sessions between shards.
  std::string ssl_session_cache_shard;
};

// Details on a failed operation. This enum is used to diagnose causes of TLS
// version interference by buggy middleboxes. The values are histogramed so they
// must not be changed.
enum class SSLErrorDetails {
  kOther = 0,
  // The failure was due to ERR_CONNECTION_CLOSED. BlueCoat has a bug with this
  // failure mode. https://crbug.com/694593.
  kConnectionClosed = 1,
  // The failure was due to ERR_CONNECTION_RESET.
  kConnectionReset = 2,
  // The failure was due to receiving an access_denied alert. Fortinet has a
  // bug with this failure mode. https://crbug.com/676969.
  kAccessDeniedAlert = 3,
  // The failure was due to receiving a bad_record_mac alert.
  kBadRecordMACAlert = 4,
  // The failure was due to receiving an unencrypted application_data record
  // during the handshake. Watchguard has a bug with this failure
  // mode. https://crbug.com/733223.
  kApplicationDataInsteadOfHandshake = 5,
  // The failure was due to failing to negotiate a version or cipher suite.
  kVersionOrCipherMismatch = 6,
  // The failure was due to some other protocol error.
  kProtocolError = 7,
  kLastValue = kProtocolError,
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

  // Gets the SSL CertificateRequest info of the socket after Connect failed
  // with ERR_SSL_CLIENT_AUTH_CERT_NEEDED.
  virtual void GetSSLCertRequestInfo(
      SSLCertRequestInfo* cert_request_info) = 0;

  // Log SSL key material to |path|. Must be called before any
  // SSLClientSockets are created.
  //
  // TODO(davidben): Switch this to a parameter on the SSLClientSocketContext
  // once https://crbug.com/458365 is resolved. To avoid a dependency from
  // OS_NACL to file I/O logic, this will require splitting SSLKeyLogger into an
  // interface, built with OS_NACL and a non-NaCl SSLKeyLoggerImpl.
  static void SetSSLKeyLogFile(const base::FilePath& path);

  // Returns true if |error| is OK or |load_flags| ignores certificate errors
  // and |error| is a certificate error.
  static bool IgnoreCertError(int error, int load_flags);

  // ClearSessionCache clears the SSL session cache, used to resume SSL
  // sessions.
  static void ClearSessionCache();

  // Returns the ChannelIDService used by this socket, or NULL if
  // channel ids are not supported.
  virtual ChannelIDService* GetChannelIDService() const = 0;

  // Generates the signature used in Token Binding using key |*key| and for a
  // Token Binding of type |tb_type|, putting the signature in |*out|. Returns a
  // net error code.
  virtual Error GetTokenBindingSignature(crypto::ECPrivateKey* key,
                                         TokenBindingType tb_type,
                                         std::vector<uint8_t>* out) = 0;

  // This method is only for debugging crbug.com/548423 and will be removed when
  // that bug is closed. This returns the channel ID key that was used when
  // establishing the connection (or NULL if no channel ID was used).
  virtual crypto::ECPrivateKey* GetChannelIDKey() const = 0;

  // Returns details for a failed Connect() operation. This method is used to
  // track causes of TLS version interference by buggy middleboxes.
  virtual SSLErrorDetails GetConnectErrorDetails() const;

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
                           ConnectSignedCertTimestampsEnabledTLSExtension);
  FRIEND_TEST_ALL_PREFIXES(SSLClientSocketTest,
                           ConnectSignedCertTimestampsEnabledOCSP);
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
