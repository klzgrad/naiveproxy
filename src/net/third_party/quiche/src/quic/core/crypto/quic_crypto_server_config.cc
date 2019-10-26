// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

#include "third_party/boringssl/src/include/openssl/sha.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/cert_compressor.h"
#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/channel_id.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/crypto/curve25519_key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/p256_key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_hkdf.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/crypto/tls_server_connection.h"
#include "net/third_party/quiche/src/quic/core/proto/crypto_server_config_proto.h"
#include "net/third_party/quiche/src/quic/core/proto/source_address_token_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_socket_address_coder.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_cert_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_clock.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fallthrough.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_hostname_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

namespace {

// kMultiplier is the multiple of the CHLO message size that a REJ message
// must stay under when the client doesn't present a valid source-address
// token. This is used to protect QUIC from amplification attacks.
// TODO(rch): Reduce this to 2 again once b/25933682 is fixed.
const size_t kMultiplier = 3;

const int kMaxTokenAddresses = 4;

std::string DeriveSourceAddressTokenKey(
    QuicStringPiece source_address_token_secret) {
  QuicHKDF hkdf(source_address_token_secret, QuicStringPiece() /* no salt */,
                "QUIC source address token key",
                CryptoSecretBoxer::GetKeySize(), 0 /* no fixed IV needed */,
                0 /* no subkey secret */);
  return std::string(hkdf.server_write_key());
}

// Default source for creating KeyExchange objects.
class DefaultKeyExchangeSource : public KeyExchangeSource {
 public:
  DefaultKeyExchangeSource() = default;
  ~DefaultKeyExchangeSource() override = default;

  std::unique_ptr<AsynchronousKeyExchange> Create(
      std::string /*server_config_id*/,
      bool /* is_fallback */,
      QuicTag type,
      QuicStringPiece private_key) override {
    if (private_key.empty()) {
      QUIC_LOG(WARNING) << "Server config contains key exchange method without "
                           "corresponding private key of type "
                        << QuicTagToString(type);
      return nullptr;
    }

    std::unique_ptr<SynchronousKeyExchange> ka =
        CreateLocalSynchronousKeyExchange(type, private_key);
    if (!ka) {
      QUIC_LOG(WARNING) << "Failed to create key exchange method of type "
                        << QuicTagToString(type);
    }
    return ka;
  }
};

// Returns true if the PDMD field from the client hello demands an X509
// certificate.
bool ClientDemandsX509Proof(const CryptoHandshakeMessage& client_hello) {
  QuicTagVector their_proof_demands;

  if (client_hello.GetTaglist(kPDMD, &their_proof_demands) != QUIC_NO_ERROR) {
    return false;
  }

  for (const QuicTag tag : their_proof_demands) {
    if (tag == kX509) {
      return true;
    }
  }
  return false;
}

}  // namespace

// static
std::unique_ptr<KeyExchangeSource> KeyExchangeSource::Default() {
  return QuicMakeUnique<DefaultKeyExchangeSource>();
}

class ValidateClientHelloHelper {
 public:
  // Note: stores a pointer to a unique_ptr, and std::moves the unique_ptr when
  // ValidationComplete is called.
  ValidateClientHelloHelper(
      QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
          result,
      std::unique_ptr<ValidateClientHelloResultCallback>* done_cb)
      : result_(std::move(result)), done_cb_(done_cb) {}
  ValidateClientHelloHelper(const ValidateClientHelloHelper&) = delete;
  ValidateClientHelloHelper& operator=(const ValidateClientHelloHelper&) =
      delete;

  ~ValidateClientHelloHelper() {
    QUIC_BUG_IF(done_cb_ != nullptr)
        << "Deleting ValidateClientHelloHelper with a pending callback.";
  }

  void ValidationComplete(
      QuicErrorCode error_code,
      const char* error_details,
      std::unique_ptr<ProofSource::Details> proof_source_details) {
    result_->error_code = error_code;
    result_->error_details = error_details;
    (*done_cb_)->Run(std::move(result_), std::move(proof_source_details));
    DetachCallback();
  }

  void DetachCallback() {
    QUIC_BUG_IF(done_cb_ == nullptr) << "Callback already detached.";
    done_cb_ = nullptr;
  }

 private:
  QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
      result_;
  std::unique_ptr<ValidateClientHelloResultCallback>* done_cb_;
};

// static
const char QuicCryptoServerConfig::TESTING[] = "secret string for testing";

ClientHelloInfo::ClientHelloInfo(const QuicIpAddress& in_client_ip,
                                 QuicWallTime in_now)
    : client_ip(in_client_ip), now(in_now), valid_source_address_token(false) {}

ClientHelloInfo::ClientHelloInfo(const ClientHelloInfo& other) = default;

ClientHelloInfo::~ClientHelloInfo() {}

PrimaryConfigChangedCallback::PrimaryConfigChangedCallback() {}

PrimaryConfigChangedCallback::~PrimaryConfigChangedCallback() {}

ValidateClientHelloResultCallback::Result::Result(
    const CryptoHandshakeMessage& in_client_hello,
    QuicIpAddress in_client_ip,
    QuicWallTime in_now)
    : client_hello(in_client_hello),
      info(in_client_ip, in_now),
      error_code(QUIC_NO_ERROR) {}

ValidateClientHelloResultCallback::Result::~Result() {}

ValidateClientHelloResultCallback::ValidateClientHelloResultCallback() {}

ValidateClientHelloResultCallback::~ValidateClientHelloResultCallback() {}

ProcessClientHelloResultCallback::ProcessClientHelloResultCallback() {}

ProcessClientHelloResultCallback::~ProcessClientHelloResultCallback() {}

QuicCryptoServerConfig::ConfigOptions::ConfigOptions()
    : expiry_time(QuicWallTime::Zero()),
      channel_id_enabled(false),
      p256(false) {}

QuicCryptoServerConfig::ConfigOptions::ConfigOptions(
    const ConfigOptions& other) = default;

QuicCryptoServerConfig::ConfigOptions::~ConfigOptions() {}

QuicCryptoServerConfig::ProcessClientHelloContext::
    ~ProcessClientHelloContext() {
  if (done_cb_ != nullptr) {
    QUIC_LOG(WARNING)
        << "Deleting ProcessClientHelloContext with a pending callback.";
  }
}

void QuicCryptoServerConfig::ProcessClientHelloContext::Fail(
    QuicErrorCode error,
    const std::string& error_details) {
  done_cb_->Run(error, error_details, nullptr, nullptr, nullptr);
  done_cb_ = nullptr;
}

void QuicCryptoServerConfig::ProcessClientHelloContext::Succeed(
    std::unique_ptr<CryptoHandshakeMessage> message,
    std::unique_ptr<DiversificationNonce> diversification_nonce,
    std::unique_ptr<ProofSource::Details> proof_source_details) {
  done_cb_->Run(QUIC_NO_ERROR, std::string(), std::move(message),
                std::move(diversification_nonce),
                std::move(proof_source_details));
  done_cb_ = nullptr;
}

QuicCryptoServerConfig::QuicCryptoServerConfig(
    QuicStringPiece source_address_token_secret,
    QuicRandom* server_nonce_entropy,
    std::unique_ptr<ProofSource> proof_source,
    std::unique_ptr<KeyExchangeSource> key_exchange_source)
    : replay_protection_(true),
      chlo_multiplier_(kMultiplier),
      configs_lock_(),
      primary_config_(nullptr),
      next_config_promotion_time_(QuicWallTime::Zero()),
      proof_source_(std::move(proof_source)),
      key_exchange_source_(std::move(key_exchange_source)),
      ssl_ctx_(TlsServerConnection::CreateSslCtx()),
      source_address_token_future_secs_(3600),
      source_address_token_lifetime_secs_(86400),
      enable_serving_sct_(false),
      rejection_observer_(nullptr),
      pad_rej_(true),
      pad_shlo_(true),
      validate_chlo_size_(true),
      validate_source_address_token_(true) {
  DCHECK(proof_source_.get());
  source_address_token_boxer_.SetKeys(
      {DeriveSourceAddressTokenKey(source_address_token_secret)});

  // Generate a random key and orbit for server nonces.
  server_nonce_entropy->RandBytes(server_nonce_orbit_,
                                  sizeof(server_nonce_orbit_));
  const size_t key_size = server_nonce_boxer_.GetKeySize();
  std::unique_ptr<uint8_t[]> key_bytes(new uint8_t[key_size]);
  server_nonce_entropy->RandBytes(key_bytes.get(), key_size);

  server_nonce_boxer_.SetKeys(
      {std::string(reinterpret_cast<char*>(key_bytes.get()), key_size)});
}

QuicCryptoServerConfig::~QuicCryptoServerConfig() {}

