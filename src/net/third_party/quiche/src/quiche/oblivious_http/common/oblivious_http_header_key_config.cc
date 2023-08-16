#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_endian.h"

namespace quiche {
namespace {

// Size of KEM ID is 2 bytes. Refer to OHTTP Key Config in the spec,
// https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-06.html#name-a-single-key-configuration
constexpr size_t kSizeOfHpkeKemId = 2;

// Size of Symmetric algorithms is 2 bytes(16 bits) each.
// Refer to HPKE Symmetric Algorithms configuration in the spec,
// https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-06.html#name-a-single-key-configuration
constexpr size_t kSizeOfSymmetricAlgorithmHpkeKdfId = 2;
constexpr size_t kSizeOfSymmetricAlgorithmHpkeAeadId = 2;

absl::StatusOr<const EVP_HPKE_KEM*> CheckKemId(uint16_t kem_id) {
  switch (kem_id) {
    case EVP_HPKE_DHKEM_X25519_HKDF_SHA256:
      return EVP_hpke_x25519_hkdf_sha256();
    default:
      return absl::UnimplementedError("No support for this KEM ID.");
  }
}

absl::StatusOr<const EVP_HPKE_KDF*> CheckKdfId(uint16_t kdf_id) {
  switch (kdf_id) {
    case EVP_HPKE_HKDF_SHA256:
      return EVP_hpke_hkdf_sha256();
    default:
      return absl::UnimplementedError("No support for this KDF ID.");
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
      return absl::UnimplementedError("No support for this AEAD ID.");
  }
}

}  // namespace

ObliviousHttpHeaderKeyConfig::ObliviousHttpHeaderKeyConfig(uint8_t key_id,
                                                           uint16_t kem_id,
                                                           uint16_t kdf_id,
                                                           uint16_t aead_id)
    : key_id_(key_id), kem_id_(kem_id), kdf_id_(kdf_id), aead_id_(aead_id) {}

absl::StatusOr<ObliviousHttpHeaderKeyConfig>
ObliviousHttpHeaderKeyConfig::Create(uint8_t key_id, uint16_t kem_id,
                                     uint16_t kdf_id, uint16_t aead_id) {
  ObliviousHttpHeaderKeyConfig instance(key_id, kem_id, kdf_id, aead_id);
  auto is_config_ok = instance.ValidateKeyConfig();
  if (!is_config_ok.ok()) {
    return is_config_ok;
  }
  return instance;
}

absl::Status ObliviousHttpHeaderKeyConfig::ValidateKeyConfig() const {
  auto supported_kem = CheckKemId(kem_id_);
  if (!supported_kem.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported KEM ID:", kem_id_));
  }
  auto supported_kdf = CheckKdfId(kdf_id_);
  if (!supported_kdf.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported KDF ID:", kdf_id_));
  }
  auto supported_aead = CheckAeadId(aead_id_);
  if (!supported_aead.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported AEAD ID:", aead_id_));
  }
  return absl::OkStatus();
}

const EVP_HPKE_KEM* ObliviousHttpHeaderKeyConfig::GetHpkeKem() const {
  auto kem = CheckKemId(kem_id_);
  QUICHE_CHECK_OK(kem.status());
  return kem.value();
}
const EVP_HPKE_KDF* ObliviousHttpHeaderKeyConfig::GetHpkeKdf() const {
  auto kdf = CheckKdfId(kdf_id_);
  QUICHE_CHECK_OK(kdf.status());
  return kdf.value();
}
const EVP_HPKE_AEAD* ObliviousHttpHeaderKeyConfig::GetHpkeAead() const {
  auto aead = CheckAeadId(aead_id_);
  QUICHE_CHECK_OK(aead.status());
  return aead.value();
}

std::string ObliviousHttpHeaderKeyConfig::SerializeRecipientContextInfo()
    const {
  uint8_t zero_byte = 0x00;
  int buf_len = kOhttpRequestLabel.size() + kHeaderLength + sizeof(zero_byte);
  std::string info(buf_len, '\0');
  QuicheDataWriter writer(info.size(), info.data());
  QUICHE_CHECK(writer.WriteStringPiece(kOhttpRequestLabel));
  QUICHE_CHECK(writer.WriteUInt8(zero_byte));  // Zero byte.
  QUICHE_CHECK(writer.WriteUInt8(key_id_));
  QUICHE_CHECK(writer.WriteUInt16(kem_id_));
  QUICHE_CHECK(writer.WriteUInt16(kdf_id_));
  QUICHE_CHECK(writer.WriteUInt16(aead_id_));
  return info;
}

/**
 * Follows IETF Ohttp spec, section 4.1 (Encapsulation of Requests).
 * https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.1-10
 */
absl::Status ObliviousHttpHeaderKeyConfig::ParseOhttpPayloadHeader(
    absl::string_view payload_bytes) const {
  if (payload_bytes.empty()) {
    return absl::InvalidArgumentError("Empty request payload.");
  }
  QuicheDataReader reader(payload_bytes);
  return ParseOhttpPayloadHeader(reader);
}

absl::Status ObliviousHttpHeaderKeyConfig::ParseOhttpPayloadHeader(
    QuicheDataReader& reader) const {
  uint8_t key_id;
  if (!reader.ReadUInt8(&key_id)) {
    return absl::InvalidArgumentError("Failed to read key_id from header.");
  }
  if (key_id != key_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("KeyID in request:", static_cast<uint16_t>(key_id),
                     " doesn't match with server's public key "
                     "configuration KeyID:",
                     static_cast<uint16_t>(key_id_)));
  }
  uint16_t kem_id;
  if (!reader.ReadUInt16(&kem_id)) {
    return absl::InvalidArgumentError("Failed to read kem_id from header.");
  }
  if (kem_id != kem_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Received Invalid kemID:", kem_id, " Expected:", kem_id_));
  }
  uint16_t kdf_id;
  if (!reader.ReadUInt16(&kdf_id)) {
    return absl::InvalidArgumentError("Failed to read kdf_id from header.");
  }
  if (kdf_id != kdf_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Received Invalid kdfID:", kdf_id, " Expected:", kdf_id_));
  }
  uint16_t aead_id;
  if (!reader.ReadUInt16(&aead_id)) {
    return absl::InvalidArgumentError("Failed to read aead_id from header.");
  }
  if (aead_id != aead_id_) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Received Invalid aeadID:", aead_id, " Expected:", aead_id_));
  }
  return absl::OkStatus();
}

