// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_client_config.h"

#include <algorithm>
#include <memory>
#include <string>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/cert_compressor.h"
#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/common_cert_set.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/crypto/curve25519_key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/p256_key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/proof_verifier.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/crypto/tls_client_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_client_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_hostname_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

namespace {

// Tracks the reason (the state of the server config) for sending inchoate
// ClientHello to the server.
void RecordInchoateClientHelloReason(
    QuicCryptoClientConfig::CachedState::ServerConfigState state) {
  QUIC_CLIENT_HISTOGRAM_ENUM(
      "QuicInchoateClientHelloReason", state,
      QuicCryptoClientConfig::CachedState::SERVER_CONFIG_COUNT, "");
}

// Tracks the state of the QUIC server information loaded from the disk cache.
void RecordDiskCacheServerConfigState(
    QuicCryptoClientConfig::CachedState::ServerConfigState state) {
  QUIC_CLIENT_HISTOGRAM_ENUM(
      "QuicServerInfo.DiskCacheState", state,
      QuicCryptoClientConfig::CachedState::SERVER_CONFIG_COUNT, "");
}

}  // namespace

QuicCryptoClientConfig::QuicCryptoClientConfig(
    std::unique_ptr<ProofVerifier> proof_verifier)
    : proof_verifier_(std::move(proof_verifier)),
      ssl_ctx_(TlsClientConnection::CreateSslCtx()) {
  DCHECK(proof_verifier_.get());
  SetDefaults();
}

QuicCryptoClientConfig::~QuicCryptoClientConfig() {}

QuicCryptoClientConfig::CachedState::CachedState()
    : server_config_valid_(false),
      expiration_time_(QuicWallTime::Zero()),
      generation_counter_(0) {}

QuicCryptoClientConfig::CachedState::~CachedState() {}

bool QuicCryptoClientConfig::CachedState::IsComplete(QuicWallTime now) const {
  if (server_config_.empty()) {
    RecordInchoateClientHelloReason(SERVER_CONFIG_EMPTY);
    return false;
  }

  if (!server_config_valid_) {
    RecordInchoateClientHelloReason(SERVER_CONFIG_INVALID);
    return false;
  }

  const CryptoHandshakeMessage* scfg = GetServerConfig();
  if (!scfg) {
    // Should be impossible short of cache corruption.
    RecordInchoateClientHelloReason(SERVER_CONFIG_CORRUPTED);
    DCHECK(false);
    return false;
  }

  if (now.IsBefore(expiration_time_)) {
    return true;
  }

  QUIC_CLIENT_HISTOGRAM_TIMES(
      "QuicClientHelloServerConfig.InvalidDuration",
      QuicTime::Delta::FromSeconds(now.ToUNIXSeconds() -
                                   expiration_time_.ToUNIXSeconds()),
      QuicTime::Delta::FromSeconds(60),              // 1 min.
      QuicTime::Delta::FromSeconds(20 * 24 * 3600),  // 20 days.
      50, "");
  RecordInchoateClientHelloReason(SERVER_CONFIG_EXPIRED);
  return false;
}

bool QuicCryptoClientConfig::CachedState::IsEmpty() const {
  return server_config_.empty();
}

const CryptoHandshakeMessage*
QuicCryptoClientConfig::CachedState::GetServerConfig() const {
  if (server_config_.empty()) {
    return nullptr;
  }

  if (!scfg_.get()) {
    scfg_ = CryptoFramer::ParseMessage(server_config_);
    DCHECK(scfg_.get());
  }
  return scfg_.get();
}

void QuicCryptoClientConfig::CachedState::add_server_designated_connection_id(
    QuicConnectionId connection_id) {
  server_designated_connection_ids_.push(connection_id);
}

bool QuicCryptoClientConfig::CachedState::has_server_designated_connection_id()
    const {
  return !server_designated_connection_ids_.empty();
}

void QuicCryptoClientConfig::CachedState::add_server_nonce(
    const std::string& server_nonce) {
  server_nonces_.push(server_nonce);
}

