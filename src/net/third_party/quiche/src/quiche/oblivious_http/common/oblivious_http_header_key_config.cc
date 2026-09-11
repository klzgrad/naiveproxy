#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

#include <stdbool.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_status_utils.h"

namespace quiche {
namespace {

// Size of KEM ID is 2 bytes. Refer to OHTTP Key Config in the RFC:
// https://www.rfc-editor.org/rfc/rfc9458.html#section-3.1-2
constexpr size_t kSizeOfHpkeKemId = 2;

// Size of Symmetric algorithms is 2 bytes(16 bits) each.
// Refer to HPKE Symmetric Algorithms configuration in the RFC:
// https://www.rfc-editor.org/rfc/rfc9458.html#section-3.1-2
constexpr size_t kSizeOfSymmetricAlgorithmHpkeKdfId = 2;
constexpr size_t kSizeOfSymmetricAlgorithmHpkeAeadId = 2;

}  // namespace

absl::StatusOr<const EVP_HPKE_KEM*> CheckKemId(uint16_t kem_id) {
  switch (kem_id) {
    case EVP_HPKE_DHKEM_P256_HKDF_SHA256:
      return EVP_hpke_p256_hkdf_sha256();
    case EVP_HPKE_DHKEM_X25519_HKDF_SHA256:
      return EVP_hpke_x25519_hkdf_sha256();
    case EVP_HPKE_XWING:
      return EVP_hpke_xwing();
    case EVP_HPKE_MLKEM768:
      return EVP_hpke_mlkem768();
    case EVP_HPKE_MLKEM1024:
      return EVP_hpke_mlkem1024();
    default:
      return absl::UnimplementedError(
          absl::StrCat("KEM ID", absl::Hex(kem_id), " not supported"));
  }
}

absl::StatusOr<const EVP_HPKE_KDF*> CheckKdfId(uint16_t kdf_id) {
  switch (kdf_id) {
    case EVP_HPKE_HKDF_SHA256:
      return EVP_hpke_hkdf_sha256();
    case EVP_HPKE_HKDF_SHA384:
      return EVP_hpke_hkdf_sha384();
    default:
      return absl::UnimplementedError(
          absl::StrCat("KDF ID ", absl::Hex(kdf_id), " not supported"));
  }
}

absl::StatusOr<const EVP_HPKE_AEAD*> CheckAeadId(uint16_t aead_id) {
  switch (aead_id) {
    case EVP_HPKE_AES_128_GCM:
      return EVP_hpke_aes_128_gcm();
    case EVP_HPKE_AES_256_GCM:
      return EVP_hpke_aes_256_gcm();
    case EVP_HPKE_CHACHA20_POLY1305:
      return EVP_hpke_chacha20_poly1305();
    default:
      return absl::UnimplementedError(
          absl::StrCat("AEAD ID ", absl::Hex(aead_id), " not supported"));
  }
}

ObliviousHttpHeaderKeyConfig::ObliviousHttpHeaderKeyConfig(uint8_t key_id,
                                                           uint16_t kem_id,
                                                           uint16_t kdf_id,
                                                           uint16_t aead_id)
    : key_id_(key_id), kem_id_(kem_id), kdf_id_(kdf_id), aead_id_(aead_id) {}

absl::StatusOr<ObliviousHttpHeaderKeyConfig>
ObliviousHttpHeaderKeyConfig::Create(uint8_t key_id, uint16_t kem_id,
                                     uint16_t kdf_id, uint16_t aead_id) {
  ObliviousHttpHeaderKeyConfig instance(key_id, kem_id, kdf_id, aead_id);
  QUICHE_RETURN_IF_ERROR(instance.ValidateKeyConfig());
  return instance;
}

absl::Status ObliviousHttpHeaderKeyConfig::ValidateKeyConfig() const {
  QUICHE_RETURN_IF_ERROR(CheckKemId(kem_id_).status());
  QUICHE_RETURN_IF_ERROR(CheckKdfId(kdf_id_).status());
  QUICHE_RETURN_IF_ERROR(CheckAeadId(aead_id_).status());
  return absl::OkStatus();
}

const EVP_HPKE_KEM* ObliviousHttpHeaderKeyConfig::GetHpkeKem() const {
  auto kem = CheckKemId(kem_id_);
  QUICHE_CHECK_OK(kem.status());
  return *kem;
}
const EVP_HPKE_KDF* ObliviousHttpHeaderKeyConfig::GetHpkeKdf() const {
  auto kdf = CheckKdfId(kdf_id_);
  QUICHE_CHECK_OK(kdf.status());
  return *kdf;
}
const EVP_HPKE_AEAD* ObliviousHttpHeaderKeyConfig::GetHpkeAead() const {
  auto aead = CheckAeadId(aead_id_);
  QUICHE_CHECK_OK(aead.status());
  return *aead;
}

std::string ObliviousHttpHeaderKeyConfig::SerializeRecipientContextInfo(
    absl::string_view request_label) const {
  uint8_t zero_byte = 0x00;
  int buf_len = request_label.size() + kHeaderLength + sizeof(zero_byte);
  std::string info(buf_len, '\0');
  QuicheDataWriter writer(info.size(), info.data());
  QUICHE_CHECK(writer.WriteStringPiece(request_label));
  QUICHE_CHECK(writer.WriteUInt8(zero_byte));
  QUICHE_CHECK(writer.WriteUInt8(key_id_));
  QUICHE_CHECK(writer.WriteUInt16(kem_id_));
  QUICHE_CHECK(writer.WriteUInt16(kdf_id_));
  QUICHE_CHECK(writer.WriteUInt16(aead_id_));
  return info;
}

// https://www.rfc-editor.org/rfc/rfc9458.html#section-4.3
absl::Status ObliviousHttpHeaderKeyConfig::ParseOhttpPayloadHeader(
    absl::string_view payload_bytes) const {
  if (payload_bytes.empty()) {
    return absl::InvalidArgumentError("Empty request payload");
  }
  QuicheDataReader reader(payload_bytes);
  return ParseOhttpPayloadHeader(reader);
}

absl::Status ObliviousHttpHeaderKeyConfig::ParseOhttpPayloadHeader(
    QuicheDataReader& reader) const {
  uint8_t key_id;
  if (!reader.ReadUInt8(&key_id)) {
    return absl::InvalidArgumentError("Failed to read key_id from header");
  }
  if (key_id != key_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("KeyID ", static_cast<uint16_t>(key_id),
                     " in request does not match KeyID ",
                     static_cast<uint16_t>(key_id_), " from config"));
  }
  uint16_t kem_id;
  if (!reader.ReadUInt16(&kem_id)) {
    return absl::InvalidArgumentError("Failed to read kem_id from header");
  }
  if (kem_id != kem_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Received invalid kem_id ", absl::Hex(kem_id),
                     ", expected ", absl::Hex(kem_id_)));
  }
  uint16_t kdf_id;
  if (!reader.ReadUInt16(&kdf_id)) {
    return absl::InvalidArgumentError("Failed to read kdf_id from header");
  }
  if (kdf_id != kdf_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Received invalid kdf_id ", absl::Hex(kdf_id),
                     ", expected ", absl::Hex(kdf_id_)));
  }
  uint16_t aead_id;
  if (!reader.ReadUInt16(&aead_id)) {
    return absl::InvalidArgumentError("Failed to read aead_id from header");
  }
  if (aead_id != aead_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Received invalid aead_id ", absl::Hex(aead_id),
                     ", expected ", absl::Hex(aead_id_)));
  }
  return absl::OkStatus();
}

