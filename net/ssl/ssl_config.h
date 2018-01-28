// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_CONFIG_H_
#define NET_SSL_SSL_CONFIG_H_

#include <stdint.h>

#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/cert/x509_certificate.h"
#include "net/socket/next_proto.h"
#include "net/ssl/ssl_private_key.h"

namespace net {

// Various TLS/SSL ProtocolVersion values encoded as uint16_t
//      struct {
//          uint8_t major;
//          uint8_t minor;
//      } ProtocolVersion;
// The most significant byte is |major|, and the least significant byte
// is |minor|.
enum {
  SSL_PROTOCOL_VERSION_TLS1 = 0x0301,
  SSL_PROTOCOL_VERSION_TLS1_1 = 0x0302,
  SSL_PROTOCOL_VERSION_TLS1_2 = 0x0303,
  SSL_PROTOCOL_VERSION_TLS1_3 = 0x0304,
};

enum TokenBindingParam {
  TB_PARAM_RSA2048_PKCS15 = 0,
  TB_PARAM_RSA2048_PSS = 1,
  TB_PARAM_ECDSAP256 = 2,
};

enum TLS13Variant {
  kTLS13VariantDraft,
  kTLS13VariantExperiment,
  kTLS13VariantExperiment2,
  kTLS13VariantExperiment3,
};

// Default minimum protocol version.
NET_EXPORT extern const uint16_t kDefaultSSLVersionMin;

// Default maximum protocol version.
NET_EXPORT extern const uint16_t kDefaultSSLVersionMax;

// Default TLS 1.3 variant.
NET_EXPORT extern const TLS13Variant kDefaultTLS13Variant;

// A collection of SSL-related configuration settings.
struct NET_EXPORT SSLConfig {
  // Default to revocation checking.
  SSLConfig();
  SSLConfig(const SSLConfig& other);
  ~SSLConfig();

  // Returns true if |cert| is one of the certs in |allowed_bad_certs|.
  // The expected cert status is written to |cert_status|. |*cert_status| can
  // be NULL if user doesn't care about the cert status.
  bool IsAllowedBadCert(X509Certificate* cert, CertStatus* cert_status) const;

  // Returns the set of flags to use for certificate verification, which is a
  // bitwise OR of CertVerifier::VerifyFlags that represent this SSLConfig's
  // configuration.
  int GetCertVerifyFlags() const;

  // rev_checking_enabled is true if online certificate revocation checking is
  // enabled (i.e. OCSP and CRL fetching).
  //
  // Regardless of this flag, CRLSet checking is always enabled and locally
  // cached revocation information will be considered.
  bool rev_checking_enabled;

  // rev_checking_required_local_anchors is true if revocation checking is
  // required to succeed when certificates chain to local trust anchors (that
  // is, non-public CAs). If revocation information cannot be obtained, such
  // certificates will be treated as revoked ("hard-fail").
  // Note: This is distinct from rev_checking_enabled. If true, it is
  // equivalent to also setting rev_checking_enabled, but only when the
  // certificate chain chains to a local (non-public) trust anchor.
  bool rev_checking_required_local_anchors;

  // sha1_local_anchors_enabled is true if SHA-1 signed certificates issued by a
  // local (non-public) trust anchor should be allowed.
  bool sha1_local_anchors_enabled;

  // common_name_fallback_local_anchors_enabled is true if certificates which
  // only have a commonName in the Subject (i.e. lacking a subjectAltName)
  // should be checked if the name matches. Only those issued by a local
  // (non-public) trust anchor will be allowed to match.
  bool common_name_fallback_local_anchors_enabled;

  // The minimum and maximum protocol versions that are enabled.
  // (Use the SSL_PROTOCOL_VERSION_xxx enumerators defined above.)
  // SSL 2.0 and SSL 3.0 are not supported. If version_max < version_min, it
  // means no protocol versions are enabled.
  uint16_t version_min;
  uint16_t version_max;

  // The TLS 1.3 variant that is enabled. This only takes affect if TLS 1.3 is
  // also enabled via version_min and version_max.
  TLS13Variant tls13_variant;

  // Presorted list of cipher suites which should be explicitly prevented from
  // being used in addition to those disabled by the net built-in policy.
  //
  // Though cipher suites are sent in TLS as "uint8_t CipherSuite[2]", in
  // big-endian form, they should be declared in host byte order, with the
  // first uint8_t occupying the most significant byte.
  // Ex: To disable TLS_RSA_WITH_RC4_128_MD5, specify 0x0004, while to
  // disable TLS_ECDH_ECDSA_WITH_RC4_128_SHA, specify 0xC002.
  std::vector<uint16_t> disabled_cipher_suites;

  // Enables the version interference probing mode. While TLS 1.3 has avoided
  // most endpoint intolerance, middlebox interference with TLS 1.3 is
  // rampant. This causes the connection to be discarded on success with
  // ERR_SSL_VERSION_INTERFERENCE.
  bool version_interference_probe;

  bool channel_id_enabled;   // True if TLS channel ID extension is enabled.

  // List of Token Binding key parameters supported by the client. If empty,
  // Token Binding will be disabled, even if token_binding_enabled is true.
  std::vector<TokenBindingParam> token_binding_params;

  bool false_start_enabled;  // True if we'll use TLS False Start.
  // True if the Certificate Transparency signed_certificate_timestamp
  // TLS extension is enabled.
  bool signed_cert_timestamps_enabled;

  // If true, causes only ECDHE cipher suites to be enabled.
  bool require_ecdhe;

  // TODO(wtc): move the following members to a new SSLParams structure.  They
  // are not SSL configuration settings.

  struct NET_EXPORT CertAndStatus {
    CertAndStatus();
    CertAndStatus(scoped_refptr<X509Certificate> cert, CertStatus status);
    CertAndStatus(const CertAndStatus&);
    ~CertAndStatus();

    scoped_refptr<X509Certificate> cert;
    CertStatus cert_status = 0;
  };

  // Add any known-bad SSL certificate (with its cert status) to
  // |allowed_bad_certs| that should not trigger an ERR_CERT_* error when
  // calling SSLClientSocket::Connect.  This would normally be done in
  // response to the user explicitly accepting the bad certificate.
  std::vector<CertAndStatus> allowed_bad_certs;

  // True if we should send client_cert to the server.
  bool send_client_cert;

  bool verify_ev_cert;  // True if we should verify the certificate for EV.

  // If cert_io_enabled is false, then certificate verification will not
  // result in additional HTTP requests. (For example: to fetch missing
  // intermediates or to perform OCSP/CRL fetches.) It also implies that online
  // revocation checking is disabled.
  // NOTE: Only used by NSS.
  bool cert_io_enabled;

  // The list of application level protocols supported with ALPN (Application
  // Layer Protocol Negotation), in decreasing order of preference.  Protocols
  // will be advertised in this order during TLS handshake.
  NextProtoVector alpn_protos;

  // True if renegotiation should be allowed for the default application-level
  // protocol when the peer negotiates neither ALPN nor NPN.
  bool renego_allowed_default;

  // The list of application-level protocols to enable renegotiation for.
  NextProtoVector renego_allowed_for_protos;

  scoped_refptr<X509Certificate> client_cert;
  scoped_refptr<SSLPrivateKey> client_private_key;
};

}  // namespace net

#endif  // NET_SSL_SSL_CONFIG_H_