bool QuicCryptoClientConfig::CachedState::has_server_nonce() const {
  return !server_nonces_.empty();
}

QuicCryptoClientConfig::CachedState::ServerConfigState
QuicCryptoClientConfig::CachedState::SetServerConfig(
    QuicStringPiece server_config,
    QuicWallTime now,
    QuicWallTime expiry_time,
    std::string* error_details) {
  const bool matches_existing = server_config == server_config_;

  // Even if the new server config matches the existing one, we still wish to
  // reject it if it has expired.
  std::unique_ptr<CryptoHandshakeMessage> new_scfg_storage;
  const CryptoHandshakeMessage* new_scfg;

  if (!matches_existing) {
    new_scfg_storage = CryptoFramer::ParseMessage(server_config);
    new_scfg = new_scfg_storage.get();
  } else {
    new_scfg = GetServerConfig();
  }

  if (!new_scfg) {
    *error_details = "SCFG invalid";
    return SERVER_CONFIG_INVALID;
  }

  if (expiry_time.IsZero()) {
    uint64_t expiry_seconds;
    if (new_scfg->GetUint64(kEXPY, &expiry_seconds) != QUIC_NO_ERROR) {
      *error_details = "SCFG missing EXPY";
      return SERVER_CONFIG_INVALID_EXPIRY;
    }
    expiration_time_ = QuicWallTime::FromUNIXSeconds(expiry_seconds);
  } else {
    expiration_time_ = expiry_time;
  }

  if (now.IsAfter(expiration_time_)) {
    *error_details = "SCFG has expired";
    return SERVER_CONFIG_EXPIRED;
  }

  if (!matches_existing) {
    server_config_ = std::string(server_config);
    SetProofInvalid();
    scfg_ = std::move(new_scfg_storage);
  }
  return SERVER_CONFIG_VALID;
}

void QuicCryptoClientConfig::CachedState::InvalidateServerConfig() {
  server_config_.clear();
  scfg_.reset();
  SetProofInvalid();
  QuicQueue<QuicConnectionId> empty_queue;
  using std::swap;
  swap(server_designated_connection_ids_, empty_queue);
}

void QuicCryptoClientConfig::CachedState::SetProof(
    const std::vector<std::string>& certs,
    QuicStringPiece cert_sct,
    QuicStringPiece chlo_hash,
    QuicStringPiece signature) {
  bool has_changed = signature != server_config_sig_ ||
                     chlo_hash != chlo_hash_ || certs_.size() != certs.size();

  if (!has_changed) {
    for (size_t i = 0; i < certs_.size(); i++) {
      if (certs_[i] != certs[i]) {
        has_changed = true;
        break;
      }
    }
  }

  if (!has_changed) {
    return;
  }

  // If the proof has changed then it needs to be revalidated.
  SetProofInvalid();
  certs_ = certs;
  cert_sct_ = std::string(cert_sct);
  chlo_hash_ = std::string(chlo_hash);
  server_config_sig_ = std::string(signature);
}

void QuicCryptoClientConfig::CachedState::Clear() {
  server_config_.clear();
  source_address_token_.clear();
  certs_.clear();
  cert_sct_.clear();
  chlo_hash_.clear();
  server_config_sig_.clear();
  server_config_valid_ = false;
  proof_verify_details_.reset();
  scfg_.reset();
  ++generation_counter_;
  QuicQueue<QuicConnectionId> empty_queue;
  using std::swap;
  swap(server_designated_connection_ids_, empty_queue);
}

void QuicCryptoClientConfig::CachedState::ClearProof() {
  SetProofInvalid();
  certs_.clear();
  cert_sct_.clear();
  chlo_hash_.clear();
  server_config_sig_.clear();
}

void QuicCryptoClientConfig::CachedState::SetProofValid() {
  server_config_valid_ = true;
}

