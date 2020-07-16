// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_HANDSHAKE_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_HANDSHAKE_H_

#include <memory>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class CommonCertSets;
class SynchronousKeyExchange;
class QuicDecrypter;
class QuicEncrypter;

// HandshakeFailureReason enum values are uploaded to UMA, they cannot be
// changed.
enum HandshakeFailureReason {
  HANDSHAKE_OK = 0,

  // Failure reasons for an invalid client nonce in CHLO.
  //
  // The default error value for nonce verification failures from strike
  // register (covers old strike registers and unknown failures).
  CLIENT_NONCE_UNKNOWN_FAILURE = 1,
  // Client nonce had incorrect length.
  CLIENT_NONCE_INVALID_FAILURE = 2,
  // Client nonce is not unique.
  CLIENT_NONCE_NOT_UNIQUE_FAILURE = 3,
  // Client orbit is invalid or incorrect.
  CLIENT_NONCE_INVALID_ORBIT_FAILURE = 4,
  // Client nonce's timestamp is not in the strike register's valid time range.
  CLIENT_NONCE_INVALID_TIME_FAILURE = 5,
  // Strike register's RPC call timed out, client nonce couldn't be verified.
  CLIENT_NONCE_STRIKE_REGISTER_TIMEOUT = 6,
  // Strike register is down, client nonce couldn't be verified.
  CLIENT_NONCE_STRIKE_REGISTER_FAILURE = 7,

  // Failure reasons for an invalid server nonce in CHLO.
  //
  // Unbox of server nonce failed.
  SERVER_NONCE_DECRYPTION_FAILURE = 8,
  // Decrypted server nonce had incorrect length.
  SERVER_NONCE_INVALID_FAILURE = 9,
  // Server nonce is not unique.
  SERVER_NONCE_NOT_UNIQUE_FAILURE = 10,
  // Server nonce's timestamp is not in the strike register's valid time range.
  SERVER_NONCE_INVALID_TIME_FAILURE = 11,
  // The server requires handshake confirmation.
  SERVER_NONCE_REQUIRED_FAILURE = 20,

  // Failure reasons for an invalid server config in CHLO.
  //
  // Missing Server config id (kSCID) tag.
  SERVER_CONFIG_INCHOATE_HELLO_FAILURE = 12,
  // Couldn't find the Server config id (kSCID).
  SERVER_CONFIG_UNKNOWN_CONFIG_FAILURE = 13,

  // Failure reasons for an invalid source-address token.
  //
  // Missing Source-address token (kSourceAddressTokenTag) tag.
  SOURCE_ADDRESS_TOKEN_INVALID_FAILURE = 14,
  // Unbox of Source-address token failed.
  SOURCE_ADDRESS_TOKEN_DECRYPTION_FAILURE = 15,
  // Couldn't parse the unbox'ed Source-address token.
  SOURCE_ADDRESS_TOKEN_PARSE_FAILURE = 16,
  // Source-address token is for a different IP address.
  SOURCE_ADDRESS_TOKEN_DIFFERENT_IP_ADDRESS_FAILURE = 17,
  // The source-address token has a timestamp in the future.
  SOURCE_ADDRESS_TOKEN_CLOCK_SKEW_FAILURE = 18,
  // The source-address token has expired.
  SOURCE_ADDRESS_TOKEN_EXPIRED_FAILURE = 19,

  // The expected leaf certificate hash could not be validated.
  INVALID_EXPECTED_LEAF_CERTIFICATE = 21,

  MAX_FAILURE_REASON = 22,
};

// These errors will be packed into an uint32_t and we don't want to set the
// most significant bit, which may be misinterpreted as the sign bit.
static_assert(MAX_FAILURE_REASON <= 32, "failure reason out of sync");

// A CrypterPair contains the encrypter and decrypter for an encryption level.
struct QUIC_EXPORT_PRIVATE CrypterPair {
  CrypterPair();
  CrypterPair(CrypterPair&&) = default;
  ~CrypterPair();