// static
QuicServerConfigProtobuf QuicCryptoServerConfig::GenerateConfig(
    QuicRandom* rand,
    const QuicClock* clock,
    const ConfigOptions& options) {
  CryptoHandshakeMessage msg;

  const std::string curve25519_private_key =
      Curve25519KeyExchange::NewPrivateKey(rand);
  std::unique_ptr<Curve25519KeyExchange> curve25519 =
      Curve25519KeyExchange::New(curve25519_private_key);
  QuicStringPiece curve25519_public_value = curve25519->public_value();

  std::string encoded_public_values;
  // First three bytes encode the length of the public value.
  DCHECK_LT(curve25519_public_value.size(), (1U << 24));
  encoded_public_values.push_back(
      static_cast<char>(curve25519_public_value.size()));
  encoded_public_values.push_back(
      static_cast<char>(curve25519_public_value.size() >> 8));
  encoded_public_values.push_back(
      static_cast<char>(curve25519_public_value.size() >> 16));
  encoded_public_values.append(curve25519_public_value.data(),
                               curve25519_public_value.size());

  std::string p256_private_key;
  if (options.p256) {
    p256_private_key = P256KeyExchange::NewPrivateKey();
    std::unique_ptr<P256KeyExchange> p256(
        P256KeyExchange::New(p256_private_key));
    QuicStringPiece p256_public_value = p256->public_value();

    DCHECK_LT(p256_public_value.size(), (1U << 24));
    encoded_public_values.push_back(
        static_cast<char>(p256_public_value.size()));
    encoded_public_values.push_back(
        static_cast<char>(p256_public_value.size() >> 8));
    encoded_public_values.push_back(
        static_cast<char>(p256_public_value.size() >> 16));
    encoded_public_values.append(p256_public_value.data(),
                                 p256_public_value.size());
  }

  msg.set_tag(kSCFG);
  if (options.p256) {
    msg.SetVector(kKEXS, QuicTagVector{kC255, kP256});
  } else {
    msg.SetVector(kKEXS, QuicTagVector{kC255});
  }
  msg.SetVector(kAEAD, QuicTagVector{kAESG, kCC20});
  msg.SetStringPiece(kPUBS, encoded_public_values);

  if (options.expiry_time.IsZero()) {
    const QuicWallTime now = clock->WallNow();
    const QuicWallTime expiry = now.Add(QuicTime::Delta::FromSeconds(
        60 * 60 * 24 * 180 /* 180 days, ~six months */));
    const uint64_t expiry_seconds = expiry.ToUNIXSeconds();
    msg.SetValue(kEXPY, expiry_seconds);
  } else {
    msg.SetValue(kEXPY, options.expiry_time.ToUNIXSeconds());
  }

  char orbit_bytes[kOrbitSize];
  if (options.orbit.size() == sizeof(orbit_bytes)) {
    memcpy(orbit_bytes, options.orbit.data(), sizeof(orbit_bytes));
  } else {
    DCHECK(options.orbit.empty());
    rand->RandBytes(orbit_bytes, sizeof(orbit_bytes));
  }
  msg.SetStringPiece(kORBT, QuicStringPiece(orbit_bytes, sizeof(orbit_bytes)));

  if (options.channel_id_enabled) {
    msg.SetVector(kPDMD, QuicTagVector{kCHID});
  }

  if (options.id.empty()) {
    // We need to ensure that the SCID changes whenever the server config does
    // thus we make it a hash of the rest of the server config.
    std::unique_ptr<QuicData> serialized =
        CryptoFramer::ConstructHandshakeMessage(msg);

    uint8_t scid_bytes[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const uint8_t*>(serialized->data()),
           serialized->length(), scid_bytes);
    // The SCID is a truncated SHA-256 digest.
    static_assert(16 <= SHA256_DIGEST_LENGTH, "SCID length too high.");
    msg.SetStringPiece(
        kSCID, QuicStringPiece(reinterpret_cast<const char*>(scid_bytes), 16));
  } else {
    msg.SetStringPiece(kSCID, options.id);
  }
  // Don't put new tags below this point. The SCID generation should hash over
  // everything but itself and so extra tags should be added prior to the
  // preceding if block.

  std::unique_ptr<QuicData> serialized =
      CryptoFramer::ConstructHandshakeMessage(msg);

  QuicServerConfigProtobuf config;
  config.set_config(std::string(serialized->AsStringPiece()));
  QuicServerConfigProtobuf::PrivateKey* curve25519_key = config.add_key();
  curve25519_key->set_tag(kC255);
  curve25519_key->set_private_key(curve25519_private_key);

  if (options.p256) {
    QuicServerConfigProtobuf::PrivateKey* p256_key = config.add_key();
    p256_key->set_tag(kP256);
    p256_key->set_private_key(p256_private_key);
  }

  return config;
}

std::unique_ptr<CryptoHandshakeMessage> QuicCryptoServerConfig::AddConfig(
    const QuicServerConfigProtobuf& protobuf,
    const QuicWallTime now) {
  std::unique_ptr<CryptoHandshakeMessage> msg =
      CryptoFramer::ParseMessage(protobuf.config());

  if (!msg.get()) {
    QUIC_LOG(WARNING) << "Failed to parse server config message";
    return nullptr;
  }

  QuicReferenceCountedPointer<Config> config =
      ParseConfigProtobuf(protobuf, /* is_fallback = */ false);
  if (!config.get()) {
    QUIC_LOG(WARNING) << "Failed to parse server config message";
    return nullptr;
  }

  {
    QuicWriterMutexLock locked(&configs_lock_);
    if (configs_.find(config->id) != configs_.end()) {
      QUIC_LOG(WARNING) << "Failed to add config because another with the same "
                           "server config id already exists: "
                        << QuicTextUtils::HexEncode(config->id);
      return nullptr;
    }

    configs_[config->id] = config;
    SelectNewPrimaryConfig(now);
    DCHECK(primary_config_.get());
    DCHECK_EQ(configs_.find(primary_config_->id)->second.get(),
              primary_config_.get());
  }

  return msg;
}

std::unique_ptr<CryptoHandshakeMessage>
QuicCryptoServerConfig::AddDefaultConfig(QuicRandom* rand,
                                         const QuicClock* clock,
                                         const ConfigOptions& options) {
  return AddConfig(GenerateConfig(rand, clock, options), clock->WallNow());
}

bool QuicCryptoServerConfig::SetConfigs(
    const std::vector<QuicServerConfigProtobuf>& protobufs,
    const QuicServerConfigProtobuf* fallback_protobuf,
    const QuicWallTime now) {
  std::vector<QuicReferenceCountedPointer<Config>> parsed_configs;
  for (auto& protobuf : protobufs) {
    QuicReferenceCountedPointer<Config> config =
        ParseConfigProtobuf(protobuf, /* is_fallback = */ false);
    if (!config) {
      QUIC_LOG(WARNING) << "Rejecting QUIC configs because of above errors";
      return false;
    }

    parsed_configs.push_back(config);
  }

  QuicReferenceCountedPointer<Config> fallback_config;
  if (fallback_protobuf != nullptr) {
    fallback_config =
        ParseConfigProtobuf(*fallback_protobuf, /* is_fallback = */ true);
    if (!fallback_config) {
      QUIC_LOG(WARNING) << "Rejecting QUIC configs because of above errors";
      return false;
    }
    QUIC_LOG(INFO) << "Fallback config has scid "
                   << QuicTextUtils::HexEncode(fallback_config->id);
    parsed_configs.push_back(fallback_config);
  } else {
    QUIC_LOG(INFO) << "No fallback config provided";
  }

  if (parsed_configs.empty()) {
    QUIC_LOG(WARNING)
        << "Rejecting QUIC configs because new config list is empty.";
    return false;
  }

  QUIC_LOG(INFO) << "Updating configs:";

  QuicWriterMutexLock locked(&configs_lock_);
  ConfigMap new_configs;

  for (const QuicReferenceCountedPointer<Config>& config : parsed_configs) {
    auto it = configs_.find(config->id);
    if (it != configs_.end()) {
      QUIC_LOG(INFO) << "Keeping scid: " << QuicTextUtils::HexEncode(config->id)
                     << " orbit: "
                     << QuicTextUtils::HexEncode(
                            reinterpret_cast<const char*>(config->orbit),
                            kOrbitSize)
                     << " new primary_time "
                     << config->primary_time.ToUNIXSeconds()
                     << " old primary_time "
                     << it->second->primary_time.ToUNIXSeconds()
                     << " new priority " << config->priority << " old priority "
                     << it->second->priority;
      // Update primary_time and priority.
      it->second->primary_time = config->primary_time;
      it->second->priority = config->priority;
      new_configs.insert(*it);
    } else {
      QUIC_LOG(INFO) << "Adding scid: " << QuicTextUtils::HexEncode(config->id)
                     << " orbit: "
                     << QuicTextUtils::HexEncode(
                            reinterpret_cast<const char*>(config->orbit),
                            kOrbitSize)
                     << " primary_time " << config->primary_time.ToUNIXSeconds()
                     << " priority " << config->priority;
      new_configs.emplace(config->id, config);
    }
  }

  configs_ = std::move(new_configs);
  fallback_config_ = fallback_config;
  SelectNewPrimaryConfig(now);
  DCHECK(primary_config_.get());
  DCHECK_EQ(configs_.find(primary_config_->id)->second.get(),
            primary_config_.get());

  return true;
}