void QuicCryptoClientConfig::CachedState::SetProofInvalid() {
  server_config_valid_ = false;
  ++generation_counter_;
}

bool QuicCryptoClientConfig::CachedState::Initialize(
    QuicStringPiece server_config,
    QuicStringPiece source_address_token,
    const std::vector<std::string>& certs,
    const std::string& cert_sct,
    QuicStringPiece chlo_hash,
    QuicStringPiece signature,
    QuicWallTime now,
    QuicWallTime expiration_time) {
  DCHECK(server_config_.empty());

  if (server_config.empty()) {
    RecordDiskCacheServerConfigState(SERVER_CONFIG_EMPTY);
    return false;
  }

  std::string error_details;
  ServerConfigState state =
      SetServerConfig(server_config, now, expiration_time, &error_details);
  RecordDiskCacheServerConfigState(state);
  if (state != SERVER_CONFIG_VALID) {
    QUIC_DVLOG(1) << "SetServerConfig failed with " << error_details;
    return false;
  }

  chlo_hash_.assign(chlo_hash.data(), chlo_hash.size());
  server_config_sig_.assign(signature.data(), signature.size());
  source_address_token_.assign(source_address_token.data(),
                               source_address_token.size());
  certs_ = certs;
  cert_sct_ = cert_sct;
  return true;
}

const std::string& QuicCryptoClientConfig::CachedState::server_config() const {
  return server_config_;
}

const std::string& QuicCryptoClientConfig::CachedState::source_address_token()
    const {
  return source_address_token_;
}

const std::vector<std::string>& QuicCryptoClientConfig::CachedState::certs()
    const {
  return certs_;
}

const std::string& QuicCryptoClientConfig::CachedState::cert_sct() const {
  return cert_sct_;
}

const std::string& QuicCryptoClientConfig::CachedState::chlo_hash() const {
  return chlo_hash_;
}

const std::string& QuicCryptoClientConfig::CachedState::signature() const {
  return server_config_sig_;
}

bool QuicCryptoClientConfig::CachedState::proof_valid() const {
  return server_config_valid_;
}

uint64_t QuicCryptoClientConfig::CachedState::generation_counter() const {
  return generation_counter_;
}

const ProofVerifyDetails*
QuicCryptoClientConfig::CachedState::proof_verify_details() const {
  return proof_verify_details_.get();
}

void QuicCryptoClientConfig::CachedState::set_source_address_token(
    QuicStringPiece token) {
  source_address_token_ = std::string(token);
}

void QuicCryptoClientConfig::CachedState::set_cert_sct(
    QuicStringPiece cert_sct) {
  cert_sct_ = std::string(cert_sct);
}

void QuicCryptoClientConfig::CachedState::SetProofVerifyDetails(
    ProofVerifyDetails* details) {
  proof_verify_details_.reset(details);
}

void QuicCryptoClientConfig::CachedState::InitializeFrom(
    const QuicCryptoClientConfig::CachedState& other) {
  DCHECK(server_config_.empty());
  DCHECK(!server_config_valid_);
  server_config_ = other.server_config_;
  source_address_token_ = other.source_address_token_;
  certs_ = other.certs_;
  cert_sct_ = other.cert_sct_;
  chlo_hash_ = other.chlo_hash_;
  server_config_sig_ = other.server_config_sig_;
  server_config_valid_ = other.server_config_valid_;
  server_designated_connection_ids_ = other.server_designated_connection_ids_;
  expiration_time_ = other.expiration_time_;
  if (other.proof_verify_details_ != nullptr) {
    proof_verify_details_.reset(other.proof_verify_details_->Clone());
  }
  ++generation_counter_;
}

QuicConnectionId
QuicCryptoClientConfig::CachedState::GetNextServerDesignatedConnectionId() {
  if (server_designated_connection_ids_.empty()) {
    QUIC_BUG
        << "Attempting to consume a connection id that was never designated.";
    return EmptyQuicConnectionId();
  }
  const QuicConnectionId next_id = server_designated_connection_ids_.front();
  server_designated_connection_ids_.pop();
  return next_id;
}

