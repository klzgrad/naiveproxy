// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"

#include <memory>
#include <string>
#include <utility>

#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/hkdf.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/sha.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_hkdf.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

// Implements the HKDF-Expand-Label function as defined in section 7.1 of RFC
// 8446, except that it uses "quic " as the prefix instead of "tls13 ", as
// specified by draft-ietf-quic-tls-14. The HKDF-Expand-Label function takes 4
// explicit arguments (Secret, Label, Context, and Length), as well as
// implicit PRF which is the hash function negotiated by TLS. Its use in QUIC
// (as needed by the QUIC stack, instead of as used internally by the TLS
// stack) is only for deriving initial secrets for obfuscation and for
// calculating packet protection keys and IVs from the corresponding packet
// protection secret. Neither of these uses need a Context (a zero-length
// context is provided), so this argument is omitted here.
//
// The implicit PRF is explicitly passed into HkdfExpandLabel as |prf|; the
// Secret, Label, and Length are passed in as |secret|, |label|, and
// |out_len|, respectively. The resulting expanded secret is returned.
//
// TODO(nharper): HkdfExpandLabel and SetKeyAndIV (below) implement what is
// specified in draft-ietf-quic-tls-16. The latest editors' draft has changed
// derivation again, and this will need to be updated to reflect those (and any
// other future) changes.
std::vector<uint8_t> HkdfExpandLabel(const EVP_MD* prf,
                                     const std::vector<uint8_t>& secret,
                                     const std::string& label,
                                     size_t out_len) {
  bssl::ScopedCBB quic_hkdf_label;
  CBB inner_label;
  const char label_prefix[] = "tls13 ";
  // 19 = size(u16) + size(u8) + len("tls13 ") + len ("client in") + size(u8)
  static const size_t max_quic_hkdf_label_length = 19;
  if (!CBB_init(quic_hkdf_label.get(), max_quic_hkdf_label_length) ||
      !CBB_add_u16(quic_hkdf_label.get(), out_len) ||
      !CBB_add_u8_length_prefixed(quic_hkdf_label.get(), &inner_label) ||
      !CBB_add_bytes(&inner_label,
                     reinterpret_cast<const uint8_t*>(label_prefix),
                     QUICHE_ARRAYSIZE(label_prefix) - 1) ||
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

}  // namespace

void CryptoUtils::SetKeyAndIV(const EVP_MD* prf,
                              const std::vector<uint8_t>& pp_secret,
                              QuicCrypter* crypter) {
  std::vector<uint8_t> key =
      HkdfExpandLabel(prf, pp_secret, "quic key", crypter->GetKeySize());
  std::vector<uint8_t> iv =
      HkdfExpandLabel(prf, pp_secret, "quic iv", crypter->GetIVSize());
  std::vector<uint8_t> pn =
      HkdfExpandLabel(prf, pp_secret, "quic hp", crypter->GetKeySize());
  crypter->SetKey(quiche::QuicheStringPiece(reinterpret_cast<char*>(key.data()),
                                            key.size()));
  crypter->SetIV(
      quiche::QuicheStringPiece(reinterpret_cast<char*>(iv.data()), iv.size()));
  crypter->SetHeaderProtectionKey(
      quiche::QuicheStringPiece(reinterpret_cast<char*>(pn.data()), pn.size()));
}

namespace {

// Salt from https://tools.ietf.org/html/draft-ietf-quic-tls-27#section-5.2
const uint8_t kDraft27InitialSalt[] = {0xc3, 0xee, 0xf7, 0x12, 0xc7, 0x2e, 0xbb,
                                       0x5a, 0x11, 0xa7, 0xd2, 0x43, 0x2b, 0xb4,
                                       0x63, 0x65, 0xbe, 0xf9, 0xf5, 0x02};

// Salt from https://tools.ietf.org/html/draft-ietf-quic-tls-29#section-5.2
const uint8_t kDraft29InitialSalt[] = {0xaf, 0xbf, 0xec, 0x28, 0x99, 0x93, 0xd2,
                                       0x4c, 0x9e, 0x97, 0x86, 0xf1, 0x9c, 0x61,
                                       0x11, 0xe0, 0x43, 0x90, 0xa8, 0x99};

// Salts used by deployed versions of QUIC. When introducing a new version,
// generate a new salt by running `openssl rand -hex 20`.

// Salt to use for initial obfuscators in version Q050.
const uint8_t kQ050Salt[] = {0x50, 0x45, 0x74, 0xef, 0xd0, 0x66, 0xfe,
                             0x2f, 0x9d, 0x94, 0x5c, 0xfc, 0xdb, 0xd3,
                             0xa7, 0xf0, 0xd3, 0xb5, 0x6b, 0x45};
// Salt to use for initial obfuscators in version T050.
const uint8_t kT050Salt[] = {0x7f, 0xf5, 0x79, 0xe5, 0xac, 0xd0, 0x72,
                             0x91, 0x55, 0x80, 0x30, 0x4c, 0x43, 0xa2,
                             0x36, 0x7c, 0x60, 0x48, 0x83, 0x10};
// Salt to use for initial obfuscators in version T051.
const uint8_t kT051Salt[] = {0x7a, 0x4e, 0xde, 0xf4, 0xe7, 0xcc, 0xee,
                             0x5f, 0xa4, 0x50, 0x6c, 0x19, 0x12, 0x4f,
                             0xc8, 0xcc, 0xda, 0x6e, 0x03, 0x3d};

const uint8_t* InitialSaltForVersion(const ParsedQuicVersion& version,
                                     size_t* out_len) {
  static_assert(SupportedVersions().size() == 7u,
                "Supported versions out of sync with initial encryption salts");
  if (version == ParsedQuicVersion::Draft29()) {
    *out_len = QUICHE_ARRAYSIZE(kDraft29InitialSalt);
    return kDraft29InitialSalt;
  } else if (version == ParsedQuicVersion::Draft27() ||
             version == ParsedQuicVersion::ReservedForNegotiation()) {
    *out_len = QUICHE_ARRAYSIZE(kDraft27InitialSalt);
    return kDraft27InitialSalt;
  } else if (version == ParsedQuicVersion::T051()) {
    *out_len = QUICHE_ARRAYSIZE(kT051Salt);
    return kT051Salt;
  } else if (version == ParsedQuicVersion::T050()) {
    *out_len = QUICHE_ARRAYSIZE(kT050Salt);
    return kT050Salt;
  } else if (version == ParsedQuicVersion::Q050()) {
    *out_len = QUICHE_ARRAYSIZE(kQ050Salt);
    return kQ050Salt;
  }
  QUIC_BUG << "No initial obfuscation salt for version " << version;
  *out_len = QUICHE_ARRAYSIZE(kDraft27InitialSalt);
  return kDraft27InitialSalt;
}

const char kPreSharedKeyLabel[] = "QUIC PSK";

// Retry Integrity Protection Keys and Nonces.
// https://tools.ietf.org/html/draft-ietf-quic-tls-27#section-5.8
const uint8_t kDraft27RetryIntegrityKey[] = {0x4d, 0x32, 0xec, 0xdb, 0x2a, 0x21,
                                             0x33, 0xc8, 0x41, 0xe4, 0x04, 0x3d,
                                             0xf2, 0x7d, 0x44, 0x30};
const uint8_t kDraft27RetryIntegrityNonce[] = {
    0x4d, 0x16, 0x11, 0xd0, 0x55, 0x13, 0xa5, 0x52, 0xc5, 0x87, 0xd5, 0x75};

// https://tools.ietf.org/html/draft-ietf-quic-tls-29#section-5.8
const uint8_t kDraft29RetryIntegrityKey[] = {0xcc, 0xce, 0x18, 0x7e, 0xd0, 0x9a,
                                             0x09, 0xd0, 0x57, 0x28, 0x15, 0x5a,
                                             0x6c, 0xb9, 0x6b, 0xe1};
const uint8_t kDraft29RetryIntegrityNonce[] = {
    0xe5, 0x49, 0x30, 0xf9, 0x7f, 0x21, 0x36, 0xf0, 0x53, 0x0a, 0x8c, 0x1c};

// Keys used by Google versions of QUIC. When introducing a new version,
// generate a new key by running `openssl rand -hex 16`.
const uint8_t kT050RetryIntegrityKey[] = {0xc9, 0x2d, 0x32, 0x3d, 0x9c, 0xe3,
                                          0x0d, 0xa0, 0x88, 0xb9, 0xb7, 0xbb,
                                          0xdc, 0xcd, 0x50, 0xc8};
const uint8_t kT051RetryIntegrityKey[] = {0x2e, 0xb9, 0x61, 0xa6, 0x79, 0x56,
                                          0xf8, 0x79, 0x53, 0x14, 0xda, 0xfb,
                                          0x2e, 0xbc, 0x83, 0xd7};
// Nonces used by Google versions of QUIC. When introducing a new version,
// generate a new nonce by running `openssl rand -hex 12`.
const uint8_t kT050RetryIntegrityNonce[] = {0x26, 0xe4, 0xd6, 0x23, 0x83, 0xd5,
                                            0xc7, 0x60, 0xea, 0x02, 0xb4, 0x1f};
const uint8_t kT051RetryIntegrityNonce[] = {0xb5, 0x0e, 0x4e, 0x53, 0x4c, 0xfc,
                                            0x0b, 0xbb, 0x85, 0xf2, 0xf9, 0xca};

bool RetryIntegrityKeysForVersion(const ParsedQuicVersion& version,
                                  quiche::QuicheStringPiece* key,
                                  quiche::QuicheStringPiece* nonce) {
  static_assert(SupportedVersions().size() == 7u,
                "Supported versions out of sync with retry integrity keys");
  if (!version.HasRetryIntegrityTag()) {
    QUIC_BUG << "Attempted to get retry integrity keys for invalid version "
             << version;
    return false;
  } else if (version == ParsedQuicVersion::Draft29()) {
    *key = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kDraft29RetryIntegrityKey),
        QUICHE_ARRAYSIZE(kDraft29RetryIntegrityKey));
    *nonce = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kDraft29RetryIntegrityNonce),
        QUICHE_ARRAYSIZE(kDraft29RetryIntegrityNonce));
    return true;
  } else if (version == ParsedQuicVersion::Draft27()) {
    *key = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kDraft27RetryIntegrityKey),
        QUICHE_ARRAYSIZE(kDraft27RetryIntegrityKey));
    *nonce = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kDraft27RetryIntegrityNonce),
        QUICHE_ARRAYSIZE(kDraft27RetryIntegrityNonce));
    return true;
  } else if (version == ParsedQuicVersion::T051()) {
    *key = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kT051RetryIntegrityKey),
        QUICHE_ARRAYSIZE(kT051RetryIntegrityKey));
    *nonce = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kT051RetryIntegrityNonce),
        QUICHE_ARRAYSIZE(kT051RetryIntegrityNonce));
    return true;
  } else if (version == ParsedQuicVersion::T050()) {
    *key = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kT050RetryIntegrityKey),
        QUICHE_ARRAYSIZE(kT050RetryIntegrityKey));
    *nonce = quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(kT050RetryIntegrityNonce),
        QUICHE_ARRAYSIZE(kT050RetryIntegrityNonce));
    return true;
  }
  QUIC_BUG << "Attempted to get retry integrity keys for version " << version;
  return false;
}

}  // namespace