void QuicCryptoServerConfig::SetSourceAddressTokenKeys(
    const std::vector<std::string>& keys) {
  source_address_token_boxer_.SetKeys(keys);
}

void QuicCryptoServerConfig::GetConfigIds(
    std::vector<std::string>* scids) const {
  QuicReaderMutexLock locked(&configs_lock_);
  for (auto it = configs_.begin(); it != configs_.end(); ++it) {
    scids->push_back(it->first);
  }
}

void QuicCryptoServerConfig::ValidateClientHello(
    const CryptoHandshakeMessage& client_hello,
    const QuicIpAddress& client_ip,
    const QuicSocketAddress& server_address,
    QuicTransportVersion version,
    const QuicClock* clock,
    QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config,
    std::unique_ptr<ValidateClientHelloResultCallback> done_cb) const {
  const QuicWallTime now(clock->WallNow());

  QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result> result(
      new ValidateClientHelloResultCallback::Result(client_hello, client_ip,
                                                    now));

  QuicStringPiece requested_scid;
  client_hello.GetStringPiece(kSCID, &requested_scid);
  Configs configs;
  if (!GetCurrentConfigs(now, requested_scid,
                         /* old_primary_config = */ nullptr, &configs)) {
    result->error_code = QUIC_CRYPTO_INTERNAL_ERROR;
    result->error_details = "No configurations loaded";
  }
  signed_config->config = configs.primary;

  if (result->error_code == QUIC_NO_ERROR) {
    // QUIC requires a new proof for each CHLO so clear any existing proof.
    signed_config->chain = nullptr;
    signed_config->proof.signature = "";
    signed_config->proof.leaf_cert_scts = "";
    EvaluateClientHello(server_address, version, configs, result,
                        std::move(done_cb));
  } else {
    done_cb->Run(result, /* details = */ nullptr);
  }
}

class QuicCryptoServerConfig::ProcessClientHelloCallback
    : public ProofSource::Callback {
 public:
  ProcessClientHelloCallback(const QuicCryptoServerConfig* config,
                             std::unique_ptr<ProcessClientHelloContext> context,
                             const Configs& configs)
      : config_(config), context_(std::move(context)), configs_(configs) {}

  void Run(bool ok,
           const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
           const QuicCryptoProof& proof,
           std::unique_ptr<ProofSource::Details> details) override {
    if (ok) {
      context_->signed_config()->chain = chain;
      context_->signed_config()->proof = proof;
    }
    config_->ProcessClientHelloAfterGetProof(!ok, std::move(details),
                                             std::move(context_), configs_);
  }

 private:
  const QuicCryptoServerConfig* config_;
  std::unique_ptr<ProcessClientHelloContext> context_;
  const Configs configs_;
};

class QuicCryptoServerConfig::ProcessClientHelloAfterGetProofCallback
    : public AsynchronousKeyExchange::Callback {
 public:
  ProcessClientHelloAfterGetProofCallback(
      const QuicCryptoServerConfig* config,
      std::unique_ptr<ProofSource::Details> proof_source_details,
      QuicTag key_exchange_type,
      std::unique_ptr<CryptoHandshakeMessage> out,
      QuicStringPiece public_value,
      std::unique_ptr<ProcessClientHelloContext> context,
      const Configs& configs)
      : config_(config),
        proof_source_details_(std::move(proof_source_details)),
        key_exchange_type_(key_exchange_type),
        out_(std::move(out)),
        public_value_(public_value),
        context_(std::move(context)),
        configs_(configs) {}

  void Run(bool ok) override {
    config_->ProcessClientHelloAfterCalculateSharedKeys(
        !ok, std::move(proof_source_details_), key_exchange_type_,
        std::move(out_), public_value_, std::move(context_), configs_);
  }

 private:
  const QuicCryptoServerConfig* config_;
  std::unique_ptr<ProofSource::Details> proof_source_details_;
  const QuicTag key_exchange_type_;
  std::unique_ptr<CryptoHandshakeMessage> out_;
  const std::string public_value_;
  std::unique_ptr<ProcessClientHelloContext> context_;
  const Configs configs_;
  std::unique_ptr<ProcessClientHelloResultCallback> done_cb_;
};

class QuicCryptoServerConfig::SendRejectWithFallbackConfigCallback
    : public ProofSource::Callback {
 public:
  SendRejectWithFallbackConfigCallback(
      const QuicCryptoServerConfig* config,
      std::unique_ptr<ProcessClientHelloContext> context,
      QuicReferenceCountedPointer<Config> fallback_config)
      : config_(config),
        context_(std::move(context)),
        fallback_config_(fallback_config) {}

  // Capture |chain| and |proof| into the signed config, and then invoke
  // SendRejectWithFallbackConfigAfterGetProof.
  void Run(bool ok,
           const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
           const QuicCryptoProof& proof,
           std::unique_ptr<ProofSource::Details> details) override {
    if (ok) {
      context_->signed_config()->chain = chain;
      context_->signed_config()->proof = proof;
    }
    config_->SendRejectWithFallbackConfigAfterGetProof(
        !ok, std::move(details), std::move(context_), fallback_config_);
  }

 private:
  const QuicCryptoServerConfig* config_;
  std::unique_ptr<ProcessClientHelloContext> context_;
  QuicReferenceCountedPointer<Config> fallback_config_;
};

void QuicCryptoServerConfig::ProcessClientHello(
    QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
        validate_chlo_result,
    bool reject_only,
    QuicConnectionId connection_id,
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    ParsedQuicVersion version,
    const ParsedQuicVersionVector& supported_versions,
    const QuicClock* clock,
    QuicRandom* rand,
    QuicCompressedCertsCache* compressed_certs_cache,
    QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params,
    QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config,
    QuicByteCount total_framing_overhead,
    QuicByteCount chlo_packet_size,
    std::unique_ptr<ProcessClientHelloResultCallback> done_cb) const {
  DCHECK(done_cb);
  auto context = QuicMakeUnique<ProcessClientHelloContext>(
      validate_chlo_result, reject_only, connection_id, server_address,
      client_address, version, supported_versions, clock, rand,
      compressed_certs_cache, params, signed_config, total_framing_overhead,
      chlo_packet_size, std::move(done_cb));

  // Verify that various parts of the CHLO are valid
  std::string error_details;
  QuicErrorCode valid = CryptoUtils::ValidateClientHello(
      context->client_hello(), context->version(),
      context->supported_versions(), &error_details);
  if (valid != QUIC_NO_ERROR) {
    context->Fail(valid, error_details);
    return;
  }

  QuicStringPiece requested_scid;
  context->client_hello().GetStringPiece(kSCID, &requested_scid);
  Configs configs;
  if (!GetCurrentConfigs(context->clock()->WallNow(), requested_scid,
                         signed_config->config, &configs)) {
    context->Fail(QUIC_CRYPTO_INTERNAL_ERROR, "No configurations loaded");
    return;
  }

  if (context->validate_chlo_result()->error_code != QUIC_NO_ERROR) {
    context->Fail(context->validate_chlo_result()->error_code,
                  context->validate_chlo_result()->error_details);
    return;
  }

  if (!ClientDemandsX509Proof(context->client_hello())) {
    context->Fail(QUIC_UNSUPPORTED_PROOF_DEMAND, "Missing or invalid PDMD");
    return;
  }

  // No need to get a new proof if one was already generated.
  if (!context->signed_config()->chain) {
    const std::string chlo_hash = CryptoUtils::HashHandshakeMessage(
        context->client_hello(), Perspective::IS_SERVER);
    const QuicSocketAddress server_address = context->server_address();
    const std::string sni = std::string(context->info().sni);
    const QuicTransportVersion transport_version = context->transport_version();

    auto cb = QuicMakeUnique<ProcessClientHelloCallback>(
        this, std::move(context), configs);

    DCHECK(proof_source_.get());
    proof_source_->GetProof(server_address, sni, configs.primary->serialized,
                            transport_version, chlo_hash, std::move(cb));
    return;
  }

  ProcessClientHelloAfterGetProof(
      /* found_error = */ false, /* proof_source_details = */ nullptr,
      std::move(context), configs);
}

