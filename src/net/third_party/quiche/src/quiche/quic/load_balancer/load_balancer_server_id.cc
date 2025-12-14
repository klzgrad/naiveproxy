// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/load_balancer/load_balancer_server_id.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

LoadBalancerServerId::LoadBalancerServerId(absl::string_view data)
    : LoadBalancerServerId(absl::MakeSpan(
          reinterpret_cast<const uint8_t*>(data.data()), data.length())) {}

LoadBalancerServerId::LoadBalancerServerId(absl::Span<const uint8_t> data)
    : length_(data.length()) {
  if (length_ == 0 || length_ > kLoadBalancerMaxServerIdLen) {
    QUIC_BUG(quic_bug_433312504_02)
        << "Attempted to create LoadBalancerServerId with length "
        << static_cast<int>(length_);
    length_ = 0;
    return;
  }
  memcpy(data_.data(), data.data(), data.length());
}

void LoadBalancerServerId::set_length(uint8_t length) {
  QUIC_BUG_IF(quic_bug_599862571_01,
              length == 0 || length > kLoadBalancerMaxServerIdLen)
      << "Attempted to set LoadBalancerServerId length to "
      << static_cast<int>(length);
  length_ = length;
}

std::string LoadBalancerServerId::ToString() const {
  return absl::BytesToHexString(
      absl::string_view(reinterpret_cast<const char*>(data_.data()), length_));
}

}  // namespace quic