// static
void CryptoUtils::CreateInitialObfuscators(Perspective perspective,
                                           ParsedQuicVersion version,
                                           QuicConnectionId connection_id,
                                           CrypterPair* crypters) {
  QUIC_DLOG(INFO) << "Creating "
                  << (perspective == Perspective::IS_CLIENT ? "client"
                                                            : "server")
                  << " crypters for version " << version << " with CID "
                  << connection_id;
  if (!version.UsesInitialObfuscators()) {
    crypters->encrypter = std::make_unique<NullEncrypter>(perspective);
    crypters->decrypter = std::make_unique<NullDecrypter>(perspective);
    return;
  }
  QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(
      connection_id, version.transport_version))
      << "CreateTlsInitialCrypters: attempted to use connection ID "
      << connection_id << " which is invalid with version " << version;
  const EVP_MD* hash = EVP_sha256();

  size_t salt_len;
  const uint8_t* salt = InitialSaltForVersion(version, &salt_len);
  std::vector<uint8_t> handshake_secret;
  handshake_secret.resize(EVP_MAX_MD_SIZE);
  size_t handshake_secret_len;
  const bool hkdf_extract_success =
      HKDF_extract(handshake_secret.data(), &handshake_secret_len, hash,
                   reinterpret_cast<const uint8_t*>(connection_id.data()),
                   connection_id.length(), salt, salt_len);
  QUIC_BUG_IF(!hkdf_extract_success)
      << "HKDF_extract failed when creating initial crypters";
  handshake_secret.resize(handshake_secret_len);

  const std::string client_label = "client in";
  const std::string server_label = "server in";
  std::string encryption_label, decryption_label;
  if (perspective == Perspective::IS_CLIENT) {
    encryption_label = client_label;
    decryption_label = server_label;
  } else {
    encryption_label = server_label;
    decryption_label = client_label;
  }
  std::vector<uint8_t> encryption_secret = HkdfExpandLabel(
      hash, handshake_secret, encryption_label, EVP_MD_size(hash));
  crypters->encrypter = std::make_unique<Aes128GcmEncrypter>();
  SetKeyAndIV(hash, encryption_secret, crypters->encrypter.get());

  std::vector<uint8_t> decryption_secret = HkdfExpandLabel(
      hash, handshake_secret, decryption_label, EVP_MD_size(hash));
  crypters->decrypter = std::make_unique<Aes128GcmDecrypter>();
  SetKeyAndIV(hash, decryption_secret, crypters->decrypter.get());
}