absl::StatusOr<uint8_t>
ObliviousHttpHeaderKeyConfig::ParseKeyIdFromObliviousHttpRequestPayload(
    absl::string_view payload_bytes) {
  if (payload_bytes.empty()) {
    return absl::InvalidArgumentError("Empty request payload.");
  }
  QuicheDataReader reader(payload_bytes);
  uint8_t key_id;
  if (!reader.ReadUInt8(&key_id)) {
    return absl::InvalidArgumentError("Failed to read key_id from payload.");
  }
  return key_id;
}

std::string ObliviousHttpHeaderKeyConfig::SerializeOhttpPayloadHeader() const {
  int buf_len =
      sizeof(key_id_) + sizeof(kem_id_) + sizeof(kdf_id_) + sizeof(aead_id_);
  std::string hdr(buf_len, '\0');
  QuicheDataWriter writer(hdr.size(), hdr.data());
  QUICHE_CHECK(writer.WriteUInt8(key_id_));
  QUICHE_CHECK(writer.WriteUInt16(kem_id_));   // kemID
  QUICHE_CHECK(writer.WriteUInt16(kdf_id_));   // kdfID
  QUICHE_CHECK(writer.WriteUInt16(aead_id_));  // aeadID
  return hdr;
}

namespace {
// https://www.rfc-editor.org/rfc/rfc9180#section-7.1
absl::StatusOr<uint16_t> KeyLength(uint16_t kem_id) {
  auto supported_kem = CheckKemId(kem_id);
  if (!supported_kem.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unsupported KEM ID:", kem_id, ". public key length is unknown."));
  }
  return EVP_HPKE_KEM_public_key_len(supported_kem.value());
}

absl::StatusOr<std::string> SerializeOhttpKeyWithPublicKey(
    uint8_t key_id, absl::string_view public_key,
    const std::vector<ObliviousHttpHeaderKeyConfig>& ohttp_configs) {
  auto ohttp_config = ohttp_configs[0];
  // Check if `ohttp_config` match spec's encoding guidelines.
  static_assert(sizeof(ohttp_config.GetHpkeKemId()) == kSizeOfHpkeKemId &&
                    sizeof(ohttp_config.GetHpkeKdfId()) ==
                        kSizeOfSymmetricAlgorithmHpkeKdfId &&
                    sizeof(ohttp_config.GetHpkeAeadId()) ==
                        kSizeOfSymmetricAlgorithmHpkeAeadId,
                "Size of HPKE IDs should match RFC specification.");

  uint16_t symmetric_algs_length =
      ohttp_configs.size() * (kSizeOfSymmetricAlgorithmHpkeKdfId +
                              kSizeOfSymmetricAlgorithmHpkeAeadId);
  int buf_len = sizeof(key_id) + kSizeOfHpkeKemId + public_key.size() +
                sizeof(symmetric_algs_length) + symmetric_algs_length;
  std::string ohttp_key_configuration(buf_len, '\0');
  QuicheDataWriter writer(ohttp_key_configuration.size(),
                          ohttp_key_configuration.data());
  if (!writer.WriteUInt8(key_id)) {
    return absl::InternalError("Failed to serialize OHTTP key.[key_id]");
  }
  if (!writer.WriteUInt16(ohttp_config.GetHpkeKemId())) {
    return absl::InternalError(
        "Failed to serialize OHTTP key.[kem_id]");  // kemID.
  }
  if (!writer.WriteStringPiece(public_key)) {
    return absl::InternalError(
        "Failed to serialize OHTTP key.[public_key]");  // Raw public key.
  }
  if (!writer.WriteUInt16(symmetric_algs_length)) {
    return absl::InternalError(
        "Failed to serialize OHTTP key.[symmetric_algs_length]");
  }
  for (const auto& item : ohttp_configs) {
    // Check if KEM ID is the same for all the configs stored in `this` for
    // given `key_id`.
    if (item.GetHpkeKemId() != ohttp_config.GetHpkeKemId()) {
      QUICHE_BUG(ohttp_key_configs_builder_parser)
          << "ObliviousHttpKeyConfigs object cannot hold ConfigMap of "
             "different KEM IDs:[ "
          << item.GetHpkeKemId() << "," << ohttp_config.GetHpkeKemId()
          << " ]for a given key_id:" << static_cast<uint16_t>(key_id);
    }
    if (!writer.WriteUInt16(item.GetHpkeKdfId())) {
      return absl::InternalError(
          "Failed to serialize OHTTP key.[kdf_id]");  // kdfID.
    }
    if (!writer.WriteUInt16(item.GetHpkeAeadId())) {
      return absl::InternalError(
          "Failed to serialize OHTTP key.[aead_id]");  // aeadID.
    }
  }
  QUICHE_DCHECK_EQ(writer.remaining(), 0u);
  return ohttp_key_configuration;
}

std::string GetDebugStringForFailedKeyConfig(
    const ObliviousHttpKeyConfigs::OhttpKeyConfig& failed_key_config) {
  std::string debug_string = "[ ";
  absl::StrAppend(&debug_string,
                  "key_id:", static_cast<uint16_t>(failed_key_config.key_id),
                  " , kem_id:", failed_key_config.kem_id,
                  ". Printing HEX formatted public_key:",
                  absl::BytesToHexString(failed_key_config.public_key));
  absl::StrAppend(&debug_string, ", symmetric_algorithms: { ");
  for (const auto& symmetric_config : failed_key_config.symmetric_algorithms) {
    absl::StrAppend(&debug_string, "{kdf_id: ", symmetric_config.kdf_id,
                    ", aead_id:", symmetric_config.aead_id, " }");
  }
  absl::StrAppend(&debug_string, " } ]");
  return debug_string;
}

// Verifies if the `key_config` contains all valid combinations of [kem_id,
// kdf_id, aead_id] that comprises Single Key configuration encoding as
// specified in
// https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#name-a-single-key-configuration.
absl::Status StoreKeyConfigIfValid(
    ObliviousHttpKeyConfigs::OhttpKeyConfig key_config,
    absl::btree_map<uint8_t, std::vector<ObliviousHttpHeaderKeyConfig>,
                    std::greater<uint8_t>>& configs,
    absl::flat_hash_map<uint8_t, std::string>& keys) {
  if (!CheckKemId(key_config.kem_id).ok() ||
      key_config.public_key.size() != KeyLength(key_config.kem_id).value()) {
    QUICHE_LOG(ERROR) << "Failed to process: "
                      << GetDebugStringForFailedKeyConfig(key_config);
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid key_config! [KEM ID:", key_config.kem_id, "]"));
  }
  for (const auto& symmetric_config : key_config.symmetric_algorithms) {
    if (!CheckKdfId(symmetric_config.kdf_id).ok() ||
        !CheckAeadId(symmetric_config.aead_id).ok()) {
      QUICHE_LOG(ERROR) << "Failed to process: "
                        << GetDebugStringForFailedKeyConfig(key_config);
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid key_config! [KDF ID:", symmetric_config.kdf_id,
                       ", AEAD ID:", symmetric_config.aead_id, "]"));
    }
    auto ohttp_config = ObliviousHttpHeaderKeyConfig::Create(
        key_config.key_id, key_config.kem_id, symmetric_config.kdf_id,
        symmetric_config.aead_id);
    if (ohttp_config.ok()) {
      configs[key_config.key_id].emplace_back(std::move(ohttp_config.value()));
    }
  }
  keys.emplace(key_config.key_id, std::move(key_config.public_key));
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<ObliviousHttpKeyConfigs>
ObliviousHttpKeyConfigs::ParseConcatenatedKeys(absl::string_view key_config) {
  ConfigMap configs;
  PublicKeyMap keys;
  auto reader = QuicheDataReader(key_config);
  while (!reader.IsDoneReading()) {
    absl::Status status = ReadSingleKeyConfig(reader, configs, keys);
    if (!status.ok()) return status;
  }
  return ObliviousHttpKeyConfigs(std::move(configs), std::move(keys));
}

absl::StatusOr<ObliviousHttpKeyConfigs> ObliviousHttpKeyConfigs::Create(
    absl::flat_hash_set<ObliviousHttpKeyConfigs::OhttpKeyConfig>
        ohttp_key_configs) {
  if (ohttp_key_configs.empty()) {
    return absl::InvalidArgumentError("Empty input.");
  }
  ConfigMap configs_map;
  PublicKeyMap keys_map;
  for (auto& ohttp_key_config : ohttp_key_configs) {
    auto result = StoreKeyConfigIfValid(std::move(ohttp_key_config),
                                        configs_map, keys_map);
    if (!result.ok()) {
      return result;
    }
  }
  auto oblivious_configs =
      ObliviousHttpKeyConfigs(std::move(configs_map), std::move(keys_map));
  return oblivious_configs;
}

absl::StatusOr<ObliviousHttpKeyConfigs> ObliviousHttpKeyConfigs::Create(
    const ObliviousHttpHeaderKeyConfig& single_key_config,
    absl::string_view public_key) {
  if (public_key.empty()) {
    return absl::InvalidArgumentError("Empty input.");
  }

  if (auto key_length = KeyLength(single_key_config.GetHpkeKemId());
      public_key.size() != key_length.value()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid key. Key size mismatch. Expected:", key_length.value(),
        " Actual:", public_key.size()));
  }

  ConfigMap configs;
  PublicKeyMap keys;
  uint8_t key_id = single_key_config.GetKeyId();
  keys.emplace(key_id, public_key);
  configs[key_id].emplace_back(std::move(single_key_config));
  return ObliviousHttpKeyConfigs(std::move(configs), std::move(keys));
}

