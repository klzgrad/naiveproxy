// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_crypto_server_config_peer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/test_tools/mock_clock.h"
#include "quiche/quic/test_tools/mock_random.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

quiche::QuicheReferenceCountedPointer<QuicCryptoServerConfig::Config>
QuicCryptoServerConfigPeer::GetPrimaryConfig() {
  absl::ReaderMutexLock locked(&server_config_->configs_lock_);
  return quiche::QuicheReferenceCountedPointer<QuicCryptoServerConfig::Config>(
      server_config_->primary_config_);
}

quiche::QuicheReferenceCountedPointer<QuicCryptoServerConfig::Config>
QuicCryptoServerConfigPeer::GetConfig(std::string config_id) {
  absl::ReaderMutexLock locked(&server_config_->configs_lock_);
  if (config_id == "<primary>") {
    return quiche::QuicheReferenceCountedPointer<
        QuicCryptoServerConfig::Config>(server_config_->primary_config_);
  } else {
    return server_config_->GetConfigWithScid(config_id);
  }
}

ProofSource* QuicCryptoServerConfigPeer::GetProofSource() const {
  return server_config_->proof_source_.get();
}

void QuicCryptoServerConfigPeer::ResetProofSource(
    std::unique_ptr<ProofSource> proof_source) {
  server_config_->proof_source_ = std::move(proof_source);
}

std::string QuicCryptoServerConfigPeer::NewSourceAddressToken(
    std::string config_id, SourceAddressTokens previous_tokens,
    const QuicIpAddress& ip, QuicRandom* rand, QuicWallTime now,
    CachedNetworkParameters* cached_network_params) {
  return server_config_->NewSourceAddressToken(
      *GetConfig(config_id)->source_address_token_boxer, previous_tokens, ip,
      rand, now, cached_network_params);
}

HandshakeFailureReason QuicCryptoServerConfigPeer::ValidateSourceAddressTokens(
    std::string config_id, absl::string_view srct, const QuicIpAddress& ip,
    QuicWallTime now, CachedNetworkParameters* cached_network_params) {
  SourceAddressTokens tokens;
  HandshakeFailureReason reason = server_config_->ParseSourceAddressToken(
      *GetConfig(config_id)->source_address_token_boxer, srct, tokens);
  if (reason != HANDSHAKE_OK) {
    return reason;
  }

  return server_config_->ValidateSourceAddressTokens(tokens, ip, now,
                                                     cached_network_params);
}

HandshakeFailureReason
QuicCryptoServerConfigPeer::ValidateSingleSourceAddressToken(
    absl::string_view token, const QuicIpAddress& ip, QuicWallTime now) {
  SourceAddressTokens tokens;
  HandshakeFailureReason parse_status = server_config_->ParseSourceAddressToken(
      *GetPrimaryConfig()->source_address_token_boxer, token, tokens);
  if (HANDSHAKE_OK != parse_status) {
    return parse_status;
  }
  EXPECT_EQ(1, tokens.tokens_size());
  return server_config_->ValidateSingleSourceAddressToken(tokens.tokens(0), ip,
                                                          now);
}

void QuicCryptoServerConfigPeer::CheckConfigs(
    std::vector<std::pair<std::string, bool>> expected_ids_and_status) {
  absl::ReaderMutexLock locked(&server_config_->configs_lock_);

  ASSERT_EQ(expected_ids_and_status.size(), server_config_->configs_.size())
      << ConfigsDebug();

  for (const std::pair<const ServerConfigID,
                       quiche::QuicheReferenceCountedPointer<
                           QuicCryptoServerConfig::Config>>& i :
       server_config_->configs_) {
    bool found = false;
    for (std::pair<ServerConfigID, bool>& j : expected_ids_and_status) {
      if (i.first == j.first && i.second->is_primary == j.second) {
        found = true;
        j.first.clear();
        break;
      }
    }

    ASSERT_TRUE(found) << "Failed to find match for " << i.first
                       << " in configs:\n"
                       << ConfigsDebug();
  }
}

// ConfigsDebug returns a std::string that contains debugging information about
// the set of Configs loaded in |server_config_| and their status.
std::string QuicCryptoServerConfigPeer::ConfigsDebug() {
  if (server_config_->configs_.empty()) {
    return "No Configs in QuicCryptoServerConfig";
  }

  std::string s;

  for (const auto& i : server_config_->configs_) {
    const quiche::QuicheReferenceCountedPointer<QuicCryptoServerConfig::Config>
        config = i.second;
    if (config->is_primary) {
      s += "(primary) ";
    } else {
      s += "          ";
    }
    s += config->id;
    s += "\n";
  }

  return s;
}

void QuicCryptoServerConfigPeer::SelectNewPrimaryConfig(int seconds) {
  absl::WriterMutexLock locked(&server_config_->configs_lock_);
  server_config_->SelectNewPrimaryConfig(
      QuicWallTime::FromUNIXSeconds(seconds));
}

std::string QuicCryptoServerConfigPeer::CompressChain(
    QuicCompressedCertsCache* compressed_certs_cache,
    const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
    const std::string& client_cached_cert_hashes) {
  return QuicCryptoServerConfig::CompressChain(compressed_certs_cache, chain,
                                               client_cached_cert_hashes);
}

uint32_t QuicCryptoServerConfigPeer::source_address_token_future_secs() {
  return server_config_->source_address_token_future_secs_;
}

uint32_t QuicCryptoServerConfigPeer::source_address_token_lifetime_secs() {
  return server_config_->source_address_token_lifetime_secs_;
}

}  // namespace test
}  // namespace quic