absl::StatusOr<uint8_t>
ObliviousHttpHeaderKeyConfig::ParseKeyIdFromObliviousHttpRequestPayload(
    absl::string_view payload_bytes) {
  if (payload_bytes.empty()) {
    return absl::InvalidArgumentError("Empty request payload");
  }
  QuicheDataReader reader(payload_bytes);
  uint8_t key_id;
  if (!reader.ReadUInt8(&key_id)) {
    return absl::InvalidArgumentError("Failed to read key_id from payload");
  }
  return key_id;
}

std::string ObliviousHttpHeaderKeyConfig::SerializeOhttpPayloadHeader() const {
  int buf_len =
      sizeof(key_id_) + sizeof(kem_id_) + sizeof(kdf_id_) + sizeof(aead_id_);
  std::string hdr(buf_len, '\0');
  QuicheDataWriter writer(hdr.size(), hdr.data());
  QUICHE_CHECK(writer.WriteUInt8(key_id_));
  QUICHE_CHECK(writer.WriteUInt16(kem_id_));
  QUICHE_CHECK(writer.WriteUInt16(kdf_id_));
  QUICHE_CHECK(writer.WriteUInt16(aead_id_));
  return hdr;
}

namespace {
// https://www.rfc-editor.org/rfc/rfc9180#section-7.1
absl::StatusOr<uint16_t> KeyLength(uint16_t kem_id) {
  QUICHE_ASSIGN_OR_RETURN(const EVP_HPKE_KEM* supported_kem,
                          CheckKemId(kem_id));
  return EVP_HPKE_KEM_public_key_len(supported_kem);
}

absl::StatusOr<std::string> SerializeOhttpKeyWithPublicKey(
    uint8_t key_id, absl::string_view public_key,
    const std::vector<ObliviousHttpHeaderKeyConfig>& ohttp_configs) {
  if (ohttp_configs.empty()) {
    return absl::InvalidArgumentError("Empty ohttp_configs");
  }
  auto ohttp_config = ohttp_configs[0];
  static_assert(sizeof(ohttp_config.GetHpkeKemId()) == kSizeOfHpkeKemId &&
                    sizeof(ohttp_config.GetHpkeKdfId()) ==
                        kSizeOfSymmetricAlgorithmHpkeKdfId &&
                    sizeof(ohttp_config.GetHpkeAeadId()) ==
                        kSizeOfSymmetricAlgorithmHpkeAeadId,
                "Bad algorithm ID sizes");

  uint16_t symmetric_algs_length =
      ohttp_configs.size() * (kSizeOfSymmetricAlgorithmHpkeKdfId +
                              kSizeOfSymmetricAlgorithmHpkeAeadId);
  int buf_len = sizeof(key_id) + kSizeOfHpkeKemId + public_key.size() +
                sizeof(symmetric_algs_length) + symmetric_algs_length;
  std::string ohttp_key_configuration(buf_len, '\0');
  QuicheDataWriter writer(ohttp_key_configuration.size(),
                          ohttp_key_configuration.data());
  QUICHE_CHECK(writer.WriteUInt8(key_id));
  QUICHE_CHECK(writer.WriteUInt16(ohttp_config.GetHpkeKemId()));
  QUICHE_CHECK(writer.WriteStringPiece(public_key));
  QUICHE_CHECK(writer.WriteUInt16(symmetric_algs_length));
  for (const auto& item : ohttp_configs) {
    // Check if KEM ID is the same for all the configs stored in `this` for
    // given `key_id`.
    if (item.GetHpkeKemId() != ohttp_config.GetHpkeKemId()) {
      return absl::InternalError(
          absl::StrCat("ObliviousHttpKeyConfigs object cannot hold ConfigMap "
                       "of different KEM IDs ",
                       absl::Hex(item.GetHpkeKemId()), " vs ",
                       absl::Hex(ohttp_config.GetHpkeKemId()), " for key_id ",
                       static_cast<uint16_t>(key_id)));
    }
    QUICHE_CHECK(writer.WriteUInt16(item.GetHpkeKdfId()));
    QUICHE_CHECK(writer.WriteUInt16(item.GetHpkeAeadId()));
  }
  QUICHE_CHECK_EQ(writer.remaining(), 0u);
  return ohttp_key_configuration;
}

// Verifies if the `key_config` contains all valid combinations of [kem_id,
// kdf_id, aead_id] that comprises Single Key configuration encoding as
// specified in
// https://www.rfc-editor.org/rfc/rfc9458.html#section-3.1-2
absl::Status StoreKeyConfigIfValid(
    ObliviousHttpKeyConfigs::OhttpKeyConfig key_config,
    absl::btree_map<uint8_t, std::vector<ObliviousHttpHeaderKeyConfig>,
                    std::greater<uint8_t>>& configs,
    absl::flat_hash_map<uint8_t, std::string>& keys) {
  QUICHE_ASSIGN_OR_RETURN(uint16_t key_length, KeyLength(key_config.kem_id));
  if (key_length != key_config.public_key.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid key length ", key_config.public_key.size(), " for KEM ID ",
        absl::Hex(key_config.kem_id), ", expected ", key_length));
  }
  for (const auto& symmetric_config : key_config.symmetric_algorithms) {
    QUICHE_RETURN_IF_ERROR(CheckKdfId(symmetric_config.kdf_id).status());
    QUICHE_RETURN_IF_ERROR(CheckAeadId(symmetric_config.aead_id).status());
    auto ohttp_config = ObliviousHttpHeaderKeyConfig::Create(
        key_config.key_id, key_config.kem_id, symmetric_config.kdf_id,
        symmetric_config.aead_id);
    if (ohttp_config.ok()) {
      configs[key_config.key_id].emplace_back(std::move(*ohttp_config));
    }
  }
  keys.emplace(key_config.key_id, std::move(key_config.public_key));
  return absl::OkStatus();
}

}  // namespace