// static
bool CryptoUtils::ValidateRetryIntegrityTag(
    ParsedQuicVersion version,
    QuicConnectionId original_connection_id,
    quiche::QuicheStringPiece retry_without_tag,
    quiche::QuicheStringPiece integrity_tag) {
  unsigned char computed_integrity_tag[kRetryIntegrityTagLength];
  if (integrity_tag.length() != QUICHE_ARRAYSIZE(computed_integrity_tag)) {
    QUIC_BUG << "Invalid retry integrity tag length " << integrity_tag.length();
    return false;
  }
  char retry_pseudo_packet[kMaxIncomingPacketSize + 256];
  QuicDataWriter writer(QUICHE_ARRAYSIZE(retry_pseudo_packet),
                        retry_pseudo_packet);
  if (!writer.WriteLengthPrefixedConnectionId(original_connection_id)) {
    QUIC_BUG << "Failed to write original connection ID in retry pseudo packet";
    return false;
  }
  if (!writer.WriteStringPiece(retry_without_tag)) {
    QUIC_BUG << "Failed to write retry without tag in retry pseudo packet";
    return false;
  }
  quiche::QuicheStringPiece key;
  quiche::QuicheStringPiece nonce;
  if (!RetryIntegrityKeysForVersion(version, &key, &nonce)) {
    // RetryIntegrityKeysForVersion already logs failures.
    return false;
  }
  Aes128GcmEncrypter crypter;
  crypter.SetKey(key);
  quiche::QuicheStringPiece associated_data(writer.data(), writer.length());
  quiche::QuicheStringPiece plaintext;  // Plaintext is empty.
  if (!crypter.Encrypt(nonce, associated_data, plaintext,
                       computed_integrity_tag)) {
    QUIC_BUG << "Failed to compute retry integrity tag";
    return false;
  }
  if (CRYPTO_memcmp(computed_integrity_tag, integrity_tag.data(),
                    QUICHE_ARRAYSIZE(computed_integrity_tag)) != 0) {
    QUIC_DLOG(ERROR) << "Failed to validate retry integrity tag";
    return false;
  }
  return true;
}