void QuicCryptoServerConfig::ProcessClientHelloAfterGetProof(
    bool found_error,
    std::unique_ptr<ProofSource::Details> proof_source_details,
    std::unique_ptr<ProcessClientHelloContext> context,
    const Configs& configs) const {
  QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(
      context->connection_id(), context->transport_version()))
      << "ProcessClientHelloAfterGetProof: attempted to use connection ID "
      << context->connection_id() << " which is invalid with version "
      << QuicVersionToString(context->transport_version());

  if (found_error) {
    context->Fail(QUIC_HANDSHAKE_FAILED, "Failed to get proof");
    return;
  }

  auto out_diversification_nonce = QuicMakeUnique<DiversificationNonce>();

  QuicStringPiece cert_sct;
  if (context->client_hello().GetStringPiece(kCertificateSCTTag, &cert_sct) &&
      cert_sct.empty()) {
    context->params()->sct_supported_by_client = true;
  }

  auto out = QuicMakeUnique<CryptoHandshakeMessage>();
  if (!context->info().reject_reasons.empty() || !configs.requested) {
    BuildRejectionAndRecordStats(*context, *configs.primary,
                                 context->info().reject_reasons, out.get());
    context->Succeed(std::move(out), std::move(out_diversification_nonce),
                     std::move(proof_source_details));
    return;
  }

  if (context->reject_only()) {
    context->Succeed(std::move(out), std::move(out_diversification_nonce),
                     std::move(proof_source_details));
    return;
  }

  QuicTagVector their_aeads;
  QuicTagVector their_key_exchanges;
  if (context->client_hello().GetTaglist(kAEAD, &their_aeads) !=
          QUIC_NO_ERROR ||
      context->client_hello().GetTaglist(kKEXS, &their_key_exchanges) !=
          QUIC_NO_ERROR ||
      their_aeads.size() != 1 || their_key_exchanges.size() != 1) {
    context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                  "Missing or invalid AEAD or KEXS");
    return;
  }

  size_t key_exchange_index;
  if (!FindMutualQuicTag(configs.requested->aead, their_aeads,
                         &context->params()->aead, nullptr) ||
      !FindMutualQuicTag(configs.requested->kexs, their_key_exchanges,
                         &context->params()->key_exchange,
                         &key_exchange_index)) {
    context->Fail(QUIC_CRYPTO_NO_SUPPORT, "Unsupported AEAD or KEXS");
    return;
  }

  QuicStringPiece public_value;
  if (!context->client_hello().GetStringPiece(kPUBS, &public_value)) {
    context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                  "Missing public value");
    return;
  }

  const AsynchronousKeyExchange* key_exchange =
      configs.requested->key_exchanges[key_exchange_index].get();
  std::string* initial_premaster_secret =
      &context->params()->initial_premaster_secret;
  auto cb = QuicMakeUnique<ProcessClientHelloAfterGetProofCallback>(
      this, std::move(proof_source_details), key_exchange->type(),
      std::move(out), public_value, std::move(context), configs);
  key_exchange->CalculateSharedKeyAsync(public_value, initial_premaster_secret,
                                        std::move(cb));
}

void QuicCryptoServerConfig::ProcessClientHelloAfterCalculateSharedKeys(
    bool found_error,
    std::unique_ptr<ProofSource::Details> proof_source_details,
    QuicTag key_exchange_type,
    std::unique_ptr<CryptoHandshakeMessage> out,
    QuicStringPiece public_value,
    std::unique_ptr<ProcessClientHelloContext> context,
    const Configs& configs) const {
  QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(
      context->connection_id(), context->transport_version()))
      << "ProcessClientHelloAfterCalculateSharedKeys:"
         " attempted to use connection ID "
      << context->connection_id() << " which is invalid with version "
      << QuicVersionToString(context->transport_version());

  if (found_error) {
    // If we are already using the fallback config, just bail out of the
    // handshake.
    if (context->signed_config()->config == configs.fallback ||
        !GetQuicReloadableFlag(
            send_quic_fallback_server_config_on_leto_error)) {
      context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                    "Failed to calculate shared key");
    } else {
      SendRejectWithFallbackConfig(std::move(context), configs.fallback);
    }
    return;
  }

  if (!context->info().sni.empty()) {
    context->params()->sni =
        QuicHostnameUtils::NormalizeHostname(context->info().sni);
  }

  std::string hkdf_suffix;
  const QuicData& client_hello_serialized =
      context->client_hello().GetSerialized();
  hkdf_suffix.reserve(context->connection_id().length() +
                      client_hello_serialized.length() +
                      configs.requested->serialized.size());
  hkdf_suffix.append(context->connection_id().data(),
                     context->connection_id().length());
  hkdf_suffix.append(client_hello_serialized.data(),
                     client_hello_serialized.length());
  hkdf_suffix.append(configs.requested->serialized);
  DCHECK(proof_source_.get());
  if (context->signed_config()->chain->certs.empty()) {
    context->Fail(QUIC_CRYPTO_INTERNAL_ERROR, "Failed to get certs");
    return;
  }
  hkdf_suffix.append(context->signed_config()->chain->certs.at(0));

  QuicStringPiece cetv_ciphertext;
  if (configs.requested->channel_id_enabled &&
      context->client_hello().GetStringPiece(kCETV, &cetv_ciphertext)) {
    CryptoHandshakeMessage client_hello_copy(context->client_hello());
    client_hello_copy.Erase(kCETV);
    client_hello_copy.Erase(kPAD);

    const QuicData& client_hello_copy_serialized =
        client_hello_copy.GetSerialized();
    std::string hkdf_input;
    hkdf_input.append(QuicCryptoConfig::kCETVLabel,
                      strlen(QuicCryptoConfig::kCETVLabel) + 1);
    hkdf_input.append(context->connection_id().data(),
                      context->connection_id().length());
    hkdf_input.append(client_hello_copy_serialized.data(),
                      client_hello_copy_serialized.length());
    hkdf_input.append(configs.requested->serialized);

    CrypterPair crypters;
    if (!CryptoUtils::DeriveKeys(
            context->params()->initial_premaster_secret,
            context->params()->aead, context->info().client_nonce,
            context->info().server_nonce, pre_shared_key_, hkdf_input,
            Perspective::IS_SERVER, CryptoUtils::Diversification::Never(),
            &crypters, nullptr /* subkey secret */)) {
      context->Fail(QUIC_CRYPTO_SYMMETRIC_KEY_SETUP_FAILED,
                    "Symmetric key setup failed");
      return;
    }

    char plaintext[kMaxOutgoingPacketSize];
    size_t plaintext_length = 0;
    const bool success = crypters.decrypter->DecryptPacket(
        0 /* packet number */, QuicStringPiece() /* associated data */,
        cetv_ciphertext, plaintext, &plaintext_length, kMaxOutgoingPacketSize);
    if (!success) {
      context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                    "CETV decryption failure");
      return;
    }
    std::unique_ptr<CryptoHandshakeMessage> cetv(CryptoFramer::ParseMessage(
        QuicStringPiece(plaintext, plaintext_length)));
    if (!cetv.get()) {
      context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER, "CETV parse error");
      return;
    }

    QuicStringPiece key, signature;
    if (cetv->GetStringPiece(kCIDK, &key) &&
        cetv->GetStringPiece(kCIDS, &signature)) {
      if (!ChannelIDVerifier::Verify(key, hkdf_input, signature)) {
        context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                      "ChannelID signature failure");
        return;
      }

      context->params()->channel_id = std::string(key);
    }
  }

  std::string hkdf_input;
  size_t label_len = strlen(QuicCryptoConfig::kInitialLabel) + 1;
  hkdf_input.reserve(label_len + hkdf_suffix.size());
  hkdf_input.append(QuicCryptoConfig::kInitialLabel, label_len);
  hkdf_input.append(hkdf_suffix);

  auto out_diversification_nonce = QuicMakeUnique<DiversificationNonce>();
  context->rand()->RandBytes(out_diversification_nonce->data(),
                             out_diversification_nonce->size());
  CryptoUtils::Diversification diversification =
      CryptoUtils::Diversification::Now(out_diversification_nonce.get());
  if (!CryptoUtils::DeriveKeys(
          context->params()->initial_premaster_secret, context->params()->aead,
          context->info().client_nonce, context->info().server_nonce,
          pre_shared_key_, hkdf_input, Perspective::IS_SERVER, diversification,
          &context->params()->initial_crypters,
          &context->params()->initial_subkey_secret)) {
    context->Fail(QUIC_CRYPTO_SYMMETRIC_KEY_SETUP_FAILED,
                  "Symmetric key setup failed");
    return;
  }

  std::string forward_secure_public_value;
  std::unique_ptr<SynchronousKeyExchange> forward_secure_key_exchange =
      CreateLocalSynchronousKeyExchange(key_exchange_type, context->rand());
  if (!forward_secure_key_exchange) {
    QUIC_DLOG(WARNING) << "Failed to create keypair";
    context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                  "Failed to create keypair");
    return;
  }

  forward_secure_public_value =
      std::string(forward_secure_key_exchange->public_value());
  if (!forward_secure_key_exchange->CalculateSharedKeySync(
          public_value, &context->params()->forward_secure_premaster_secret)) {
    context->Fail(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                  "Invalid public value");
    return;
  }

  std::string forward_secure_hkdf_input;
  label_len = strlen(QuicCryptoConfig::kForwardSecureLabel) + 1;
  forward_secure_hkdf_input.reserve(label_len + hkdf_suffix.size());
  forward_secure_hkdf_input.append(QuicCryptoConfig::kForwardSecureLabel,
                                   label_len);
  forward_secure_hkdf_input.append(hkdf_suffix);

  std::string shlo_nonce;
  shlo_nonce = NewServerNonce(context->rand(), context->info().now);
  out->SetStringPiece(kServerNonceTag, shlo_nonce);

  if (!CryptoUtils::DeriveKeys(
          context->params()->forward_secure_premaster_secret,
          context->params()->aead, context->info().client_nonce,
          shlo_nonce.empty() ? context->info().server_nonce : shlo_nonce,
          pre_shared_key_, forward_secure_hkdf_input, Perspective::IS_SERVER,
          CryptoUtils::Diversification::Never(),
          &context->params()->forward_secure_crypters,
          &context->params()->subkey_secret)) {
    context->Fail(QUIC_CRYPTO_SYMMETRIC_KEY_SETUP_FAILED,
                  "Symmetric key setup failed");
    return;
  }

  out->set_tag(kSHLO);
  out->SetVersionVector(kVER, context->supported_versions());
  out->SetStringPiece(
      kSourceAddressTokenTag,
      NewSourceAddressToken(*configs.requested,
                            context->info().source_address_tokens,
                            context->client_address().host(), context->rand(),
                            context->info().now, nullptr));
  QuicSocketAddressCoder address_coder(context->client_address());
  out->SetStringPiece(kCADR, address_coder.Encode());
  out->SetStringPiece(kPUBS, forward_secure_public_value);

  context->Succeed(std::move(out), std::move(out_diversification_nonce),
                   std::move(proof_source_details));
}