  std::unique_ptr<QuicEncrypter> encrypter;
  std::unique_ptr<QuicDecrypter> decrypter;
};

// Parameters negotiated by the crypto handshake.
struct QUIC_EXPORT_PRIVATE QuicCryptoNegotiatedParameters
    : public QuicReferenceCounted {
  // Initializes the members to 0 or empty values.
  QuicCryptoNegotiatedParameters();

  QuicTag key_exchange;
  QuicTag aead;
  std::string initial_premaster_secret;
  std::string forward_secure_premaster_secret;
  // initial_subkey_secret is used as the PRK input to the HKDF used when
  // performing key extraction that needs to happen before forward-secure keys
  // are available.
  std::string initial_subkey_secret;
  // subkey_secret is used as the PRK input to the HKDF used for key extraction.
  std::string subkey_secret;
  CrypterPair initial_crypters;
  CrypterPair forward_secure_crypters;
  // Normalized SNI: converted to lower case and trailing '.' removed.
  std::string sni;
  std::string client_nonce;
  std::string server_nonce;
  // hkdf_input_suffix contains the HKDF input following the label: the
  // ConnectionId, client hello and server config. This is only populated in the
  // client because only the client needs to derive the forward secure keys at a
  // later time from the initial keys.
  std::string hkdf_input_suffix;
  // cached_certs contains the cached certificates that a client used when
  // sending a client hello.
  std::vector<std::string> cached_certs;
  // client_key_exchange is used by clients to store the ephemeral KeyExchange
  // for the connection.
  std::unique_ptr<SynchronousKeyExchange> client_key_exchange;
  // channel_id is set by servers to a ChannelID key when the client correctly
  // proves possession of the corresponding private key. It consists of 32
  // bytes of x coordinate, followed by 32 bytes of y coordinate. Both values
  // are big-endian and the pair is a P-256 public key.
  std::string channel_id;
  QuicTag token_binding_key_param;

  // Used when generating proof signature when sending server config updates.

  // Used to generate cert chain when sending server config updates.
  std::string client_common_set_hashes;
  std::string client_cached_cert_hashes;

  // Default to false; set to true if the client indicates that it supports sct
  // by sending CSCT tag with an empty value in client hello.
  bool sct_supported_by_client;

  // Parameters only populated for TLS handshakes. These will be 0 for
  // connections not using TLS, or if the TLS handshake is not finished yet.
  uint16_t cipher_suite = 0;
  uint16_t key_exchange_group = 0;
  uint16_t peer_signature_algorithm = 0;

 protected:
  ~QuicCryptoNegotiatedParameters() override;
};

// QuicCryptoConfig contains common configuration between clients and servers.
class QUIC_EXPORT_PRIVATE QuicCryptoConfig {
 public:
  // kInitialLabel is a constant that is used when deriving the initial
  // (non-forward secure) keys for the connection in order to tie the resulting
  // key to this protocol.
  static const char kInitialLabel[];

  // kCETVLabel is a constant that is used when deriving the keys for the
  // encrypted tag/value block in the client hello.
  static const char kCETVLabel[];

  // kForwardSecureLabel is a constant that is used when deriving the forward
  // secure keys for the connection in order to tie the resulting key to this
  // protocol.
  static const char kForwardSecureLabel[];

  QuicCryptoConfig();
  QuicCryptoConfig(const QuicCryptoConfig&) = delete;
  QuicCryptoConfig& operator=(const QuicCryptoConfig&) = delete;
  ~QuicCryptoConfig();

  // Key exchange methods. The following two members' values correspond by
  // index.
  QuicTagVector kexs;
  // Authenticated encryption with associated data (AEAD) algorithms.
  QuicTagVector aead;

  const CommonCertSets* common_cert_sets;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_HANDSHAKE_H_
