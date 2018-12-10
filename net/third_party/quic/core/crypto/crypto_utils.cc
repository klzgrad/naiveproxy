// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/crypto_utils.h"

#include <memory>

#include "net/third_party/quic/core/crypto/aes_128_gcm_decrypter.h"
#include "net/third_party/quic/core/crypto/aes_128_gcm_encrypter.h"
#include "net/third_party/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quic/core/crypto/quic_hkdf.h"
#include "net/third_party/quic/core/crypto/quic_random.h"
#include "net/third_party/quic/core/quic_data_writer.h"
#include "net/third_party/quic/core/quic_time.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/hkdf.h"
#include "third_party/boringssl/src/include/openssl/sha.h"

namespace quic {

// static
std::vector<uint8_t> CryptoUtils::QhkdfExpand(
    const EVP_MD* prf,
    const std::vector<uint8_t>& secret,
    const QuicString& label,
    size_t out_len) {
  bssl::ScopedCBB quic_hkdf_label;
  CBB inner_label;
  const char label_prefix[] = "QUIC ";
  // The minimum possible length for the QuicHkdfLabel is 10 bytes - 2 bytes for
  // Length, plus 1 byte for the length of the inner label, plus the length of
  // that label (which is at least 6), plus 1 byte at the end.
  if (!CBB_init(quic_hkdf_label.get(), 10) ||
      !CBB_add_u16(quic_hkdf_label.get(), out_len) ||
      !CBB_add_u8_length_prefixed(quic_hkdf_label.get(), &inner_label) ||
      !CBB_add_bytes(&inner_label,
                     reinterpret_cast<const uint8_t*>(label_prefix),
                     QUIC_ARRAYSIZE(label_prefix) - 1) ||
      !CBB_add_bytes(&inner_label,
                     reinterpret_cast<const uint8_t*>(label.data()),
                     label.size()) ||
      !CBB_add_u8(quic_hkdf_label.get(), 0) ||
      !CBB_flush(quic_hkdf_label.get())) {
    QUIC_LOG(ERROR) << "Building HKDF label failed";
    return std::vector<uint8_t>();
  }
  std::vector<uint8_t> out;
  out.resize(out_len);
  if (!HKDF_expand(out.data(), out_len, prf, secret.data(), secret.size(),
                   CBB_data(quic_hkdf_label.get()),
                   CBB_len(quic_hkdf_label.get()))) {
    QUIC_LOG(ERROR) << "Running HKDF-Expand-Label failed";
    return std::vector<uint8_t>();
  }
  return out;
}

template void CryptoUtils::SetKeyAndIV(const EVP_MD* prf,
                                       const std::vector<uint8_t>& pp_secret,
                                       QuicEncrypter*);
template void CryptoUtils::SetKeyAndIV(const EVP_MD* prf,
                                       const std::vector<uint8_t>& pp_secret,
                                       QuicDecrypter*);

namespace {

const uint8_t kQuicVersion1Salt[] = {0xaf, 0xc8, 0x24, 0xec, 0x5f, 0xc7, 0x7e,
                                     0xca, 0x1e, 0x9d, 0x36, 0xf3, 0x7f, 0xb2,
                                     0xd4, 0x65, 0x18, 0xc3, 0x66, 0x39};

const char kPreSharedKeyLabel[] = "QUIC PSK";

}  // namespace

// static
void CryptoUtils::CreateTlsInitialCrypters(Perspective perspective,
                                           QuicConnectionId connection_id,
                                           CrypterPair* crypters) {
  const EVP_MD* hash = EVP_sha256();

  uint8_t connection_id_bytes[sizeof(connection_id)];
  for (size_t i = 0; i < sizeof(connection_id); ++i) {
    connection_id_bytes[i] =
        (connection_id >> ((sizeof(connection_id) - i - 1) * 8)) & 0xff;
  }

  std::vector<uint8_t> handshake_secret;
  handshake_secret.resize(EVP_MAX_MD_SIZE);
  size_t handshake_secret_len;
  if (!HKDF_extract(handshake_secret.data(), &handshake_secret_len, hash,
                    connection_id_bytes, arraysize(connection_id_bytes),
                    kQuicVersion1Salt, arraysize(kQuicVersion1Salt))) {
    QUIC_BUG << "HKDF_extract failed when creating initial crypters";
  }
  handshake_secret.resize(handshake_secret_len);

  const QuicString client_label = "client hs";
  const QuicString server_label = "server hs";
  QuicString encryption_label, decryption_label;
  if (perspective == Perspective::IS_CLIENT) {
    encryption_label = client_label;
    decryption_label = server_label;
  } else {
    encryption_label = server_label;
    decryption_label = client_label;
  }
  crypters->encrypter = QuicMakeUnique<Aes128GcmEncrypter>();
  std::vector<uint8_t> encryption_secret =
      QhkdfExpand(hash, handshake_secret, encryption_label, EVP_MD_size(hash));
  SetKeyAndIV(hash, encryption_secret, crypters->encrypter.get());

  crypters->decrypter = QuicMakeUnique<Aes128GcmDecrypter>();
  std::vector<uint8_t> decryption_secret =
      QhkdfExpand(hash, handshake_secret, decryption_label, EVP_MD_size(hash));
  SetKeyAndIV(hash, decryption_secret, crypters->decrypter.get());
}

// static
void CryptoUtils::GenerateNonce(QuicWallTime now,
                                QuicRandom* random_generator,
                                QuicStringPiece orbit,
                                QuicString* nonce) {
  // a 4-byte timestamp + 28 random bytes.
  nonce->reserve(kNonceSize);
  nonce->resize(kNonceSize);

  uint32_t gmt_unix_time = static_cast<uint32_t>(now.ToUNIXSeconds());
  // The time in the nonce must be encoded in big-endian because the
  // strike-register depends on the nonces being ordered by time.
  (*nonce)[0] = static_cast<char>(gmt_unix_time >> 24);
  (*nonce)[1] = static_cast<char>(gmt_unix_time >> 16);
  (*nonce)[2] = static_cast<char>(gmt_unix_time >> 8);
  (*nonce)[3] = static_cast<char>(gmt_unix_time);
  size_t bytes_written = 4;

  if (orbit.size() == 8) {
    memcpy(&(*nonce)[bytes_written], orbit.data(), orbit.size());
    bytes_written += orbit.size();
  }

  random_generator->RandBytes(&(*nonce)[bytes_written],
                              kNonceSize - bytes_written);
}

// static
bool CryptoUtils::DeriveKeys(QuicStringPiece premaster_secret,
                             QuicTag aead,
                             QuicStringPiece client_nonce,
                             QuicStringPiece server_nonce,
                             QuicStringPiece pre_shared_key,
                             const QuicString& hkdf_input,
                             Perspective perspective,
                             Diversification diversification,
                             CrypterPair* crypters,
                             QuicString* subkey_secret) {
  // If the connection is using PSK, concatenate it with the pre-master secret.
  std::unique_ptr<char[]> psk_premaster_secret;
  if (!pre_shared_key.empty()) {
    const QuicStringPiece label(kPreSharedKeyLabel);
    const size_t psk_premaster_secret_size = label.size() + 1 +
                                             pre_shared_key.size() + 8 +
                                             premaster_secret.size() + 8;

    psk_premaster_secret = QuicMakeUnique<char[]>(psk_premaster_secret_size);
    QuicDataWriter writer(psk_premaster_secret_size, psk_premaster_secret.get(),
                          HOST_BYTE_ORDER);

    if (!writer.WriteStringPiece(label) || !writer.WriteUInt8(0) ||
        !writer.WriteStringPiece(pre_shared_key) ||
        !writer.WriteUInt64(pre_shared_key.size()) ||
        !writer.WriteStringPiece(premaster_secret) ||
        !writer.WriteUInt64(premaster_secret.size()) ||
        writer.remaining() != 0) {
      return false;
    }

    premaster_secret =
        QuicStringPiece(psk_premaster_secret.get(), psk_premaster_secret_size);
  }

  crypters->encrypter = QuicEncrypter::Create(aead);
  crypters->decrypter = QuicDecrypter::Create(aead);
  size_t key_bytes = crypters->encrypter->GetKeySize();
  size_t nonce_prefix_bytes = crypters->encrypter->GetNoncePrefixSize();
  size_t subkey_secret_bytes =
      subkey_secret == nullptr ? 0 : premaster_secret.length();

  QuicStringPiece nonce = client_nonce;
  QuicString nonce_storage;
  if (!server_nonce.empty()) {
    nonce_storage = QuicString(client_nonce) + QuicString(server_nonce);
    nonce = nonce_storage;
  }

  QuicHKDF hkdf(premaster_secret, nonce, hkdf_input, key_bytes,
                nonce_prefix_bytes, subkey_secret_bytes);

  // Key derivation depends on the key diversification method being employed.
  // both the client and the server support never doing key diversification.
  // The server also supports immediate diversification, and the client
  // supports pending diversification.
  switch (diversification.mode()) {
    case Diversification::NEVER: {
      if (perspective == Perspective::IS_SERVER) {
        if (!crypters->encrypter->SetKey(hkdf.server_write_key()) ||
            !crypters->encrypter->SetNoncePrefix(hkdf.server_write_iv()) ||
            !crypters->decrypter->SetKey(hkdf.client_write_key()) ||
            !crypters->decrypter->SetNoncePrefix(hkdf.client_write_iv())) {
          return false;
        }
      } else {
        if (!crypters->encrypter->SetKey(hkdf.client_write_key()) ||
            !crypters->encrypter->SetNoncePrefix(hkdf.client_write_iv()) ||
            !crypters->decrypter->SetKey(hkdf.server_write_key()) ||
            !crypters->decrypter->SetNoncePrefix(hkdf.server_write_iv())) {
          return false;
        }
      }
      break;
    }
    case Diversification::PENDING: {
      if (perspective == Perspective::IS_SERVER) {
        QUIC_BUG << "Pending diversification is only for clients.";
        return false;
      }

      if (!crypters->encrypter->SetKey(hkdf.client_write_key()) ||
          !crypters->encrypter->SetNoncePrefix(hkdf.client_write_iv()) ||
          !crypters->decrypter->SetPreliminaryKey(hkdf.server_write_key()) ||
          !crypters->decrypter->SetNoncePrefix(hkdf.server_write_iv())) {
        return false;
      }
      break;
    }
    case Diversification::NOW: {
      if (perspective == Perspective::IS_CLIENT) {
        QUIC_BUG << "Immediate diversification is only for servers.";
        return false;
      }

      QuicString key, nonce_prefix;
      QuicDecrypter::DiversifyPreliminaryKey(
          hkdf.server_write_key(), hkdf.server_write_iv(),
          *diversification.nonce(), key_bytes, nonce_prefix_bytes, &key,
          &nonce_prefix);
      if (!crypters->decrypter->SetKey(hkdf.client_write_key()) ||
          !crypters->decrypter->SetNoncePrefix(hkdf.client_write_iv()) ||
          !crypters->encrypter->SetKey(key) ||
          !crypters->encrypter->SetNoncePrefix(nonce_prefix)) {
        return false;
      }
      break;
    }
    default:
      DCHECK(false);
  }

  if (subkey_secret != nullptr) {
    *subkey_secret = QuicString(hkdf.subkey_secret());
  }

  return true;
}

// static
bool CryptoUtils::ExportKeyingMaterial(QuicStringPiece subkey_secret,
                                       QuicStringPiece label,
                                       QuicStringPiece context,
                                       size_t result_len,
                                       QuicString* result) {
  for (size_t i = 0; i < label.length(); i++) {
    if (label[i] == '\0') {
      QUIC_LOG(ERROR) << "ExportKeyingMaterial label may not contain NULs";
      return false;
    }
  }
  // Create HKDF info input: null-terminated label + length-prefixed context
  if (context.length() >= std::numeric_limits<uint32_t>::max()) {
    QUIC_LOG(ERROR) << "Context value longer than 2^32";
    return false;
  }
  uint32_t context_length = static_cast<uint32_t>(context.length());
  QuicString info = QuicString(label);
  info.push_back('\0');
  info.append(reinterpret_cast<char*>(&context_length), sizeof(context_length));
  info.append(context.data(), context.length());

  QuicHKDF hkdf(subkey_secret, QuicStringPiece() /* no salt */, info,
                result_len, 0 /* no fixed IV */, 0 /* no subkey secret */);
  *result = QuicString(hkdf.client_write_key());
  return true;
}

// static
uint64_t CryptoUtils::ComputeLeafCertHash(QuicStringPiece cert) {
  return QuicUtils::FNV1a_64_Hash(cert);
}

QuicErrorCode CryptoUtils::ValidateServerHello(
    const CryptoHandshakeMessage& server_hello,
    const ParsedQuicVersionVector& negotiated_versions,
    QuicString* error_details) {
  DCHECK(error_details != nullptr);

  if (server_hello.tag() != kSHLO) {
    *error_details = "Bad tag";
    return QUIC_INVALID_CRYPTO_MESSAGE_TYPE;
  }

  QuicVersionLabelVector supported_version_labels;
  if (server_hello.GetVersionLabelList(kVER, &supported_version_labels) !=
      QUIC_NO_ERROR) {
    *error_details = "server hello missing version list";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  return ValidateServerHelloVersions(supported_version_labels,
                                     negotiated_versions, error_details);
}

QuicErrorCode CryptoUtils::ValidateServerHelloVersions(
    const QuicVersionLabelVector& server_versions,
    const ParsedQuicVersionVector& negotiated_versions,
    QuicString* error_details) {
  if (!negotiated_versions.empty()) {
    bool mismatch = server_versions.size() != negotiated_versions.size();
    for (size_t i = 0; i < server_versions.size() && !mismatch; ++i) {
      mismatch =
          server_versions[i] != CreateQuicVersionLabel(negotiated_versions[i]);
    }
    // The server sent a list of supported versions, and the connection
    // reports that there was a version negotiation during the handshake.
    // Ensure that these two lists are identical.
    if (mismatch) {
      *error_details = QuicStrCat(
          "Downgrade attack detected: ServerVersions(", server_versions.size(),
          ")[", QuicVersionLabelVectorToString(server_versions, ",", 30),
          "] NegotiatedVersions(", negotiated_versions.size(), ")[",
          ParsedQuicVersionVectorToString(negotiated_versions, ",", 30), "]");
      return QUIC_VERSION_NEGOTIATION_MISMATCH;
    }
  }
  return QUIC_NO_ERROR;
}

QuicErrorCode CryptoUtils::ValidateClientHello(
    const CryptoHandshakeMessage& client_hello,
    ParsedQuicVersion version,
    const ParsedQuicVersionVector& supported_versions,
    QuicString* error_details) {
  if (client_hello.tag() != kCHLO) {
    *error_details = "Bad tag";
    return QUIC_INVALID_CRYPTO_MESSAGE_TYPE;
  }

  // If the client's preferred version is not the version we are currently
  // speaking, then the client went through a version negotiation.  In this
  // case, we need to make sure that we actually do not support this version
  // and that it wasn't a downgrade attack.
  QuicVersionLabel client_version_label;
  if (client_hello.GetVersionLabel(kVER, &client_version_label) !=
      QUIC_NO_ERROR) {
    *error_details = "client hello missing version list";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }
  return ValidateClientHelloVersion(client_version_label, version,
                                    supported_versions, error_details);
}

QuicErrorCode CryptoUtils::ValidateClientHelloVersion(
    QuicVersionLabel client_version,
    ParsedQuicVersion connection_version,
    const ParsedQuicVersionVector& supported_versions,
    QuicString* error_details) {
  if (client_version != CreateQuicVersionLabel(connection_version)) {
    // Check to see if |client_version| is actually on the supported versions
    // list. If not, the server doesn't support that version and it's not a
    // downgrade attack.
    for (size_t i = 0; i < supported_versions.size(); ++i) {
      if (client_version == CreateQuicVersionLabel(supported_versions[i])) {
        *error_details = QuicStrCat(
            "Downgrade attack detected: ClientVersion[",
            QuicVersionLabelToString(client_version), "] SupportedVersions(",
            supported_versions.size(), ")[",
            ParsedQuicVersionVectorToString(supported_versions, ",", 30), "]");
        return QUIC_VERSION_NEGOTIATION_MISMATCH;
      }
    }
  }
  return QUIC_NO_ERROR;
}

#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x

// Returns the name of the HandshakeFailureReason as a char*
// static
const char* CryptoUtils::HandshakeFailureReasonToString(
    HandshakeFailureReason reason) {
  switch (reason) {
    RETURN_STRING_LITERAL(HANDSHAKE_OK);
    RETURN_STRING_LITERAL(CLIENT_NONCE_UNKNOWN_FAILURE);
    RETURN_STRING_LITERAL(CLIENT_NONCE_INVALID_FAILURE);
    RETURN_STRING_LITERAL(CLIENT_NONCE_NOT_UNIQUE_FAILURE);
    RETURN_STRING_LITERAL(CLIENT_NONCE_INVALID_ORBIT_FAILURE);
    RETURN_STRING_LITERAL(CLIENT_NONCE_INVALID_TIME_FAILURE);
    RETURN_STRING_LITERAL(CLIENT_NONCE_STRIKE_REGISTER_TIMEOUT);
    RETURN_STRING_LITERAL(CLIENT_NONCE_STRIKE_REGISTER_FAILURE);

    RETURN_STRING_LITERAL(SERVER_NONCE_DECRYPTION_FAILURE);
    RETURN_STRING_LITERAL(SERVER_NONCE_INVALID_FAILURE);
    RETURN_STRING_LITERAL(SERVER_NONCE_NOT_UNIQUE_FAILURE);
    RETURN_STRING_LITERAL(SERVER_NONCE_INVALID_TIME_FAILURE);
    RETURN_STRING_LITERAL(SERVER_NONCE_REQUIRED_FAILURE);

    RETURN_STRING_LITERAL(SERVER_CONFIG_INCHOATE_HELLO_FAILURE);
    RETURN_STRING_LITERAL(SERVER_CONFIG_UNKNOWN_CONFIG_FAILURE);

    RETURN_STRING_LITERAL(SOURCE_ADDRESS_TOKEN_INVALID_FAILURE);
    RETURN_STRING_LITERAL(SOURCE_ADDRESS_TOKEN_DECRYPTION_FAILURE);
    RETURN_STRING_LITERAL(SOURCE_ADDRESS_TOKEN_PARSE_FAILURE);
    RETURN_STRING_LITERAL(SOURCE_ADDRESS_TOKEN_DIFFERENT_IP_ADDRESS_FAILURE);
    RETURN_STRING_LITERAL(SOURCE_ADDRESS_TOKEN_CLOCK_SKEW_FAILURE);
    RETURN_STRING_LITERAL(SOURCE_ADDRESS_TOKEN_EXPIRED_FAILURE);

    RETURN_STRING_LITERAL(INVALID_EXPECTED_LEAF_CERTIFICATE);
    RETURN_STRING_LITERAL(MAX_FAILURE_REASON);
  }
  // Return a default value so that we return this when |reason| doesn't match
  // any HandshakeFailureReason.. This can happen when the message by the peer
  // (attacker) has invalid reason.
  return "INVALID_HANDSHAKE_FAILURE_REASON";
}

// static
void CryptoUtils::HashHandshakeMessage(const CryptoHandshakeMessage& message,
                                       QuicString* output,
                                       Perspective perspective) {
  const QuicData& serialized = message.GetSerialized(perspective);
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(serialized.data()),
         serialized.length(), digest);
  output->assign(reinterpret_cast<const char*>(digest), sizeof(digest));
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds
}  // namespace quic
