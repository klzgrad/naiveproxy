// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/ip_range.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"

namespace quic {

namespace {

constexpr size_t kIPv4Size = 32;
constexpr size_t kIPv6Size = 128;

QuicIpAddress TruncateToLength(const QuicIpAddress& input,
                               size_t* prefix_length) {
  QuicIpAddress output;
  if (input.IsIPv4()) {
    if (*prefix_length > kIPv4Size) {
      *prefix_length = kIPv4Size;
      return input;
    }
    uint32_t raw_address =
        *reinterpret_cast<const uint32_t*>(input.ToPackedString().data());
    raw_address = quiche::QuicheEndian::NetToHost32(raw_address);
    raw_address &= ~0U << (kIPv4Size - *prefix_length);
    raw_address = quiche::QuicheEndian::HostToNet32(raw_address);
    output.FromPackedString(reinterpret_cast<const char*>(&raw_address),
                            sizeof(raw_address));
    return output;
  }
  if (input.IsIPv6()) {
    if (*prefix_length > kIPv6Size) {
      *prefix_length = kIPv6Size;
      return input;
    }
    uint64_t raw_address[2];
    memcpy(raw_address, input.ToPackedString().data(), sizeof(raw_address));
    // raw_address[0] holds higher 8 bytes in big endian and raw_address[1]
    // holds lower 8 bytes. Converting each to little endian for us to mask bits
    // out.
    // The endianess between raw_address[0] and raw_address[1] is handled
    // explicitly by handling lower and higher bytes separately.
    raw_address[0] = quiche::QuicheEndian::NetToHost64(raw_address[0]);
    raw_address[1] = quiche::QuicheEndian::NetToHost64(raw_address[1]);
    if (*prefix_length <= kIPv6Size / 2) {
      raw_address[0] &= ~uint64_t{0} << (kIPv6Size / 2 - *prefix_length);
      raw_address[1] = 0;
    } else {
      raw_address[1] &= ~uint64_t{0} << (kIPv6Size - *prefix_length);
    }
    raw_address[0] = quiche::QuicheEndian::HostToNet64(raw_address[0]);
    raw_address[1] = quiche::QuicheEndian::HostToNet64(raw_address[1]);
    output.FromPackedString(reinterpret_cast<const char*>(raw_address),
                            sizeof(raw_address));
    return output;
  }
  return output;
}

}  // namespace

IpRange::IpRange(const QuicIpAddress& prefix, size_t prefix_length)
    : prefix_(prefix), prefix_length_(prefix_length) {
  prefix_ = TruncateToLength(prefix_, &prefix_length_);
}

bool IpRange::operator==(IpRange other) const {
  return prefix_ == other.prefix_ && prefix_length_ == other.prefix_length_;
}

bool IpRange::operator!=(IpRange other) const {
  return !(*this == other);
}

bool IpRange::FromString(const std::string& range) {
  size_t slash_pos = range.find('/');
  if (slash_pos == std::string::npos) {
    return false;
  }
  QuicIpAddress prefix;
  bool success = prefix.FromString(range.substr(0, slash_pos));
  if (!success) {
    return false;
  }
  uint64_t num_processed = 0;
  size_t prefix_length = std::stoi(range.substr(slash_pos + 1), &num_processed);
  if (num_processed + 1 + slash_pos != range.length()) {
    return false;
  }
  prefix_ = TruncateToLength(prefix, &prefix_length);
  prefix_length_ = prefix_length;
  return true;
}

QuicIpAddress IpRange::FirstAddressInRange() {
  return prefix();
}

}  // namespace quic
