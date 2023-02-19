// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/load_balancer/load_balancer_encoder.h"

#include "absl/numeric/int128.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_packet_number.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/load_balancer/load_balancer_config.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

namespace {

// Returns the number of nonces given a certain |nonce_len|.
absl::uint128 NumberOfNonces(uint8_t nonce_len) {
  return (static_cast<absl::uint128>(1) << (nonce_len * 8));
}

// Writes the |size| least significant bytes from |in| to |out| in host byte
// order. Returns false if |out| does not have enough space.
bool WriteUint128(const absl::uint128 in, uint8_t size, QuicDataWriter &out) {
  if (out.remaining() < size) {
    QUIC_BUG(quic_bug_435375038_05)
        << "Call to WriteUint128() does not have enough space in |out|";
    return false;
  }
  uint64_t num64 = absl::Uint128Low64(in);
  if (size <= sizeof(num64)) {
    out.WriteBytes(&num64, size);
  } else {
    out.WriteBytes(&num64, sizeof(num64));
    num64 = absl::Uint128High64(in);
    out.WriteBytes(&num64, size - sizeof(num64));
  }
  return true;
}

}  // namespace

absl::optional<LoadBalancerEncoder> LoadBalancerEncoder::Create(
    QuicRandom &random, LoadBalancerEncoderVisitorInterface *const visitor,
    const bool len_self_encoded, const uint8_t unroutable_connection_id_len) {
  if (unroutable_connection_id_len == 0 ||
      unroutable_connection_id_len >
          kQuicMaxConnectionIdWithLengthPrefixLength) {
    QUIC_BUG(quic_bug_435375038_01)
        << "Invalid unroutable_connection_id_len = "
        << static_cast<int>(unroutable_connection_id_len);
    return absl::optional<LoadBalancerEncoder>();
  }
  return LoadBalancerEncoder(random, visitor, len_self_encoded,
                             unroutable_connection_id_len);
}

bool LoadBalancerEncoder::UpdateConfig(const LoadBalancerConfig &config,
                                       const LoadBalancerServerId server_id) {
  if (config_.has_value() && config_->config_id() == config.config_id()) {
    QUIC_BUG(quic_bug_435375038_02)
        << "Attempting to change config with same ID";
    return false;
  }
  if (server_id.length() != config.server_id_len()) {
    QUIC_BUG(quic_bug_435375038_03)
        << "Server ID length " << static_cast<int>(server_id.length())
        << " does not match configured value of "
        << static_cast<int>(config.server_id_len());
    return false;
  }
  if (visitor_ != nullptr) {
    if (config_.has_value()) {
      visitor_->OnConfigChanged(config_->config_id(), config.config_id());
    } else {
      visitor_->OnConfigAdded(config.config_id());
    }
  }
  config_ = config;
  server_id_ = server_id;

  seed_ = absl::MakeUint128(random_.RandUint64(), random_.RandUint64()) %
          NumberOfNonces(config.nonce_len());
  num_nonces_left_ = NumberOfNonces(config.nonce_len());
  connection_id_lengths_[config.config_id()] = config.total_len();
  return true;
}

void LoadBalancerEncoder::DeleteConfig() {
  if (visitor_ != nullptr && config_.has_value()) {
    visitor_->OnConfigDeleted(config_->config_id());
  }
  config_.reset();
  server_id_.reset();
  num_nonces_left_ = 0;
}

