// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_SERVER_ID_H_
#define QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_SERVER_ID_H_

#include <array>
#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// The maximum number of bytes in a LoadBalancerServerId.
inline constexpr uint8_t kLoadBalancerMaxServerIdLen = 15;
// Regardless of key length, the AES block size is always 16 Bytes.
inline constexpr uint8_t kLoadBalancerBlockSize = 16;
static_assert(kLoadBalancerMaxServerIdLen <= kLoadBalancerBlockSize,
              "LoadBalancerServerId array not large enough to hold Server ID");

// LoadBalancerServerId is the globally understood identifier for a given pool
// member. It is unique to any given QUIC-LB configuration. See
// draft-ietf-quic-load-balancers.
// Note: this has nothing to do with QuicServerID. It's an unfortunate collision
// between an internal term for the destination identifiers for a particular
// deployment (QuicServerID) and the object of a load balancing decision
// (LoadBalancerServerId).
class QUIC_EXPORT_PRIVATE LoadBalancerServerId {
 public:
  // Creates an empty/invalid server id.
  LoadBalancerServerId() : length_(0) {}

  // Copies all the bytes from |data| into a new LoadBalancerServerId.
  explicit LoadBalancerServerId(absl::Span<const uint8_t> data);
  explicit LoadBalancerServerId(absl::string_view data);

  // Server IDs are opaque bytes, but defining these operators allows us to sort
  // them into a tree and define ranges.
  bool operator<(const LoadBalancerServerId& other) const {
    return data() < other.data();
  }
  bool operator==(const LoadBalancerServerId& other) const {
    return data() == other.data();
  }

  // Hash function to allow use as a key in unordered maps.
  template <typename H>
  friend H AbslHashValue(H h, const LoadBalancerServerId& server_id) {
    return H::combine_contiguous(std::move(h), server_id.data().data(),
                                 server_id.length());
  }

  absl::Span<const uint8_t> data() const {
    return absl::MakeConstSpan(data_.data(), length_);
  }
  uint8_t* mutable_data() { return data_.data(); }

  uint8_t length() const { return length_; }
  void set_length(uint8_t length);

  // Returns the server ID in hex format.
  std::string ToString() const;

  // Returns true if this is a valid server id.
  bool IsValid() { return length_ != 0; }

 private:
  // Make the array large enough to hold an entire decrypt result, to save a
  // copy from the decrypt result into LoadBalancerServerId.
  std::array<uint8_t, kLoadBalancerBlockSize> data_;
  uint8_t length_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_LOAD_BALANCER_LOAD_BALANCER_SERVER_ID_H_