// static
void CryptoUtils::GenerateNonce(QuicWallTime now,
                                QuicRandom* random_generator,
                                quiche::QuicheStringPiece orbit,
                                std::string* nonce) {
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
bool CryptoUtils::DeriveKeys(const ParsedQuicVersion& version,
                             quiche::QuicheStringPiece premaster_secret,
                             QuicTag aead,
                             quiche::QuicheStringPiece client_nonce,
                             quiche::QuicheStringPiece server_nonce,
                             quiche::QuicheStringPiece pre_shared_key,
                             const std::string& hkdf_input,
                             Perspective perspective,
                             Diversification diversification,
                             CrypterPair* crypters,
                             std::string* subkey_secret) {
  // If the connection is using PSK, concatenate it with the pre-master secret.
  std::unique_ptr<char[]> psk_premaster_secret;
  if (!pre_shared_key.empty()) {
    const quiche::QuicheStringPiece label(kPreSharedKeyLabel);
    const size_t psk_premaster_secret_size = label.size() + 1 +
                                             pre_shared_key.size() + 8 +
                                             premaster_secret.size() + 8;

    psk_premaster_secret = std::make_unique<char[]>(psk_premaster_secret_size);
    QuicDataWriter writer(psk_premaster_secret_size, psk_premaster_secret.get(),
                          quiche::HOST_BYTE_ORDER);

    if (!writer.WriteStringPiece(label) || !writer.WriteUInt8(0) ||
        !writer.WriteStringPiece(pre_shared_key) ||
        !writer.WriteUInt64(pre_shared_key.size()) ||
        !writer.WriteStringPiece(premaster_secret) ||
        !writer.WriteUInt64(premaster_secret.size()) ||
        writer.remaining() != 0) {
      return false;
    }

    premaster_secret = quiche::QuicheStringPiece(psk_premaster_secret.get(),
                                                 psk_premaster_secret_size);
  }

  crypters->encrypter = QuicEncrypter::Create(version, aead);
  crypters->decrypter = QuicDecrypter::Create(version, aead);

  size_t key_bytes = crypters->encrypter->GetKeySize();
  size_t nonce_prefix_bytes = crypters->encrypter->GetNoncePrefixSize();
  if (version.UsesInitialObfuscators()) {
    nonce_prefix_bytes = crypters->encrypter->GetIVSize();
  }
  size_t subkey_secret_bytes =
      subkey_secret == nullptr ? 0 : premaster_secret.length();

  quiche::QuicheStringPiece nonce = client_nonce;
  std::string nonce_storage;
  if (!server_nonce.empty()) {
    nonce_storage = std::string(client_nonce) + std::string(server_nonce);
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
            !crypters->encrypter->SetNoncePrefixOrIV(version,
                                                     hkdf.server_write_iv()) ||
            !crypters->encrypter->SetHeaderProtectionKey(
                hkdf.server_hp_key()) ||
            !crypters->decrypter->SetKey(hkdf.client_write_key()) ||
            !crypters->decrypter->SetNoncePrefixOrIV(version,
                                                     hkdf.client_write_iv()) ||
            !crypters->decrypter->SetHeaderProtectionKey(
                hkdf.client_hp_key())) {
          return false;
        }
      } else {
        if (!crypters->encrypter->SetKey(hkdf.client_write_key()) ||
            !crypters->encrypter->SetNoncePrefixOrIV(version,
                                                     hkdf.client_write_iv()) ||
            !crypters->encrypter->SetHeaderProtectionKey(
                hkdf.client_hp_key()) ||
            !crypters->decrypter->SetKey(hkdf.server_write_key()) ||
            !crypters->decrypter->SetNoncePrefixOrIV(version,
                                                     hkdf.server_write_iv()) ||
            !crypters->decrypter->SetHeaderProtectionKey(
                hkdf.server_hp_key())) {
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
          !crypters->encrypter->SetNoncePrefixOrIV(version,
                                                   hkdf.client_write_iv()) ||
          !crypters->encrypter->SetHeaderProtectionKey(hkdf.client_hp_key()) ||
          !crypters->decrypter->SetPreliminaryKey(hkdf.server_write_key()) ||
          !crypters->decrypter->SetNoncePrefixOrIV(version,
                                                   hkdf.server_write_iv()) ||
          !crypters->decrypter->SetHeaderProtectionKey(hkdf.server_hp_key())) {
        return false;
      }
      break;
    }
    case Diversification::NOW: {
      if (perspective == Perspective::IS_CLIENT) {
        QUIC_BUG << "Immediate diversification is only for servers.";
        return false;
      }

      std::string key, nonce_prefix;
      QuicDecrypter::DiversifyPreliminaryKey(
          hkdf.server_write_key(), hkdf.server_write_iv(),
          *diversification.nonce(), key_bytes, nonce_prefix_bytes, &key,
          &nonce_prefix);
      if (!crypters->decrypter->SetKey(hkdf.client_write_key()) ||
          !crypters->decrypter->SetNoncePrefixOrIV(version,
                                                   hkdf.client_write_iv()) ||
          !crypters->decrypter->SetHeaderProtectionKey(hkdf.client_hp_key()) ||
          !crypters->encrypter->SetKey(key) ||
          !crypters->encrypter->SetNoncePrefixOrIV(version, nonce_prefix) ||
          !crypters->encrypter->SetHeaderProtectionKey(hkdf.server_hp_key())) {
        return false;
      }
      break;
    }
    default:
      DCHECK(false);
  }

  if (subkey_secret != nullptr) {
    *subkey_secret = std::string(hkdf.subkey_secret());
  }

  return true;
}