void QuicCryptoServerConfig::SendRejectWithFallbackConfig(
    std::unique_ptr<ProcessClientHelloContext> context,
    QuicReferenceCountedPointer<Config> fallback_config) const {
  // We failed to calculate a shared initial key, likely because we tried to use
  // a remote key-exchange service which could not be reached.  We want to send
  // a REJ which tells the client to use a different ServerConfig which
  // corresponds to a local keypair.  To generate the REJ we need to request a
  // new proof.
  const std::string chlo_hash = CryptoUtils::HashHandshakeMessage(
      context->client_hello(), Perspective::IS_SERVER);
  const QuicSocketAddress server_address = context->server_address();
  const std::string sni(context->info().sni);
  const QuicTransportVersion transport_version = context->transport_version();

  auto cb = QuicMakeUnique<SendRejectWithFallbackConfigCallback>(
      this, std::move(context), fallback_config);
  proof_source_->GetProof(server_address, sni, fallback_config->serialized,
                          transport_version, chlo_hash, std::move(cb));
}

void QuicCryptoServerConfig::SendRejectWithFallbackConfigAfterGetProof(
    bool found_error,
    std::unique_ptr<ProofSource::Details> proof_source_details,
    std::unique_ptr<ProcessClientHelloContext> context,
    QuicReferenceCountedPointer<Config> fallback_config) const {
  if (found_error) {
    context->Fail(QUIC_HANDSHAKE_FAILED, "Failed to get proof");
    return;
  }

  auto out = QuicMakeUnique<CryptoHandshakeMessage>();
  BuildRejectionAndRecordStats(*context, *fallback_config,
                               {SERVER_CONFIG_UNKNOWN_CONFIG_FAILURE},
                               out.get());

  context->Succeed(std::move(out), QuicMakeUnique<DiversificationNonce>(),
                   std::move(proof_source_details));
}

QuicReferenceCountedPointer<QuicCryptoServerConfig::Config>
QuicCryptoServerConfig::GetConfigWithScid(
    QuicStringPiece requested_scid) const {
  configs_lock_.AssertReaderHeld();

  if (!requested_scid.empty()) {
    auto it = configs_.find((std::string(requested_scid)));
    if (it != configs_.end()) {
      // We'll use the config that the client requested in order to do
      // key-agreement.
      return QuicReferenceCountedPointer<Config>(it->second);
    }
  }

  return QuicReferenceCountedPointer<Config>();
}

bool QuicCryptoServerConfig::GetCurrentConfigs(
    const QuicWallTime& now,
    QuicStringPiece requested_scid,
    QuicReferenceCountedPointer<Config> old_primary_config,
    Configs* configs) const {
  QuicReaderMutexLock locked(&configs_lock_);

  if (!primary_config_) {
    return false;
  }

  if (IsNextConfigReady(now)) {
    configs_lock_.ReaderUnlock();
    configs_lock_.WriterLock();
    SelectNewPrimaryConfig(now);
    DCHECK(primary_config_.get());
    DCHECK_EQ(configs_.find(primary_config_->id)->second.get(),
              primary_config_.get());
    configs_lock_.WriterUnlock();
    configs_lock_.ReaderLock();
  }

  if (old_primary_config != nullptr) {
    configs->primary = old_primary_config;
  } else {
    configs->primary = primary_config_;
  }
  configs->requested = GetConfigWithScid(requested_scid);
  configs->fallback = fallback_config_;

  return true;
}

// ConfigPrimaryTimeLessThan is a comparator that implements "less than" for
// Config's based on their primary_time.
// static
bool QuicCryptoServerConfig::ConfigPrimaryTimeLessThan(
    const QuicReferenceCountedPointer<Config>& a,
    const QuicReferenceCountedPointer<Config>& b) {
  if (a->primary_time.IsBefore(b->primary_time) ||
      b->primary_time.IsBefore(a->primary_time)) {
    // Primary times differ.
    return a->primary_time.IsBefore(b->primary_time);
  } else if (a->priority != b->priority) {
    // Primary times are equal, sort backwards by priority.
    return a->priority < b->priority;
  } else {
    // Primary times and priorities are equal, sort by config id.
    return a->id < b->id;
  }
}

void QuicCryptoServerConfig::SelectNewPrimaryConfig(
    const QuicWallTime now) const {
  std::vector<QuicReferenceCountedPointer<Config>> configs;
  configs.reserve(configs_.size());

  for (auto it = configs_.begin(); it != configs_.end(); ++it) {
    // TODO(avd) Exclude expired configs?
    configs.push_back(it->second);
  }

  if (configs.empty()) {
    if (primary_config_ != nullptr) {
      QUIC_BUG << "No valid QUIC server config. Keeping the current config.";
    } else {
      QUIC_BUG << "No valid QUIC server config.";
    }
    return;
  }

  std::sort(configs.begin(), configs.end(), ConfigPrimaryTimeLessThan);

  QuicReferenceCountedPointer<Config> best_candidate = configs[0];

  for (size_t i = 0; i < configs.size(); ++i) {
    const QuicReferenceCountedPointer<Config> config(configs[i]);
    if (!config->primary_time.IsAfter(now)) {
      if (config->primary_time.IsAfter(best_candidate->primary_time)) {
        best_candidate = config;
      }
      continue;
    }

    // This is the first config with a primary_time in the future. Thus the
    // previous Config should be the primary and this one should determine the
    // next_config_promotion_time_.
    QuicReferenceCountedPointer<Config> new_primary = best_candidate;
    if (i == 0) {
      // We need the primary_time of the next config.
      if (configs.size() > 1) {
        next_config_promotion_time_ = configs[1]->primary_time;
      } else {
        next_config_promotion_time_ = QuicWallTime::Zero();
      }
    } else {
      next_config_promotion_time_ = config->primary_time;
    }

    if (primary_config_) {
      primary_config_->is_primary = false;
    }
    primary_config_ = new_primary;
    new_primary->is_primary = true;
    QUIC_DLOG(INFO) << "New primary config.  orbit: "
                    << QuicTextUtils::HexEncode(reinterpret_cast<const char*>(
                                                    primary_config_->orbit),
                                                kOrbitSize);
    if (primary_config_changed_cb_ != nullptr) {
      primary_config_changed_cb_->Run(primary_config_->id);
    }

    return;
  }

  // All config's primary times are in the past. We should make the most recent
  // and highest priority candidate primary.
  QuicReferenceCountedPointer<Config> new_primary = best_candidate;
  if (primary_config_) {
    primary_config_->is_primary = false;
  }
  primary_config_ = new_primary;
  new_primary->is_primary = true;
  QUIC_DLOG(INFO) << "New primary config.  orbit: "
                  << QuicTextUtils::HexEncode(
                         reinterpret_cast<const char*>(primary_config_->orbit),
                         kOrbitSize)
                  << " scid: " << QuicTextUtils::HexEncode(primary_config_->id);
  next_config_promotion_time_ = QuicWallTime::Zero();
  if (primary_config_changed_cb_ != nullptr) {
    primary_config_changed_cb_->Run(primary_config_->id);
  }
}