// static
std::string ObliviousHttpKeyConfigs::GetAcceptHeaderValueForConcatenatedKeys() {
  return "application/ohttp-keys";
}

absl::StatusOr<ObliviousHttpKeyConfigs>
ObliviousHttpKeyConfigs::ParseConcatenatedKeys(
    absl::string_view key_config,
    std::optional<absl::string_view> /*media_type*/) {
  ConfigMap configs;
  PublicKeyMap keys;
  std::vector<uint8_t> key_ids;
  // First, try to parse the keys using the length-prefixed format from RFC
  // 9458.
  if (ReadKeyConfigsWithLengthPrefix(key_config, configs, keys, key_ids).ok()) {
    return ObliviousHttpKeyConfigs(std::move(configs), std::move(keys),
                                   key_ids);
  }
  // Otherwise, try parsing using the non-length-prefixed format from
  // draft-ietf-ohai-ohttp-08, a precursor to RFC 9458.
  configs.clear();
  keys.clear();
  key_ids.clear();
  QuicheDataReader reader(key_config);
  while (!reader.IsDoneReading()) {
    uint8_t key_id;
    QUICHE_RETURN_IF_ERROR(ReadSingleKeyConfig(reader, configs, keys, key_id,
                                               /*skip_unknown_kems=*/false));
    if (!configs.empty() && (key_ids.empty() || key_ids.back() != key_id)) {
      key_ids.push_back(key_id);
    }
  }
  return ObliviousHttpKeyConfigs(std::move(configs), std::move(keys), key_ids);
}