// static
bool CryptoUtils::ExportKeyingMaterial(quiche::QuicheStringPiece subkey_secret,
                                       quiche::QuicheStringPiece label,
                                       quiche::QuicheStringPiece context,
                                       size_t result_len,
                                       std::string* result) {
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
  std::string info = std::string(label);
  info.push_back('\0');
  info.append(reinterpret_cast<char*>(&context_length), sizeof(context_length));
  info.append(context.data(), context.length());

  QuicHKDF hkdf(subkey_secret, quiche::QuicheStringPiece() /* no salt */, info,
                result_len, 0 /* no fixed IV */, 0 /* no subkey secret */);
  *result = std::string(hkdf.client_write_key());
  return true;
}

// static
uint64_t CryptoUtils::ComputeLeafCertHash(quiche::QuicheStringPiece cert) {
  return QuicUtils::FNV1a_64_Hash(cert);
}

QuicErrorCode CryptoUtils::ValidateServerHello(
    const CryptoHandshakeMessage& server_hello,
    const ParsedQuicVersionVector& negotiated_versions,
    std::string* error_details) {
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
    std::string* error_details) {
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
      *error_details = quiche::QuicheStrCat(
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
    std::string* error_details) {
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
    std::string* error_details) {
  if (client_version != CreateQuicVersionLabel(connection_version)) {
    // Check to see if |client_version| is actually on the supported versions
    // list. If not, the server doesn't support that version and it's not a
    // downgrade attack.
    for (size_t i = 0; i < supported_versions.size(); ++i) {
      if (client_version == CreateQuicVersionLabel(supported_versions[i])) {
        *error_details = quiche::QuicheStrCat(
            "Downgrade attack detected: ClientVersion[",
            QuicVersionLabelToString(client_version), "] ConnectionVersion[",
            ParsedQuicVersionToString(connection_version),
            "] SupportedVersions(", supported_versions.size(), ")[",
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
const char* CryptoUtils::EarlyDataReasonToString(
    ssl_early_data_reason_t reason) {
  switch (reason) {
    RETURN_STRING_LITERAL(ssl_early_data_unknown);
    RETURN_STRING_LITERAL(ssl_early_data_disabled);
    RETURN_STRING_LITERAL(ssl_early_data_accepted);
    RETURN_STRING_LITERAL(ssl_early_data_protocol_version);
    RETURN_STRING_LITERAL(ssl_early_data_peer_declined);
    RETURN_STRING_LITERAL(ssl_early_data_no_session_offered);
    RETURN_STRING_LITERAL(ssl_early_data_session_not_resumed);
    RETURN_STRING_LITERAL(ssl_early_data_unsupported_for_session);
    RETURN_STRING_LITERAL(ssl_early_data_hello_retry_request);
    RETURN_STRING_LITERAL(ssl_early_data_alpn_mismatch);
    RETURN_STRING_LITERAL(ssl_early_data_channel_id);
    RETURN_STRING_LITERAL(ssl_early_data_token_binding);
    RETURN_STRING_LITERAL(ssl_early_data_ticket_age_skew);
    RETURN_STRING_LITERAL(ssl_early_data_quic_parameter_mismatch);
  }
  QUIC_BUG_IF(reason < 0 || reason > ssl_early_data_reason_max_value)
      << "Unknown ssl_early_data_reason_t " << reason;
  return "unknown ssl_early_data_reason_t";
}

// static
std::string CryptoUtils::HashHandshakeMessage(
    const CryptoHandshakeMessage& message,
    Perspective /*perspective*/) {
  std::string output;
  const QuicData& serialized = message.GetSerialized();
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(serialized.data()),
         serialized.length(), digest);
  output.assign(reinterpret_cast<const char*>(digest), sizeof(digest));
  return output;
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds
}  // namespace quic
