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
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

namespace {

// Helper to allow setting the const array during initialization.
std::array<uint8_t, kLoadBalancerMaxServerIdLen> MakeArray(
    const absl::Span<const uint8_t> data, const uint8_t length) {
  std::array<uint8_t, kLoadBalancerMaxServerIdLen> array;
  memcpy(array.data(), data.data(), length);
  return array;
}

}  // namespace

absl::optional<LoadBalancerServerId> LoadBalancerServerId::Create(
    const absl::Span<const uint8_t> data) {
  if (data.length() == 0 || data.length() > kLoadBalancerMaxServerIdLen) {
    QUIC_BUG(quic_bug_433312504_01)
        << "Attempted to create LoadBalancerServerId with length "
        << data.length();
    return absl::optional<LoadBalancerServerId>();
  }
  return LoadBalancerServerId(data);
}

std::string LoadBalancerServerId::ToString() const {
  return absl::BytesToHexString(
      absl::string_view(reinterpret_cast<const char*>(data_.data()), length_));
}

LoadBalancerServerId::LoadBalancerServerId(const absl::Span<const uint8_t> data)
    : data_(MakeArray(data, data.length())), length_(data.length()) {}

}  // namespace quic