absl::StatusOr<ObliviousHttpKeyConfigs> ObliviousHttpKeyConfigs::Create(
    std::vector<OhttpKeyConfig> ohttp_key_configs) {
  if (ohttp_key_configs.empty()) {
    return absl::InvalidArgumentError("Empty input");
  }
  ConfigMap configs_map;
  PublicKeyMap keys_map;
  std::vector<uint8_t> key_ids;
  key_ids.reserve(ohttp_key_configs.size());
  for (OhttpKeyConfig ohttp_key_config : ohttp_key_configs) {
    const uint8_t key_id = ohttp_key_config.key_id;
    QUICHE_RETURN_IF_ERROR(StoreKeyConfigIfValid(std::move(ohttp_key_config),
                                                 configs_map, keys_map));
    if (key_ids.empty() || key_ids.back() != key_id) {
      key_ids.push_back(key_id);
    }
  }
  return ObliviousHttpKeyConfigs(std::move(configs_map), std::move(keys_map),
                                 std::move(key_ids));
}

absl::StatusOr<ObliviousHttpKeyConfigs> ObliviousHttpKeyConfigs::Create(
    const ObliviousHttpHeaderKeyConfig& single_key_config,
    absl::string_view public_key) {
  QUICHE_ASSIGN_OR_RETURN(uint16_t key_length,
                          KeyLength(single_key_config.GetHpkeKemId()));
  if (key_length != public_key.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid key length ", public_key.size(), " for KEM ID ",
                     absl::Hex(single_key_config.GetHpkeKemId()), ", expected ",
                     key_length));
  }

  ConfigMap configs;
  PublicKeyMap keys;
  uint8_t key_id = single_key_config.GetKeyId();
  std::vector<uint8_t> key_ids = {key_id};
  keys.emplace(key_id, public_key);
  configs[key_id].emplace_back(std::move(single_key_config));
  return ObliviousHttpKeyConfigs(std::move(configs), std::move(keys),
                                 std::move(key_ids));
}

