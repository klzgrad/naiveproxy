// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_ENCODER_H_
#define QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_ENCODER_H_

#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/load_balancer/load_balancer_config.h"
#include "quiche/quic/load_balancer/load_balancer_server_id.h"

namespace quic {

namespace test {
class LoadBalancerEncoderPeer;
}

// Default length of a 4-tuple connection ID.
inline constexpr uint8_t kLoadBalancerUnroutableLen = 8;
// When the encoder is self-encoding the connection ID length, these are the
// bits of the first byte that do so.
constexpr uint8_t kLoadBalancerLengthMask = 0x3f;
// The bits of the connection ID first byte that encode the config ID.
constexpr uint8_t kLoadBalancerConfigIdMask = 0xc0;
// The config ID that means the connection ID does not contain routing
// information.
constexpr uint8_t kLoadBalancerUnroutableConfigId = kNumLoadBalancerConfigs;
// The bits of the connection ID first byte that correspond to a connection ID
// that does not contain routing information.
constexpr uint8_t kLoadBalancerUnroutablePrefix =
    kLoadBalancerUnroutableConfigId << 6;

// Interface which receives notifications when the current config is updated.
class QUIC_EXPORT_PRIVATE LoadBalancerEncoderVisitorInterface {
 public:
  virtual ~LoadBalancerEncoderVisitorInterface() {}

  // Called when a config is added where none existed.
  //
  // Connections that support address migration should retire unroutable
  // connection IDs and replace them with routable ones using the new config,
  // while avoiding sending a sudden storm of packets containing
  // RETIRE_CONNECTION_ID and NEW_CONNECTION_ID frames.
  virtual void OnConfigAdded(const uint8_t config_id) = 0;
  // Called when the config is changed.
  //
  // Existing routable connection IDs should be retired before the decoder stops
  // supporting that config. The timing of this event is deployment-dependent
  // and might be tied to the arrival of a new config at the encoder.
  virtual void OnConfigChanged(const uint8_t old_config_id,
                               const uint8_t new_config_id) = 0;
  // Called when a config is deleted. The encoder will generate unroutable
  // connection IDs from now on.
  //
  // New connections will not be able to support address migration until a new
  // config arrives. Existing connections can retain connection IDs that use the
  // deleted config, which will only become unroutable once the decoder also
  // deletes it. The time of that deletion is deployment-dependent and might be
  // tied to the arrival of a new config at the encoder.
  virtual void OnConfigDeleted(const uint8_t config_id) = 0;
};

// Manages QUIC-LB configurations to properly encode a given server ID in a
// QUIC Connection ID.
class QUIC_EXPORT_PRIVATE LoadBalancerEncoder
    : public ConnectionIdGeneratorInterface {
 public:
  LoadBalancerEncoder(QuicRandom& random,
                      LoadBalancerEncoderVisitorInterface* const visitor,
                      const bool len_self_encoded)
      : LoadBalancerEncoder(random, visitor, len_self_encoded,
                            kLoadBalancerUnroutableLen) {}
  ~LoadBalancerEncoder() override {}

  // Returns a newly created encoder with no active config, if
  // |unroutable_connection_id_length| is valid. |visitor| specifies an optional
  // interface to receive callbacks when config status changes.
  // If |len_self_encoded| is true, then the first byte of any generated
  // connection ids will encode the length. Otherwise, those bits will be
  // random. |unroutable_connection_id_length| specifies the length of
  // connection IDs to be generated when there is no active config. It must not
  // be 0 and must not be larger than the RFC9000 maximum of 20.
  static absl::optional<LoadBalancerEncoder> Create(
      QuicRandom& random, LoadBalancerEncoderVisitorInterface* const visitor,
      const bool len_self_encoded,
      const uint8_t unroutable_connection_id_len = kLoadBalancerUnroutableLen);

  // Attempts to replace the current config and server_id with |config| and
  // |server_id|. If the length |server_id| does not match the server_id_length
  // of |config| or the ID of |config| matches the ID of the current config,
  // returns false and leaves the current config unchanged. Otherwise, returns
  // true. When the encoder runs out of nonces, it will delete the config and
  // begin generating unroutable connection IDs.
  bool UpdateConfig(const LoadBalancerConfig& config,
                    const LoadBalancerServerId server_id);

  // Delete the current config and generate unroutable connection IDs from now
  // on.
  void DeleteConfig();

  // Returns the number of additional connection IDs that can be generated with
  // the current config, or 0 if there is no current config.
  absl::uint128 num_nonces_left() const { return num_nonces_left_; }

  // Functions below are declared virtual to enable mocking.
  // Returns true if there is an active configuration.
  virtual bool IsEncoding() const { return config_.has_value(); }
  // Returns true if there is an active configuration that uses encryption.
  virtual bool IsEncrypted() const {
    return config_.has_value() && config_->IsEncrypted();
  }
  virtual bool len_self_encoded() const { return len_self_encoded_; }

  // If there's an active config, generates a connection ID using it. If not,
  // generates an unroutable connection_id. If there's an error, returns a zero-
  // length Connection ID.
  QuicConnectionId GenerateConnectionId();

  // Functions from ConnectionIdGeneratorInterface
  absl::optional<QuicConnectionId> GenerateNextConnectionId(
      const QuicConnectionId& original) override;
  absl::optional<QuicConnectionId> MaybeReplaceConnectionId(
      const QuicConnectionId& original,
      const ParsedQuicVersion& version) override;
  uint8_t ConnectionIdLength(uint8_t first_byte) const override;

 protected:
  LoadBalancerEncoder(QuicRandom& random,
                      LoadBalancerEncoderVisitorInterface* const visitor,
                      const bool len_self_encoded,
                      const uint8_t unroutable_connection_id_len)
      : random_(random),
        len_self_encoded_(len_self_encoded),
        visitor_(visitor) {
    std::fill_n(connection_id_lengths_, 4, unroutable_connection_id_len);
  }

 private:
  friend class test::LoadBalancerEncoderPeer;

  QuicConnectionId MakeUnroutableConnectionId(uint8_t first_byte);

  QuicRandom& random_;
  const bool len_self_encoded_;
  LoadBalancerEncoderVisitorInterface* const visitor_;

  absl::optional<LoadBalancerConfig> config_;
  absl::uint128 seed_, num_nonces_left_ = 0;
  absl::optional<LoadBalancerServerId> server_id_;
  uint8_t connection_id_lengths_[kNumLoadBalancerConfigs + 1];
};

}  // namespace quic

#endif  // QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_ENCODER_H_