void QuicCryptoServerConfig::EvaluateClientHello(
    const QuicSocketAddress& server_address,
    QuicTransportVersion /*version*/,
    const Configs& configs,
    QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
        client_hello_state,
    std::unique_ptr<ValidateClientHelloResultCallback> done_cb) const {
  ValidateClientHelloHelper helper(client_hello_state, &done_cb);

  const CryptoHandshakeMessage& client_hello = client_hello_state->client_hello;
  ClientHelloInfo* info = &(client_hello_state->info);

  if (validate_chlo_size_ && client_hello.size() < kClientHelloMinimumSize) {
    helper.ValidationComplete(QUIC_CRYPTO_INVALID_VALUE_LENGTH,
                              "Client hello too small", nullptr);
    return;
  }

  if (client_hello.GetStringPiece(kSNI, &info->sni) &&
      !QuicHostnameUtils::IsValidSNI(info->sni)) {
    helper.ValidationComplete(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER,
                              "Invalid SNI name", nullptr);
    return;
  }

  client_hello.GetStringPiece(kUAID, &info->user_agent_id);

  HandshakeFailureReason source_address_token_error = MAX_FAILURE_REASON;
  if (validate_source_address_token_) {
    QuicStringPiece srct;
    if (client_hello.GetStringPiece(kSourceAddressTokenTag, &srct)) {
      Config& config =
          configs.requested != nullptr ? *configs.requested : *configs.primary;
      source_address_token_error =
          ParseSourceAddressToken(config, srct, &info->source_address_tokens);

      if (source_address_token_error == HANDSHAKE_OK) {
        source_address_token_error = ValidateSourceAddressTokens(
            info->source_address_tokens, info->client_ip, info->now,
            &client_hello_state->cached_network_params);
      }
      info->valid_source_address_token =
          (source_address_token_error == HANDSHAKE_OK);
    } else {
      source_address_token_error = SOURCE_ADDRESS_TOKEN_INVALID_FAILURE;
    }
  } else {
    source_address_token_error = HANDSHAKE_OK;
    info->valid_source_address_token = true;
  }

  if (!configs.requested) {
    QuicStringPiece requested_scid;
    if (client_hello.GetStringPiece(kSCID, &requested_scid)) {
      info->reject_reasons.push_back(SERVER_CONFIG_UNKNOWN_CONFIG_FAILURE);
    } else {
      info->reject_reasons.push_back(SERVER_CONFIG_INCHOATE_HELLO_FAILURE);
    }
    // No server config with the requested ID.
    helper.ValidationComplete(QUIC_NO_ERROR, "", nullptr);
    return;
  }

  if (!client_hello.GetStringPiece(kNONC, &info->client_nonce)) {
    info->reject_reasons.push_back(SERVER_CONFIG_INCHOATE_HELLO_FAILURE);
    // Report no client nonce as INCHOATE_HELLO_FAILURE.
    helper.ValidationComplete(QUIC_NO_ERROR, "", nullptr);
    return;
  }

  if (source_address_token_error != HANDSHAKE_OK) {
    info->reject_reasons.push_back(source_address_token_error);
    // No valid source address token.
  }

  QuicReferenceCountedPointer<ProofSource::Chain> chain =
      proof_source_->GetCertChain(server_address, std::string(info->sni));
  if (!chain) {
    info->reject_reasons.push_back(SERVER_CONFIG_UNKNOWN_CONFIG_FAILURE);
  } else if (!ValidateExpectedLeafCertificate(client_hello, chain->certs)) {
    info->reject_reasons.push_back(INVALID_EXPECTED_LEAF_CERTIFICATE);
  }

  if (info->client_nonce.size() != kNonceSize) {
    info->reject_reasons.push_back(CLIENT_NONCE_INVALID_FAILURE);
    // Invalid client nonce.
    QUIC_LOG_FIRST_N(ERROR, 2)
        << "Invalid client nonce: " << client_hello.DebugString();
    QUIC_DLOG(INFO) << "Invalid client nonce.";
  }

  // Server nonce is optional, and used for key derivation if present.
  client_hello.GetStringPiece(kServerNonceTag, &info->server_nonce);

  QUIC_DVLOG(1) << "No 0-RTT replay protection in QUIC_VERSION_33 and higher.";
  // If the server nonce is empty and we're requiring handshake confirmation
  // for DoS reasons then we must reject the CHLO.
  if (GetQuicReloadableFlag(quic_require_handshake_confirmation) &&
      info->server_nonce.empty()) {
    info->reject_reasons.push_back(SERVER_NONCE_REQUIRED_FAILURE);
  }
  helper.ValidationComplete(QUIC_NO_ERROR, "",
                            std::unique_ptr<ProofSource::Details>());
}

void QuicCryptoServerConfig::BuildServerConfigUpdateMessage(
    QuicTransportVersion version,
    QuicStringPiece chlo_hash,
    const SourceAddressTokens& previous_source_address_tokens,
    const QuicSocketAddress& server_address,
    const QuicIpAddress& client_ip,
    const QuicClock* clock,
    QuicRandom* rand,
    QuicCompressedCertsCache* compressed_certs_cache,
    const QuicCryptoNegotiatedParameters& params,
    const CachedNetworkParameters* cached_network_params,
    std::unique_ptr<BuildServerConfigUpdateMessageResultCallback> cb) const {
  std::string serialized;
  std::string source_address_token;
  const CommonCertSets* common_cert_sets;
  {
    QuicReaderMutexLock locked(&configs_lock_);
    serialized = primary_config_->serialized;
    common_cert_sets = primary_config_->common_cert_sets;
    source_address_token = NewSourceAddressToken(
        *primary_config_, previous_source_address_tokens, client_ip, rand,
        clock->WallNow(), cached_network_params);
  }

  CryptoHandshakeMessage message;
  message.set_tag(kSCUP);
  message.SetStringPiece(kSCFG, serialized);
  message.SetStringPiece(kSourceAddressTokenTag, source_address_token);

  auto proof_source_cb =
      QuicMakeUnique<BuildServerConfigUpdateMessageProofSourceCallback>(
          this, compressed_certs_cache, common_cert_sets, params,
          std::move(message), std::move(cb));

  proof_source_->GetProof(server_address, params.sni, serialized, version,
                          chlo_hash, std::move(proof_source_cb));
}

QuicCryptoServerConfig::BuildServerConfigUpdateMessageProofSourceCallback::
    ~BuildServerConfigUpdateMessageProofSourceCallback() {}

QuicCryptoServerConfig::BuildServerConfigUpdateMessageProofSourceCallback::
    BuildServerConfigUpdateMessageProofSourceCallback(
        const QuicCryptoServerConfig* config,
        QuicCompressedCertsCache* compressed_certs_cache,
        const CommonCertSets* common_cert_sets,
        const QuicCryptoNegotiatedParameters& params,
        CryptoHandshakeMessage message,
        std::unique_ptr<BuildServerConfigUpdateMessageResultCallback> cb)
    : config_(config),
      compressed_certs_cache_(compressed_certs_cache),
      common_cert_sets_(common_cert_sets),
      client_common_set_hashes_(params.client_common_set_hashes),
      client_cached_cert_hashes_(params.client_cached_cert_hashes),
      sct_supported_by_client_(params.sct_supported_by_client),
      sni_(params.sni),
      message_(std::move(message)),
      cb_(std::move(cb)) {}

void QuicCryptoServerConfig::BuildServerConfigUpdateMessageProofSourceCallback::
    Run(bool ok,
        const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
        const QuicCryptoProof& proof,
        std::unique_ptr<ProofSource::Details> details) {
  config_->FinishBuildServerConfigUpdateMessage(
      compressed_certs_cache_, common_cert_sets_, client_common_set_hashes_,
      client_cached_cert_hashes_, sct_supported_by_client_, sni_, ok, chain,
      proof.signature, proof.leaf_cert_scts, std::move(details),
      std::move(message_), std::move(cb_));
}

void QuicCryptoServerConfig::FinishBuildServerConfigUpdateMessage(
    QuicCompressedCertsCache* compressed_certs_cache,
    const CommonCertSets* common_cert_sets,
    const std::string& client_common_set_hashes,
    const std::string& client_cached_cert_hashes,
    bool sct_supported_by_client,
    const std::string& sni,
    bool ok,
    const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
    const std::string& signature,
    const std::string& leaf_cert_sct,
    std::unique_ptr<ProofSource::Details> /*details*/,
    CryptoHandshakeMessage message,
    std::unique_ptr<BuildServerConfigUpdateMessageResultCallback> cb) const {
  if (!ok) {
    cb->Run(false, message);
    return;
  }

  const std::string compressed =
      CompressChain(compressed_certs_cache, chain, client_common_set_hashes,
                    client_cached_cert_hashes, common_cert_sets);

  message.SetStringPiece(kCertificateTag, compressed);
  message.SetStringPiece(kPROF, signature);
  if (sct_supported_by_client && enable_serving_sct_) {
    if (leaf_cert_sct.empty()) {
      QUIC_LOG_EVERY_N_SEC(WARNING, 60)
          << "SCT is expected but it is empty. SNI: " << sni;
    } else {
      message.SetStringPiece(kCertificateSCTTag, leaf_cert_sct);
    }
  }

  cb->Run(true, message);
}