absl::StatusOr<std::string> ObliviousHttpKeyConfigs::GenerateConcatenatedKeys(
    bool with_length_prefix) const {
  std::string concatenated_keys;
  for (const uint8_t key_id : key_ids_) {
    const auto it = configs_.find(key_id);
    if (it == configs_.end()) {
      continue;
    }
    const std::vector<ObliviousHttpHeaderKeyConfig>& ohttp_configs = it->second;
    QUICHE_ASSIGN_OR_RETURN(absl::string_view public_key,
                            GetPublicKeyForId(key_id));
    QUICHE_ASSIGN_OR_RETURN(
        std::string serialized,
        SerializeOhttpKeyWithPublicKey(key_id, public_key, ohttp_configs));
    if (with_length_prefix) {
      char length_prefix[sizeof(uint16_t)];
      QuicheDataWriter writer(sizeof(uint16_t), length_prefix);
      QUICHE_CHECK(writer.WriteUInt16(serialized.size()));
      absl::StrAppend(&concatenated_keys,
                      absl::string_view(length_prefix, sizeof(uint16_t)));
    }
    absl::StrAppend(&concatenated_keys, std::move(serialized));
  }
  return concatenated_keys;
}

ObliviousHttpHeaderKeyConfig ObliviousHttpKeyConfigs::PreferredConfig() const {
  // configs_ is forced to have at least one object during construction.
  QUICHE_CHECK(!configs_.empty());
  if (!key_ids_.empty()) {
    auto it = configs_.find(key_ids_.front());
    if (it != configs_.end()) {
      return it->second.front();
    }
  }
  return configs_.begin()->second.front();
}

absl::StatusOr<absl::string_view> ObliviousHttpKeyConfigs::GetPublicKeyForId(
    uint8_t key_id) const {
  auto key = public_keys_.find(key_id);
  if (key == public_keys_.end()) {
    return absl::NotFoundError(
        absl::StrCat("No public key found for key_id", key_id));
  }
  return key->second;
}

