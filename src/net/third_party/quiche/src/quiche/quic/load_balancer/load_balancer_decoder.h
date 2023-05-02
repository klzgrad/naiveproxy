// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_DECODER_H_
#define QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_DECODER_H_

#include "quiche/quic/load_balancer/load_balancer_config.h"
#include "quiche/quic/load_balancer/load_balancer_server_id.h"

namespace quic {

// Manages QUIC-LB configurations to extract a server ID from a properly
// encoded connection ID, usually on behalf of a load balancer.
class QUIC_EXPORT_PRIVATE LoadBalancerDecoder {
 public:
  // Returns false if the config_id codepoint is already occupied.
  bool AddConfig(const LoadBalancerConfig& config);

  // Remove support for a config. Does nothing if there is no config for
  // |config_id|. Does nothing and creates a bug if |config_id| is greater than
  // 2.
  void DeleteConfig(uint8_t config_id);

  // Return the config for |config_id|, or nullptr if not found.
  const LoadBalancerConfig* GetConfig(const uint8_t config_id) const {
    if (config_id >= kNumLoadBalancerConfigs ||
        !config_[config_id].has_value()) {
      return nullptr;
    }

    return &config_[config_id].value();
  }

  // Extract a server ID from |connection_id|. If there is no config for the
  // codepoint, |connection_id| is too short, or there's a decrypt error,
  // returns empty. Will accept |connection_id| that is longer than necessary
  // without error.
  absl::optional<LoadBalancerServerId> GetServerId(
      const QuicConnectionId& connection_id) const;

  // Returns the config ID stored in the first two bits of |connection_id|, or
  // empty if |connection_id| is empty, or the first two bits of the first byte
  // of |connection_id| are 0b11.
  static absl::optional<uint8_t> GetConfigId(
      const QuicConnectionId& connection_id);

  // Returns the config ID stored in the first two bits of
  // |connection_id_first_byte|, or empty if the first two bits are 0b11.
  static absl::optional<uint8_t> GetConfigId(uint8_t connection_id_first_byte);

 private:
  // Decoders can support up to 3 configs at once.
  absl::optional<LoadBalancerConfig> config_[kNumLoadBalancerConfigs];
};

}  // namespace quic

#endif  // QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_DECODER_H_