void QuicCryptoServerConfig::BuildRejectionAndRecordStats(
    const ProcessClientHelloContext& context,
    const Config& config,
    const std::vector<uint32_t>& reject_reasons,
    CryptoHandshakeMessage* out) const {
  BuildRejection(context, config, reject_reasons, out);
  if (rejection_observer_ != nullptr) {
    rejection_observer_->OnRejectionBuilt(reject_reasons, out);
  }
}

void QuicCryptoServerConfig::BuildRejection(
    const ProcessClientHelloContext& context,
    const Config& config,
    const std::vector<uint32_t>& reject_reasons,
    CryptoHandshakeMessage* out) const {
  const QuicWallTime now = context.clock()->WallNow();

  out->set_tag(kREJ);
  out->SetStringPiece(kSCFG, config.serialized);
  out->SetStringPiece(
      kSourceAddressTokenTag,
      NewSourceAddressToken(
          config, context.info().source_address_tokens,
          context.info().client_ip, context.rand(), context.info().now,
          &context.validate_chlo_result()->cached_network_params));
  out->SetValue(kSTTL, config.expiry_time.AbsoluteDifference(now).ToSeconds());
  if (replay_protection_) {
    out->SetStringPiece(kServerNonceTag,
                        NewServerNonce(context.rand(), context.info().now));
  }

  // Send client the reject reason for debugging purposes.
  DCHECK_LT(0u, reject_reasons.size());
  out->SetVector(kRREJ, reject_reasons);

  // The client may have requested a certificate chain.
  if (!ClientDemandsX509Proof(context.client_hello())) {
    QUIC_BUG << "x509 certificates not supported in proof demand";
    return;
  }

  QuicStringPiece client_common_set_hashes;
  if (context.client_hello().GetStringPiece(kCCS, &client_common_set_hashes)) {
    context.params()->client_common_set_hashes =
        std::string(client_common_set_hashes);
  }

  QuicStringPiece client_cached_cert_hashes;
  if (context.client_hello().GetStringPiece(kCCRT,
                                            &client_cached_cert_hashes)) {
    context.params()->client_cached_cert_hashes =
        std::string(client_cached_cert_hashes);
  } else {
    context.params()->client_cached_cert_hashes.clear();
  }

  const std::string compressed = CompressChain(
      context.compressed_certs_cache(), context.signed_config()->chain,
      context.params()->client_common_set_hashes,
      context.params()->client_cached_cert_hashes, config.common_cert_sets);

  DCHECK_GT(context.chlo_packet_size(), context.client_hello().size());
  // kREJOverheadBytes is a very rough estimate of how much of a REJ
  // message is taken up by things other than the certificates.
  // STK: 56 bytes
  // SNO: 56 bytes
  // SCFG
  //   SCID: 16 bytes
  //   PUBS: 38 bytes
  const size_t kREJOverheadBytes = 166;
  // max_unverified_size is the number of bytes that the certificate chain,
  // signature, and (optionally) signed certificate timestamp can consume before
  // we will demand a valid source-address token.
  const size_t max_unverified_size =
      chlo_multiplier_ *
          (context.chlo_packet_size() - context.total_framing_overhead()) -
      kREJOverheadBytes;
  static_assert(kClientHelloMinimumSize * kMultiplier >= kREJOverheadBytes,
                "overhead calculation may underflow");
  bool should_return_sct =
      context.params()->sct_supported_by_client && enable_serving_sct_;
  const std::string& cert_sct = context.signed_config()->proof.leaf_cert_scts;
  const size_t sct_size = should_return_sct ? cert_sct.size() : 0;
  const size_t total_size = context.signed_config()->proof.signature.size() +
                            compressed.size() + sct_size;
  if (context.info().valid_source_address_token ||
      total_size < max_unverified_size) {
    out->SetStringPiece(kCertificateTag, compressed);
    out->SetStringPiece(kPROF, context.signed_config()->proof.signature);
    if (should_return_sct) {
      if (cert_sct.empty()) {
        // Log SNI and subject name for the leaf cert if its SCT is empty.
        // This is for debugging b/28342827.
        const std::vector<std::string>& certs =
            context.signed_config()->chain->certs;
        QuicStringPiece ca_subject;
        if (!certs.empty()) {
          QuicCertUtils::ExtractSubjectNameFromDERCert(certs[0], &ca_subject);
        }
        QUIC_LOG_EVERY_N_SEC(WARNING, 60)
            << "SCT is expected but it is empty. sni: '"
            << context.params()->sni << "' cert subject: '" << ca_subject
            << "'";
      } else {
        out->SetStringPiece(kCertificateSCTTag, cert_sct);
      }
    }
  } else {
    QUIC_LOG_EVERY_N_SEC(WARNING, 60)
        << "Sending inchoate REJ for hostname: " << context.info().sni
        << " signature: " << context.signed_config()->proof.signature.size()
        << " cert: " << compressed.size() << " sct:" << sct_size
        << " total: " << total_size << " max: " << max_unverified_size;
  }
}

std::string QuicCryptoServerConfig::CompressChain(
    QuicCompressedCertsCache* compressed_certs_cache,
    const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
    const std::string& client_common_set_hashes,
    const std::string& client_cached_cert_hashes,
    const CommonCertSets* common_sets) {
  // Check whether the compressed certs is available in the cache.
  DCHECK(compressed_certs_cache);
  const std::string* cached_value = compressed_certs_cache->GetCompressedCert(
      chain, client_common_set_hashes, client_cached_cert_hashes);
  if (cached_value) {
    return *cached_value;
  }
  std::string compressed =
      CertCompressor::CompressChain(chain->certs, client_common_set_hashes,
                                    client_cached_cert_hashes, common_sets);
  // Insert the newly compressed cert to cache.
  compressed_certs_cache->Insert(chain, client_common_set_hashes,
                                 client_cached_cert_hashes, compressed);
  return compressed;
}

QuicReferenceCountedPointer<QuicCryptoServerConfig::Config>
QuicCryptoServerConfig::ParseConfigProtobuf(
    const QuicServerConfigProtobuf& protobuf,
    bool is_fallback) const {
  std::unique_ptr<CryptoHandshakeMessage> msg =
      CryptoFramer::ParseMessage(protobuf.config());

  if (msg->tag() != kSCFG) {
    QUIC_LOG(WARNING) << "Server config message has tag " << msg->tag()
                      << " expected " << kSCFG;
    return nullptr;
  }

  QuicReferenceCountedPointer<Config> config(new Config);
  config->serialized = protobuf.config();
  config->source_address_token_boxer = &source_address_token_boxer_;

  if (protobuf.has_primary_time()) {
    config->primary_time =
        QuicWallTime::FromUNIXSeconds(protobuf.primary_time());
  }

  config->priority = protobuf.priority();

  QuicStringPiece scid;
  if (!msg->GetStringPiece(kSCID, &scid)) {
    QUIC_LOG(WARNING) << "Server config message is missing SCID";
    return nullptr;
  }
  config->id = std::string(scid);

  if (msg->GetTaglist(kAEAD, &config->aead) != QUIC_NO_ERROR) {
    QUIC_LOG(WARNING) << "Server config message is missing AEAD";
    return nullptr;
  }

  QuicTagVector kexs_tags;
  if (msg->GetTaglist(kKEXS, &kexs_tags) != QUIC_NO_ERROR) {
    QUIC_LOG(WARNING) << "Server config message is missing KEXS";
    return nullptr;
  }

  QuicStringPiece orbit;
  if (!msg->GetStringPiece(kORBT, &orbit)) {
    QUIC_LOG(WARNING) << "Server config message is missing ORBT";
    return nullptr;
  }

  if (orbit.size() != kOrbitSize) {
    QUIC_LOG(WARNING) << "Orbit value in server config is the wrong length."
                         " Got "
                      << orbit.size() << " want " << kOrbitSize;
    return nullptr;
  }
  static_assert(sizeof(config->orbit) == kOrbitSize, "incorrect orbit size");
  memcpy(config->orbit, orbit.data(), sizeof(config->orbit));

  if ((kexs_tags.size() != static_cast<size_t>(protobuf.key_size())) &&
      (!GetQuicRestartFlag(dont_fetch_quic_private_keys_from_leto) &&
       protobuf.key_size() == 0)) {
    QUIC_LOG(WARNING) << "Server config has " << kexs_tags.size()
                      << " key exchange methods configured, but "
                      << protobuf.key_size() << " private keys";
    return nullptr;
  }

  QuicTagVector proof_demand_tags;
  if (msg->GetTaglist(kPDMD, &proof_demand_tags) == QUIC_NO_ERROR) {
    for (QuicTag tag : proof_demand_tags) {
      if (tag == kCHID) {
        config->channel_id_enabled = true;
        break;
      }
    }
  }

  for (size_t i = 0; i < kexs_tags.size(); i++) {
    const QuicTag tag = kexs_tags[i];
    std::string private_key;

    config->kexs.push_back(tag);

    for (int j = 0; j < protobuf.key_size(); j++) {
      const QuicServerConfigProtobuf::PrivateKey& key = protobuf.key(i);
      if (key.tag() == tag) {
        private_key = key.private_key();
        break;
      }
    }

    std::unique_ptr<AsynchronousKeyExchange> ka =
        key_exchange_source_->Create(config->id, is_fallback, tag, private_key);
    if (!ka) {
      return nullptr;
    }
    for (const auto& key_exchange : config->key_exchanges) {
      if (key_exchange->type() == tag) {
        QUIC_LOG(WARNING) << "Duplicate key exchange in config: " << tag;
        return nullptr;
      }
    }

    config->key_exchanges.push_back(std::move(ka));
  }

  uint64_t expiry_seconds;
  if (msg->GetUint64(kEXPY, &expiry_seconds) != QUIC_NO_ERROR) {
    QUIC_LOG(WARNING) << "Server config message is missing EXPY";
    return nullptr;
  }
  config->expiry_time = QuicWallTime::FromUNIXSeconds(expiry_seconds);

  return config;
}