absl::Status ObliviousHttpKeyConfigs::ReadSingleKeyConfig(
    QuicheDataReader& reader, ConfigMap& configs, PublicKeyMap& keys,
    uint8_t& key_id, bool skip_unknown_kems) {
  uint8_t key_id2;
  if (!reader.ReadUInt8(&key_id2)) {
    return absl::InvalidArgumentError("Failed to read key_id");
  }
  key_id = key_id2;
  uint16_t kem_id;
  if (!reader.ReadUInt16(&kem_id)) {
    return absl::InvalidArgumentError("Failed to read kem_id");
  }
  absl::StatusOr<uint16_t> key_length = KeyLength(kem_id);
  if (!key_length.ok()) {
    if (skip_unknown_kems) {
      return absl::OkStatus();
    }
    return key_length.status();
  }
  std::string key_str(*key_length, '\0');
  if (!reader.ReadBytes(key_str.data(), key_str.size())) {
    return absl::InvalidArgumentError("Failed to read public key");
  }
  if (!keys.insert({key_id, std::move(key_str)}).second) {
    return absl::InvalidArgumentError(
        absl::StrCat("Found duplicated key_id ", key_id));
  }

  absl::string_view alg_bytes;
  if (!reader.ReadStringPiece16(&alg_bytes)) {
    return absl::InvalidArgumentError("Failed to read symmetric algorithms");
  }
  QuicheDataReader sub_reader(alg_bytes);
  bool found_supported_symmetric_algs = false;
  while (!sub_reader.IsDoneReading()) {
    uint16_t kdf_id;
    if (!sub_reader.ReadUInt16(&kdf_id)) {
      return absl::InvalidArgumentError("Failed to read kdf_id");
    }
    uint16_t aead_id;
    if (!sub_reader.ReadUInt16(&aead_id)) {
      return absl::InvalidArgumentError("Failed to read aead_id");
    }

    if (!CheckKdfId(kdf_id).ok() || !CheckAeadId(aead_id).ok()) {
      // Skip unsupported symmetric algorithms pairs.
      continue;
    }

    QUICHE_ASSIGN_OR_RETURN(
        ObliviousHttpHeaderKeyConfig cfg,
        ObliviousHttpHeaderKeyConfig::Create(key_id, kem_id, kdf_id, aead_id));
    configs[key_id].emplace_back(std::move(cfg));
    found_supported_symmetric_algs = true;
  }
  if (!found_supported_symmetric_algs) {
    return absl::InvalidArgumentError(absl::StrCat(
        "No supported symmetric algorithms found for key_id ", key_id));
  }
  // Intentionally allow extra data at the end of the key config. This will
  // allow us to use it for extensions. See
  // draft-schinazi-httpbis-ohttp-ext-key-config.
  return absl::OkStatus();
}

// static
absl::Status ObliviousHttpKeyConfigs::ReadKeyConfigsWithLengthPrefix(
    absl::string_view key_configs, ConfigMap& configs, PublicKeyMap& keys,
    std::vector<uint8_t>& key_ids) {
  QuicheDataReader reader(key_configs);
  while (!reader.IsDoneReading()) {
    absl::string_view single_key_config;
    if (!reader.ReadStringPiece16(&single_key_config)) {
      return absl::InvalidArgumentError(
          "Failed to read length-prefixed key config");
    }
    QuicheDataReader single_reader(single_key_config);
    uint8_t key_id;
    QUICHE_RETURN_IF_ERROR(ReadSingleKeyConfig(single_reader, configs, keys,
                                               key_id,
                                               /*skip_unknown_kems=*/true));
    if (!configs.empty() && (key_ids.empty() || key_ids.back() != key_id)) {
      key_ids.push_back(key_id);
    }
  }
  if (configs.empty() || keys.empty()) {
    return absl::InvalidArgumentError("No supported key configs found");
  }
  return absl::OkStatus();
}

// https://www.iana.org/assignments/hpke

std::string ObliviousHttpKemIdToString(uint16_t kem_id) {
  switch (kem_id) {
    case EVP_HPKE_DHKEM_P256_HKDF_SHA256:
      return "P256-SHA256";
    case EVP_HPKE_DHKEM_X25519_HKDF_SHA256:
      return "X25519-SHA256";
    case EVP_HPKE_XWING:
      return "XWING";
    case EVP_HPKE_MLKEM768:
      return "MLKEM768";
    case EVP_HPKE_MLKEM1024:
      return "MLKEM1024";
    default:
      return absl::StrCat("UnknownKEM(", kem_id, ")");
  }
}

std::string ObliviousHttpKdfIdToString(uint16_t kdf_id) {
  switch (kdf_id) {
    case EVP_HPKE_HKDF_SHA256:
      return "SHA256";
    case EVP_HPKE_HKDF_SHA384:
      return "SHA384";
    default:
      return absl::StrCat("UnknownKDF(", kdf_id, ")");
  }
}