std::string QuicCryptoClientConfig::CachedState::GetNextServerNonce() {
  if (server_nonces_.empty()) {
    QUIC_BUG
        << "Attempting to consume a server nonce that was never designated.";
    return "";
  }
  const std::string server_nonce = server_nonces_.front();
  server_nonces_.pop();
  return server_nonce;
}

void QuicCryptoClientConfig::SetDefaults() {
  // Key exchange methods.
  kexs = {kC255, kP256};

  // Authenticated encryption algorithms. Prefer AES-GCM if hardware-supported
  // fast implementation is available.
  if (EVP_has_aes_hardware() == 1) {
    aead = {kAESG, kCC20};
  } else {
    aead = {kCC20, kAESG};
  }
}

QuicCryptoClientConfig::CachedState* QuicCryptoClientConfig::LookupOrCreate(
    const QuicServerId& server_id) {
  auto it = cached_states_.find(server_id);
  if (it != cached_states_.end()) {
    return it->second.get();
  }

  CachedState* cached = new CachedState;
  cached_states_.insert(std::make_pair(server_id, QuicWrapUnique(cached)));
  bool cache_populated = PopulateFromCanonicalConfig(server_id, cached);
  QUIC_CLIENT_HISTOGRAM_BOOL(
      "QuicCryptoClientConfig.PopulatedFromCanonicalConfig", cache_populated,
      "");
  return cached;
}

void QuicCryptoClientConfig::ClearCachedStates(const ServerIdFilter& filter) {
  for (auto it = cached_states_.begin(); it != cached_states_.end(); ++it) {
    if (filter.Matches(it->first))
      it->second->Clear();
  }
}

void QuicCryptoClientConfig::FillInchoateClientHello(
    const QuicServerId& server_id,
    const ParsedQuicVersion preferred_version,
    const CachedState* cached,
    QuicRandom* rand,
    bool demand_x509_proof,
    QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
    CryptoHandshakeMessage* out) const {
  out->set_tag(kCHLO);
  // TODO(rch): Remove this when we remove quic_use_chlo_packet_size flag.
  if (pad_inchoate_hello_) {
    out->set_minimum_size(kClientHelloMinimumSize);
  } else {
    out->set_minimum_size(1);
  }

  // Server name indication. We only send SNI if it's a valid domain name, as
  // per the spec.
  if (QuicHostnameUtils::IsValidSNI(server_id.host())) {
    out->SetStringPiece(kSNI, server_id.host());
  }
  out->SetVersion(kVER, preferred_version);

  if (!user_agent_id_.empty()) {
    out->SetStringPiece(kUAID, user_agent_id_);
  }

  if (!alpn_.empty()) {
    out->SetStringPiece(kALPN, alpn_);
  }

  // Even though this is an inchoate CHLO, send the SCID so that
  // the STK can be validated by the server.
  const CryptoHandshakeMessage* scfg = cached->GetServerConfig();
  if (scfg != nullptr) {
    QuicStringPiece scid;
    if (scfg->GetStringPiece(kSCID, &scid)) {
      out->SetStringPiece(kSCID, scid);
    }
  }

  if (!cached->source_address_token().empty()) {
    out->SetStringPiece(kSourceAddressTokenTag, cached->source_address_token());
  }

  if (!demand_x509_proof) {
    return;
  }

  char proof_nonce[32];
  rand->RandBytes(proof_nonce, QUIC_ARRAYSIZE(proof_nonce));
  out->SetStringPiece(
      kNONP, QuicStringPiece(proof_nonce, QUIC_ARRAYSIZE(proof_nonce)));

  out->SetVector(kPDMD, QuicTagVector{kX509});

  if (common_cert_sets) {
    out->SetStringPiece(kCCS, common_cert_sets->GetCommonHashes());
  }

  out->SetStringPiece(kCertificateSCTTag, "");

  const std::vector<std::string>& certs = cached->certs();
  // We save |certs| in the QuicCryptoNegotiatedParameters so that, if the
  // client config is being used for multiple connections, another connection
  // doesn't update the cached certificates and cause us to be unable to
  // process the server's compressed certificate chain.
  out_params->cached_certs = certs;
  if (!certs.empty()) {
    std::vector<uint64_t> hashes;
    hashes.reserve(certs.size());
    for (auto i = certs.begin(); i != certs.end(); ++i) {
      hashes.push_back(QuicUtils::FNV1a_64_Hash(*i));
    }
    out->SetVector(kCCRT, hashes);
  }
}