QuicConnectionId LoadBalancerEncoder::GenerateConnectionId() {
  uint8_t config_id = config_.has_value() ? config_->config_id()
                                          : kLoadBalancerUnroutableConfigId;
  uint8_t shifted_config_id = config_id << 6;
  uint8_t length = connection_id_lengths_[config_id];
  if (config_.has_value() != server_id_.has_value()) {
    QUIC_BUG(quic_bug_435375038_04)
        << "Existence of config and server_id are out of sync";
    return QuicConnectionId();
  }
  uint8_t first_byte;
  // first byte
  if (len_self_encoded_) {
    first_byte = shifted_config_id | (length - 1);
  } else {
    random_.RandBytes(static_cast<void *>(&first_byte), 1);
    first_byte = shifted_config_id | (first_byte & kLoadBalancerLengthMask);
  }
  if (!config_.has_value()) {
    return MakeUnroutableConnectionId(first_byte);
  }
  QuicConnectionId id;
  id.set_length(length);
  QuicDataWriter writer(length, id.mutable_data(), quiche::HOST_BYTE_ORDER);
  writer.WriteUInt8(first_byte);
  absl::uint128 next_nonce =
      (seed_ + num_nonces_left_--) % NumberOfNonces(config_->nonce_len());
  writer.WriteBytes(server_id_->data().data(), server_id_->length());
  if (!WriteUint128(next_nonce, config_->nonce_len(), writer)) {
    return QuicConnectionId();
  }
  uint8_t *block_start = reinterpret_cast<uint8_t *>(writer.data() + 1);
  if (!config_->IsEncrypted()) {
    // Fill the nonce field with a hash of the Connection ID to avoid the nonce
    // visibly increasing by one. This would allow observers to correlate
    // connection IDs as being sequential and likely from the same connection,
    // not just the same server.
    absl::uint128 nonce_hash =
        QuicUtils::FNV1a_128_Hash(absl::string_view(writer.data(), length));
    QuicDataWriter rewriter(config_->nonce_len(),
                            id.mutable_data() + config_->server_id_len() + 1,
                            quiche::HOST_BYTE_ORDER);
    if (!WriteUint128(nonce_hash, config_->nonce_len(), rewriter)) {
      return QuicConnectionId();
    }
  } else if (config_->plaintext_len() == kLoadBalancerBlockSize) {
    // Use one encryption pass.
    if (!config_->BlockEncrypt(block_start, block_start)) {
      QUIC_LOG(ERROR) << "Block encryption failed";
      return QuicConnectionId();
    }
  } else {
    for (uint8_t i = 1; i <= kNumLoadBalancerCryptoPasses; i++) {
      if (!config_->EncryptionPass(absl::Span<uint8_t>(block_start, length - 1),
                                   i)) {
        QUIC_LOG(ERROR) << "Block encryption failed";
        return QuicConnectionId();
      }
    }
  }
  if (num_nonces_left_ == 0) {
    DeleteConfig();
  }
  return id;
}

absl::optional<QuicConnectionId> LoadBalancerEncoder::GenerateNextConnectionId(
    [[maybe_unused]] const QuicConnectionId &original) {
  // Do not allow new connection IDs if linkable.
  return (IsEncoding() && !IsEncrypted()) ? absl::optional<QuicConnectionId>()
                                          : GenerateConnectionId();
}

absl::optional<QuicConnectionId> LoadBalancerEncoder::MaybeReplaceConnectionId(
    const QuicConnectionId &original, const ParsedQuicVersion &version) {
  // Pre-IETF versions of QUIC can respond poorly to new connection IDs issued
  // during the handshake.
  uint8_t needed_length = config_.has_value()
                              ? config_->total_len()
                              : connection_id_lengths_[kNumLoadBalancerConfigs];
  return (!version.HasIetfQuicFrames() && original.length() == needed_length)
             ? absl::optional<QuicConnectionId>()
             : GenerateConnectionId();
}

uint8_t LoadBalancerEncoder::ConnectionIdLength(uint8_t first_byte) const {
  if (len_self_encoded()) {
    return (first_byte &= kLoadBalancerLengthMask) + 1;
  }
  return connection_id_lengths_[first_byte >> 6];
}

QuicConnectionId LoadBalancerEncoder::MakeUnroutableConnectionId(
    uint8_t first_byte) {
  QuicConnectionId id;
  id.set_length(connection_id_lengths_[kLoadBalancerUnroutableConfigId]);
  id.mutable_data()[0] = first_byte;
  random_.RandBytes(&id.mutable_data()[1], connection_id_lengths_[3] - 1);
  return id;
}

}  // namespace quic