void QuicCryptoServerConfig::set_replay_protection(bool on) {
  replay_protection_ = on;
}

void QuicCryptoServerConfig::set_chlo_multiplier(size_t multiplier) {
  chlo_multiplier_ = multiplier;
}

void QuicCryptoServerConfig::set_source_address_token_future_secs(
    uint32_t future_secs) {
  source_address_token_future_secs_ = future_secs;
}

void QuicCryptoServerConfig::set_source_address_token_lifetime_secs(
    uint32_t lifetime_secs) {
  source_address_token_lifetime_secs_ = lifetime_secs;
}

void QuicCryptoServerConfig::set_enable_serving_sct(bool enable_serving_sct) {
  enable_serving_sct_ = enable_serving_sct;
}

void QuicCryptoServerConfig::AcquirePrimaryConfigChangedCb(
    std::unique_ptr<PrimaryConfigChangedCallback> cb) {
  QuicWriterMutexLock locked(&configs_lock_);
  primary_config_changed_cb_ = std::move(cb);
}

std::string QuicCryptoServerConfig::NewSourceAddressToken(
    const Config& config,
    const SourceAddressTokens& previous_tokens,
    const QuicIpAddress& ip,
    QuicRandom* rand,
    QuicWallTime now,
    const CachedNetworkParameters* cached_network_params) const {
  SourceAddressTokens source_address_tokens;
  SourceAddressToken* source_address_token = source_address_tokens.add_tokens();
  source_address_token->set_ip(ip.DualStacked().ToPackedString());
  source_address_token->set_timestamp(now.ToUNIXSeconds());
  if (cached_network_params != nullptr) {
    *(source_address_token->mutable_cached_network_parameters()) =
        *cached_network_params;
  }

  // Append previous tokens.
  for (const SourceAddressToken& token : previous_tokens.tokens()) {
    if (source_address_tokens.tokens_size() > kMaxTokenAddresses) {
      break;
    }

    if (token.ip() == source_address_token->ip()) {
      // It's for the same IP address.
      continue;
    }

    if (ValidateSourceAddressTokenTimestamp(token, now) != HANDSHAKE_OK) {
      continue;
    }

    *(source_address_tokens.add_tokens()) = token;
  }

  return config.source_address_token_boxer->Box(
      rand, source_address_tokens.SerializeAsString());
}

int QuicCryptoServerConfig::NumberOfConfigs() const {
  QuicReaderMutexLock locked(&configs_lock_);
  return configs_.size();
}

ProofSource* QuicCryptoServerConfig::proof_source() const {
  return proof_source_.get();
}

SSL_CTX* QuicCryptoServerConfig::ssl_ctx() const {
  return ssl_ctx_.get();
}

HandshakeFailureReason QuicCryptoServerConfig::ParseSourceAddressToken(
    const Config& config,
    QuicStringPiece token,
    SourceAddressTokens* tokens) const {
  std::string storage;
  QuicStringPiece plaintext;
  if (!config.source_address_token_boxer->Unbox(token, &storage, &plaintext)) {
    return SOURCE_ADDRESS_TOKEN_DECRYPTION_FAILURE;
  }

  if (!tokens->ParseFromArray(plaintext.data(), plaintext.size())) {
    // Some clients might still be using the old source token format so
    // attempt to parse that format.
    // TODO(rch): remove this code once the new format is ubiquitous.
    SourceAddressToken token;
    if (!token.ParseFromArray(plaintext.data(), plaintext.size())) {
      return SOURCE_ADDRESS_TOKEN_PARSE_FAILURE;
    }
    *tokens->add_tokens() = token;
  }

  return HANDSHAKE_OK;
}

HandshakeFailureReason QuicCryptoServerConfig::ValidateSourceAddressTokens(
    const SourceAddressTokens& source_address_tokens,
    const QuicIpAddress& ip,
    QuicWallTime now,
    CachedNetworkParameters* cached_network_params) const {
  HandshakeFailureReason reason =
      SOURCE_ADDRESS_TOKEN_DIFFERENT_IP_ADDRESS_FAILURE;
  for (const SourceAddressToken& token : source_address_tokens.tokens()) {
    reason = ValidateSingleSourceAddressToken(token, ip, now);
    if (reason == HANDSHAKE_OK) {
      if (token.has_cached_network_parameters()) {
        *cached_network_params = token.cached_network_parameters();
      }
      break;
    }
  }
  return reason;
}

HandshakeFailureReason QuicCryptoServerConfig::ValidateSingleSourceAddressToken(
    const SourceAddressToken& source_address_token,
    const QuicIpAddress& ip,
    QuicWallTime now) const {
  if (source_address_token.ip() != ip.DualStacked().ToPackedString()) {
    // It's for a different IP address.
    return SOURCE_ADDRESS_TOKEN_DIFFERENT_IP_ADDRESS_FAILURE;
  }

  return ValidateSourceAddressTokenTimestamp(source_address_token, now);
}

HandshakeFailureReason
QuicCryptoServerConfig::ValidateSourceAddressTokenTimestamp(
    const SourceAddressToken& source_address_token,
    QuicWallTime now) const {
  const QuicWallTime timestamp(
      QuicWallTime::FromUNIXSeconds(source_address_token.timestamp()));
  const QuicTime::Delta delta(now.AbsoluteDifference(timestamp));

  if (now.IsBefore(timestamp) &&
      delta.ToSeconds() > source_address_token_future_secs_) {
    return SOURCE_ADDRESS_TOKEN_CLOCK_SKEW_FAILURE;
  }

  if (now.IsAfter(timestamp) &&
      delta.ToSeconds() > source_address_token_lifetime_secs_) {
    return SOURCE_ADDRESS_TOKEN_EXPIRED_FAILURE;
  }

  return HANDSHAKE_OK;
}

// kServerNoncePlaintextSize is the number of bytes in an unencrypted server
// nonce.
static const size_t kServerNoncePlaintextSize =
    4 /* timestamp */ + 20 /* random bytes */;

std::string QuicCryptoServerConfig::NewServerNonce(QuicRandom* rand,
                                                   QuicWallTime now) const {
  const uint32_t timestamp = static_cast<uint32_t>(now.ToUNIXSeconds());

  uint8_t server_nonce[kServerNoncePlaintextSize];
  static_assert(sizeof(server_nonce) > sizeof(timestamp), "nonce too small");
  server_nonce[0] = static_cast<uint8_t>(timestamp >> 24);
  server_nonce[1] = static_cast<uint8_t>(timestamp >> 16);
  server_nonce[2] = static_cast<uint8_t>(timestamp >> 8);
  server_nonce[3] = static_cast<uint8_t>(timestamp);
  rand->RandBytes(&server_nonce[sizeof(timestamp)],
                  sizeof(server_nonce) - sizeof(timestamp));

  return server_nonce_boxer_.Box(
      rand, QuicStringPiece(reinterpret_cast<char*>(server_nonce),
                            sizeof(server_nonce)));
}

bool QuicCryptoServerConfig::ValidateExpectedLeafCertificate(
    const CryptoHandshakeMessage& client_hello,
    const std::vector<std::string>& certs) const {
  if (certs.empty()) {
    return false;
  }

  uint64_t hash_from_client;
  if (client_hello.GetUint64(kXLCT, &hash_from_client) != QUIC_NO_ERROR) {
    return false;
  }
  return CryptoUtils::ComputeLeafCertHash(certs.at(0)) == hash_from_client;
}

bool QuicCryptoServerConfig::IsNextConfigReady(QuicWallTime now) const {
  return !next_config_promotion_time_.IsZero() &&
         !next_config_promotion_time_.IsAfter(now);
}

QuicCryptoServerConfig::Config::Config()
    : channel_id_enabled(false),
      is_primary(false),
      primary_time(QuicWallTime::Zero()),
      expiry_time(QuicWallTime::Zero()),
      priority(0),
      source_address_token_boxer(nullptr) {}

QuicCryptoServerConfig::Config::~Config() {}

QuicSignedServerConfig::QuicSignedServerConfig() {}
QuicSignedServerConfig::~QuicSignedServerConfig() {}

}  // namespace quic