absl::StatusOr<std::string> ObliviousHttpKeyConfigs::GenerateConcatenatedKeys()
    const {
  std::string concatenated_keys;
  for (const auto& [key_id, ohttp_configs] : configs_) {
    auto key = public_keys_.find(key_id);
    if (key == public_keys_.end()) {
      return absl::InternalError(
          "Failed to serialize. No public key found for key_id");
    }
    auto serialized =
        SerializeOhttpKeyWithPublicKey(key_id, key->second, ohttp_configs);
    if (!serialized.ok()) {
      return absl::InternalError("Failed to serialize OHTTP key configs.");
    }
    absl::StrAppend(&concatenated_keys, serialized.value());
  }
  return concatenated_keys;
}

ObliviousHttpHeaderKeyConfig ObliviousHttpKeyConfigs::PreferredConfig() const {
  // configs_ is forced to have at least one object during construction.
  return configs_.begin()->second.front();
}

absl::StatusOr<absl::string_view> ObliviousHttpKeyConfigs::GetPublicKeyForId(
    uint8_t key_id) const {
  auto key = public_keys_.find(key_id);
  if (key == public_keys_.end()) {
    return absl::NotFoundError("No public key found for key_id");
  }
  return key->second;
}

absl::Status ObliviousHttpKeyConfigs::ReadSingleKeyConfig(
    QuicheDataReader& reader, ConfigMap& configs, PublicKeyMap& keys) {
  uint8_t key_id;
  uint16_t kem_id;
  // First byte: key_id; next two bytes: kem_id.
  if (!reader.ReadUInt8(&key_id) || !reader.ReadUInt16(&kem_id)) {
    return absl::InvalidArgumentError("Invalid key_config!");
  }

  // Public key length depends on the kem_id.
  auto maybe_key_length = KeyLength(kem_id);
  if (!maybe_key_length.ok()) {
    return maybe_key_length.status();
  }
  const int key_length = maybe_key_length.value();
  std::string key_str(key_length, '\0');
  if (!reader.ReadBytes(key_str.data(), key_length)) {
    return absl::InvalidArgumentError("Invalid key_config!");
  }
  if (!keys.insert({key_id, std::move(key_str)}).second) {
    return absl::InvalidArgumentError("Duplicate key_id's in key_config!");
  }

  // Extract the algorithms for this public key.
  absl::string_view alg_bytes;
  // Read the 16-bit length, then read that many bytes into alg_bytes.
  if (!reader.ReadStringPiece16(&alg_bytes)) {
    return absl::InvalidArgumentError("Invalid key_config!");
  }
  QuicheDataReader sub_reader(alg_bytes);
  while (!sub_reader.IsDoneReading()) {
    uint16_t kdf_id;
    uint16_t aead_id;
    if (!sub_reader.ReadUInt16(&kdf_id) || !sub_reader.ReadUInt16(&aead_id)) {
      return absl::InvalidArgumentError("Invalid key_config!");
    }

    absl::StatusOr<ObliviousHttpHeaderKeyConfig> maybe_cfg =
        ObliviousHttpHeaderKeyConfig::Create(key_id, kem_id, kdf_id, aead_id);
    if (!maybe_cfg.ok()) {
      // TODO(kmg): Add support to ignore key types in the server response that
      // aren't supported by the client.
      return maybe_cfg.status();
    }
    configs[key_id].emplace_back(std::move(maybe_cfg.value()));
  }
  return absl::OkStatus();
}

}  // namespace quiche
