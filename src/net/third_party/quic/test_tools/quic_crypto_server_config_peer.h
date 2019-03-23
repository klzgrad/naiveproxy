// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CRYPTO_SERVER_CONFIG_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CRYPTO_SERVER_CONFIG_PEER_H_

#include "net/third_party/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {
namespace test {

// Peer for accessing otherwise private members of a QuicCryptoServerConfig.
class QuicCryptoServerConfigPeer {
 public:
  explicit QuicCryptoServerConfigPeer(QuicCryptoServerConfig* server_config)
      : server_config_(server_config) {}

  // Returns the primary config.
  QuicReferenceCountedPointer<QuicCryptoServerConfig::Config>
  GetPrimaryConfig();

  // Returns the config associated with |config_id|.
  QuicReferenceCountedPointer<QuicCryptoServerConfig::Config> GetConfig(
      QuicString config_id);

  // Returns a pointer to the ProofSource object.
  ProofSource* GetProofSource() const;

  // Reset the proof_source_ member.
  void ResetProofSource(std::unique_ptr<ProofSource> proof_source);

  // Generates a new valid source address token.
  QuicString NewSourceAddressToken(
      QuicString config_id,
      SourceAddressTokens previous_tokens,
      const QuicIpAddress& ip,
      QuicRandom* rand,
      QuicWallTime now,
      CachedNetworkParameters* cached_network_params);

  // Attempts to validate the tokens in |tokens|.
  HandshakeFailureReason ValidateSourceAddressTokens(
      QuicString config_id,
      QuicStringPiece tokens,
      const QuicIpAddress& ip,
      QuicWallTime now,
      CachedNetworkParameters* cached_network_params);

  // Attempts to validate the single source address token in |token|.
  HandshakeFailureReason ValidateSingleSourceAddressToken(
      QuicStringPiece token,
      const QuicIpAddress& ip,
      QuicWallTime now);

  // CheckConfigs compares the state of the Configs in |server_config_| to the
  // description given as arguments.
  // The first of each pair is the server config ID of a Config. The second is a
  // boolean describing whether the config is the primary. For example:
  //   CheckConfigs(std::vector<std::pair<ServerConfigID, bool>>());  // checks
  //   that no Configs are loaded.
  //
  //   // Checks that exactly three Configs are loaded with the given IDs and
  //   // status.
  //   CheckConfigs(
  //     {{"id1", false},
  //      {"id2", true},
  //      {"id3", false}});
  void CheckConfigs(
      std::vector<std::pair<ServerConfigID, bool>> expected_ids_and_status);

  // ConfigsDebug returns a QuicString that contains debugging information about
  // the set of Configs loaded in |server_config_| and their status.
  QuicString ConfigsDebug()
      SHARED_LOCKS_REQUIRED(server_config_->configs_lock_);

  void SelectNewPrimaryConfig(int seconds);

  static QuicString CompressChain(
      QuicCompressedCertsCache* compressed_certs_cache,
      const QuicReferenceCountedPointer<ProofSource::Chain>& chain,
      const QuicString& client_common_set_hashes,
      const QuicString& client_cached_cert_hashes,
      const CommonCertSets* common_sets);

  uint32_t source_address_token_future_secs();

  uint32_t source_address_token_lifetime_secs();

 private:
  QuicCryptoServerConfig* server_config_;
};

}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CRYPTO_SERVER_CONFIG_PEER_H_