QuicErrorCode QuicCryptoClientConfig::FillClientHello(
    const QuicServerId& server_id,
    QuicConnectionId connection_id,
    const ParsedQuicVersion preferred_version,
    const ParsedQuicVersion actual_version,
    const CachedState* cached,
    QuicWallTime now,
    QuicRandom* rand,
    QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
    CryptoHandshakeMessage* out,
    std::string* error_details) const {
  DCHECK(error_details != nullptr);
  QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(
      connection_id, preferred_version.transport_version))
      << "FillClientHello: attempted to use connection ID " << connection_id
      << " which is invalid with version "
      << QuicVersionToString(preferred_version.transport_version);

  FillInchoateClientHello(server_id, preferred_version, cached, rand,
                          /* demand_x509_proof= */ true, out_params, out);

  if (pad_full_hello_) {
    out->set_minimum_size(kClientHelloMinimumSize);
  } else {
    out->set_minimum_size(1);
  }

  const CryptoHandshakeMessage* scfg = cached->GetServerConfig();
  if (!scfg) {
    // This should never happen as our caller should have checked
    // cached->IsComplete() before calling this function.
    *error_details = "Handshake not ready";
    return QUIC_CRYPTO_INTERNAL_ERROR;
  }

  QuicStringPiece scid;
  if (!scfg->GetStringPiece(kSCID, &scid)) {
    *error_details = "SCFG missing SCID";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }
  out->SetStringPiece(kSCID, scid);

  out->SetStringPiece(kCertificateSCTTag, "");

  QuicTagVector their_aeads;
  QuicTagVector their_key_exchanges;
  if (scfg->GetTaglist(kAEAD, &their_aeads) != QUIC_NO_ERROR ||
      scfg->GetTaglist(kKEXS, &their_key_exchanges) != QUIC_NO_ERROR) {
    *error_details = "Missing AEAD or KEXS";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  // AEAD: the work loads on the client and server are symmetric. Since the
  // client is more likely to be CPU-constrained, break the tie by favoring
  // the client's preference.
  // Key exchange: the client does more work than the server, so favor the
  // client's preference.
  size_t key_exchange_index;
  if (!FindMutualQuicTag(aead, their_aeads, &out_params->aead, nullptr) ||
      !FindMutualQuicTag(kexs, their_key_exchanges, &out_params->key_exchange,
                         &key_exchange_index)) {
    *error_details = "Unsupported AEAD or KEXS";
    return QUIC_CRYPTO_NO_SUPPORT;
  }
  out->SetVector(kAEAD, QuicTagVector{out_params->aead});
  out->SetVector(kKEXS, QuicTagVector{out_params->key_exchange});

  QuicStringPiece public_value;
  if (scfg->GetNthValue24(kPUBS, key_exchange_index, &public_value) !=
      QUIC_NO_ERROR) {
    *error_details = "Missing public value";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  QuicStringPiece orbit;
  if (!scfg->GetStringPiece(kORBT, &orbit) || orbit.size() != kOrbitSize) {
    *error_details = "SCFG missing OBIT";
    return QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND;
  }

  CryptoUtils::GenerateNonce(now, rand, orbit, &out_params->client_nonce);
  out->SetStringPiece(kNONC, out_params->client_nonce);
  if (!out_params->server_nonce.empty()) {
    out->SetStringPiece(kServerNonceTag, out_params->server_nonce);
  }

  switch (out_params->key_exchange) {
    case kC255:
      out_params->client_key_exchange = Curve25519KeyExchange::New(
          Curve25519KeyExchange::NewPrivateKey(rand));
      break;
    case kP256:
      out_params->client_key_exchange =
          P256KeyExchange::New(P256KeyExchange::NewPrivateKey());
      break;
    default:
      DCHECK(false);
      *error_details = "Configured to support an unknown key exchange";
      return QUIC_CRYPTO_INTERNAL_ERROR;
  }

  if (!out_params->client_key_exchange->CalculateSharedKeySync(
          public_value, &out_params->initial_premaster_secret)) {
    *error_details = "Key exchange failure";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }
  out->SetStringPiece(kPUBS, out_params->client_key_exchange->public_value());

  const std::vector<std::string>& certs = cached->certs();
  if (certs.empty()) {
    *error_details = "No certs to calculate XLCT";
    return QUIC_CRYPTO_INTERNAL_ERROR;
  }
  out->SetValue(kXLCT, CryptoUtils::ComputeLeafCertHash(certs[0]));

  // Derive the symmetric keys and set up the encrypters and decrypters.
  // Set the following members of out_params:
  //   out_params->hkdf_input_suffix
  //   out_params->initial_crypters
  out_params->hkdf_input_suffix.clear();
  out_params->hkdf_input_suffix.append(connection_id.data(),
                                       connection_id.length());
  const QuicData& client_hello_serialized = out->GetSerialized();
  out_params->hkdf_input_suffix.append(client_hello_serialized.data(),
                                       client_hello_serialized.length());
  out_params->hkdf_input_suffix.append(cached->server_config());
  if (certs.empty()) {
    *error_details = "No certs found to include in KDF";
    return QUIC_CRYPTO_INTERNAL_ERROR;
  }
  out_params->hkdf_input_suffix.append(certs[0]);

  std::string hkdf_input;
  const size_t label_len = strlen(QuicCryptoConfig::kInitialLabel) + 1;
  hkdf_input.reserve(label_len + out_params->hkdf_input_suffix.size());
  hkdf_input.append(QuicCryptoConfig::kInitialLabel, label_len);
  hkdf_input.append(out_params->hkdf_input_suffix);

  std::string* subkey_secret = &out_params->initial_subkey_secret;

  if (!CryptoUtils::DeriveKeys(
          actual_version, out_params->initial_premaster_secret,
          out_params->aead, out_params->client_nonce, out_params->server_nonce,
          pre_shared_key_, hkdf_input, Perspective::IS_CLIENT,
          CryptoUtils::Diversification::Pending(),
          &out_params->initial_crypters, subkey_secret)) {
    *error_details = "Symmetric key setup failed";
    return QUIC_CRYPTO_SYMMETRIC_KEY_SETUP_FAILED;
  }

  return QUIC_NO_ERROR;
}

QuicErrorCode QuicCryptoClientConfig::CacheNewServerConfig(
    const CryptoHandshakeMessage& message,
    QuicWallTime now,
    QuicTransportVersion /*version*/,
    QuicStringPiece chlo_hash,
    const std::vector<std::string>& cached_certs,
    CachedState* cached,
    std::string* error_details) {
  DCHECK(error_details != nullptr);

  QuicStringPiece scfg;
  if (!message.GetStringPiece(kSCFG, &scfg)) {
    *error_details = "Missing SCFG";
    return QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND;
  }

  QuicWallTime expiration_time = QuicWallTime::Zero();
  uint64_t expiry_seconds;
  if (message.GetUint64(kSTTL, &expiry_seconds) == QUIC_NO_ERROR) {
    // Only cache configs for a maximum of 1 week.
    expiration_time = now.Add(QuicTime::Delta::FromSeconds(
        std::min(expiry_seconds, kNumSecondsPerWeek)));
  }

  CachedState::ServerConfigState state =
      cached->SetServerConfig(scfg, now, expiration_time, error_details);
  if (state == CachedState::SERVER_CONFIG_EXPIRED) {
    return QUIC_CRYPTO_SERVER_CONFIG_EXPIRED;
  }
  // TODO(rtenneti): Return more specific error code than returning
  // QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER.
  if (state != CachedState::SERVER_CONFIG_VALID) {
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  QuicStringPiece token;
  if (message.GetStringPiece(kSourceAddressTokenTag, &token)) {
    cached->set_source_address_token(token);
  }

  QuicStringPiece proof, cert_bytes, cert_sct;
  bool has_proof = message.GetStringPiece(kPROF, &proof);
  bool has_cert = message.GetStringPiece(kCertificateTag, &cert_bytes);
  if (has_proof && has_cert) {
    std::vector<std::string> certs;
    if (!CertCompressor::DecompressChain(cert_bytes, cached_certs,
                                         common_cert_sets, &certs)) {
      *error_details = "Certificate data invalid";
      return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
    }

    message.GetStringPiece(kCertificateSCTTag, &cert_sct);
    cached->SetProof(certs, cert_sct, chlo_hash, proof);
  } else {
    // Secure QUIC: clear existing proof as we have been sent a new SCFG
    // without matching proof/certs.
    cached->ClearProof();

    if (has_proof && !has_cert) {
      *error_details = "Certificate missing";
      return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
    }

    if (!has_proof && has_cert) {
      *error_details = "Proof missing";
      return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
    }
  }

  return QUIC_NO_ERROR;
}

QuicErrorCode QuicCryptoClientConfig::ProcessRejection(
    const CryptoHandshakeMessage& rej,
    QuicWallTime now,
    const QuicTransportVersion version,
    QuicStringPiece chlo_hash,
    CachedState* cached,
    QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
    std::string* error_details) {
  DCHECK(error_details != nullptr);

  if (rej.tag() != kREJ) {
    *error_details = "Message is not REJ";
    return QUIC_CRYPTO_INTERNAL_ERROR;
  }

  QuicErrorCode error =
      CacheNewServerConfig(rej, now, version, chlo_hash,
                           out_params->cached_certs, cached, error_details);
  if (error != QUIC_NO_ERROR) {
    return error;
  }

  QuicStringPiece nonce;
  if (rej.GetStringPiece(kServerNonceTag, &nonce)) {
    out_params->server_nonce = std::string(nonce);
  }

  return QUIC_NO_ERROR;
}

QuicErrorCode QuicCryptoClientConfig::ProcessServerHello(
    const CryptoHandshakeMessage& server_hello,
    QuicConnectionId /*connection_id*/,
    ParsedQuicVersion version,
    const ParsedQuicVersionVector& negotiated_versions,
    CachedState* cached,
    QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
    std::string* error_details) {
  DCHECK(error_details != nullptr);

  QuicErrorCode valid = CryptoUtils::ValidateServerHello(
      server_hello, negotiated_versions, error_details);
  if (valid != QUIC_NO_ERROR) {
    return valid;
  }

  // Learn about updated source address tokens.
  QuicStringPiece token;
  if (server_hello.GetStringPiece(kSourceAddressTokenTag, &token)) {
    cached->set_source_address_token(token);
  }

  QuicStringPiece shlo_nonce;
  if (!server_hello.GetStringPiece(kServerNonceTag, &shlo_nonce)) {
    *error_details = "server hello missing server nonce";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  // TODO(agl):
  //   learn about updated SCFGs.

  QuicStringPiece public_value;
  if (!server_hello.GetStringPiece(kPUBS, &public_value)) {
    *error_details = "server hello missing forward secure public value";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  if (!out_params->client_key_exchange->CalculateSharedKeySync(
          public_value, &out_params->forward_secure_premaster_secret)) {
    *error_details = "Key exchange failure";
    return QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER;
  }

  std::string hkdf_input;
  const size_t label_len = strlen(QuicCryptoConfig::kForwardSecureLabel) + 1;
  hkdf_input.reserve(label_len + out_params->hkdf_input_suffix.size());
  hkdf_input.append(QuicCryptoConfig::kForwardSecureLabel, label_len);
  hkdf_input.append(out_params->hkdf_input_suffix);

  if (!CryptoUtils::DeriveKeys(
          version, out_params->forward_secure_premaster_secret,
          out_params->aead, out_params->client_nonce,
          shlo_nonce.empty() ? out_params->server_nonce : shlo_nonce,
          pre_shared_key_, hkdf_input, Perspective::IS_CLIENT,
          CryptoUtils::Diversification::Never(),
          &out_params->forward_secure_crypters, &out_params->subkey_secret)) {
    *error_details = "Symmetric key setup failed";
    return QUIC_CRYPTO_SYMMETRIC_KEY_SETUP_FAILED;
  }

  return QUIC_NO_ERROR;
}

QuicErrorCode QuicCryptoClientConfig::ProcessServerConfigUpdate(
    const CryptoHandshakeMessage& server_config_update,
    QuicWallTime now,
    const QuicTransportVersion version,
    QuicStringPiece chlo_hash,
    CachedState* cached,
    QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> out_params,
    std::string* error_details) {
  DCHECK(error_details != nullptr);

  if (server_config_update.tag() != kSCUP) {
    *error_details = "ServerConfigUpdate must have kSCUP tag.";
    return QUIC_INVALID_CRYPTO_MESSAGE_TYPE;
  }
  return CacheNewServerConfig(server_config_update, now, version, chlo_hash,
                              out_params->cached_certs, cached, error_details);
}

ProofVerifier* QuicCryptoClientConfig::proof_verifier() const {
  return proof_verifier_.get();
}

SSL_CTX* QuicCryptoClientConfig::ssl_ctx() const {
  return ssl_ctx_.get();
}

void QuicCryptoClientConfig::InitializeFrom(
    const QuicServerId& server_id,
    const QuicServerId& canonical_server_id,
    QuicCryptoClientConfig* canonical_crypto_config) {
  CachedState* canonical_cached =
      canonical_crypto_config->LookupOrCreate(canonical_server_id);
  if (!canonical_cached->proof_valid()) {
    return;
  }
  CachedState* cached = LookupOrCreate(server_id);
  cached->InitializeFrom(*canonical_cached);
}

void QuicCryptoClientConfig::AddCanonicalSuffix(const std::string& suffix) {
  canonical_suffixes_.push_back(suffix);
}

bool QuicCryptoClientConfig::PopulateFromCanonicalConfig(
    const QuicServerId& server_id,
    CachedState* server_state) {
  DCHECK(server_state->IsEmpty());
  size_t i = 0;
  for (; i < canonical_suffixes_.size(); ++i) {
    if (QuicTextUtils::EndsWithIgnoreCase(server_id.host(),
                                          canonical_suffixes_[i])) {
      break;
    }
  }
  if (i == canonical_suffixes_.size()) {
    return false;
  }

  QuicServerId suffix_server_id(canonical_suffixes_[i], server_id.port(),
                                server_id.privacy_mode_enabled());
  if (!QuicContainsKey(canonical_server_map_, suffix_server_id)) {
    // This is the first host we've seen which matches the suffix, so make it
    // canonical.
    canonical_server_map_[suffix_server_id] = server_id;
    return false;
  }

  const QuicServerId& canonical_server_id =
      canonical_server_map_[suffix_server_id];
  CachedState* canonical_state = cached_states_[canonical_server_id].get();
  if (!canonical_state->proof_valid()) {
    return false;
  }

  // Update canonical version to point at the "most recent" entry.
  canonical_server_map_[suffix_server_id] = server_id;

  server_state->InitializeFrom(*canonical_state);
  return true;
}

}  // namespace quic