std::string ObliviousHttpAeadIdToString(uint16_t aead_id) {
  switch (aead_id) {
    case EVP_HPKE_AES_128_GCM:
      return "AES-128-GCM";
    case EVP_HPKE_AES_256_GCM:
      return "AES-256-GCM";
    case EVP_HPKE_CHACHA20_POLY1305:
      return "CHACHA20-POLY1305";
    default:
      return absl::StrCat("UnknownAEAD(", aead_id, ")");
  }
}

std::string ObliviousHttpHeaderKeyConfig::DebugString() const {
  return absl::StrCat("[key_id: ", static_cast<uint16_t>(key_id_),
                      ", kem_id: ", ObliviousHttpKemIdToString(kem_id_),
                      ", kdf_id: ", ObliviousHttpKdfIdToString(kdf_id_),
                      ", aead_id: ", ObliviousHttpAeadIdToString(aead_id_),
                      "]");
}

std::string ObliviousHttpKeyConfigs::SymmetricAlgorithmsConfig::DebugString()
    const {
  return absl::StrCat(ObliviousHttpKdfIdToString(kdf_id), "+",
                      ObliviousHttpAeadIdToString(aead_id));
}

std::string ObliviousHttpKeyConfigs::OhttpKeyConfig::DebugString() const {
  std::string s;
  bool first = true;
  for (const SymmetricAlgorithmsConfig& sym : symmetric_algorithms) {
    absl::StrAppend(&s, (first ? "" : ", "), sym.DebugString());
    first = false;
  }
  return absl::StrCat("[key_id: ", static_cast<uint16_t>(key_id),
                      ", kem_id: ", ObliviousHttpKemIdToString(kem_id), ", {",
                      s, "}, public_key: ", absl::BytesToHexString(public_key),
                      "]");
}

std::string ObliviousHttpKeyConfigs::DebugString() const {
  std::string s;
  for (const uint8_t key_id : key_ids_) {
    const auto kit = configs_.find(key_id);
    if (kit == configs_.end()) {
      continue;
    }
    const std::vector<ObliviousHttpHeaderKeyConfig>& ohttp_configs =
        kit->second;
    if (!s.empty()) {
      absl::StrAppend(&s, "\n");
    }
    absl::StrAppend(&s, "[key_id: ", static_cast<uint16_t>(key_id), ", {");
    for (const ObliviousHttpHeaderKeyConfig& ohttp_config : ohttp_configs) {
      absl::StrAppend(&s, "\n  ", ohttp_config.DebugString());
    }
    std::string public_key;
    auto it = public_keys_.find(key_id);
    if (it != public_keys_.end()) {
      public_key = absl::BytesToHexString(it->second);
    }
    absl::StrAppend(&s, "\n}, public_key: ", public_key, "]");
  }
  return s;
}

// static
absl::StatusOr<ObliviousHttpHeaderKeyConfig>
ObliviousHttpKeyConfigs::ParseOhttpPayloadHeaderAgainstConfigs(
    absl::string_view payload_bytes,
    const std::vector<ObliviousHttpHeaderKeyConfig>& configs) {
  for (const ObliviousHttpHeaderKeyConfig& config : configs) {
    if (config.ParseOhttpPayloadHeader(payload_bytes).ok()) {
      return config;
    }
  }
  return absl::InvalidArgumentError("Payload did not match any key configs");
}

absl::StatusOr<ObliviousHttpHeaderKeyConfig>
ObliviousHttpKeyConfigs::GetConfigForPayload(
    absl::string_view payload_bytes) const {
  QUICHE_ASSIGN_OR_RETURN(
      uint8_t key_id,
      ObliviousHttpHeaderKeyConfig::ParseKeyIdFromObliviousHttpRequestPayload(
          payload_bytes));
  auto it = configs_.find(key_id);
  if (it == configs_.end()) {
    return absl::NotFoundError(
        absl::StrCat("No config found for key_id ", key_id));
  }
  return ParseOhttpPayloadHeaderAgainstConfigs(payload_bytes, it->second);
}

}  // namespace quiche
