// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_IP_RANGE_H_
#define QUICHE_QUIC_QBONE_PLATFORM_IP_RANGE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {

class IpRange {
 public:
  // Default constructor to have an uninitialized IpRange.
  IpRange() : prefix_length_(0) {}

  // prefix will be automatically truncated to prefix_length, so that any bit
  // after prefix_length are zero.
  IpRange(const QuicIpAddress& prefix, size_t prefix_length);

  bool operator==(IpRange other) const;
  bool operator!=(IpRange other) const;

  // Parses range that looks like "10.0.0.1/8". Tailing bits will be set to zero
  // after prefix_length. Return false if the parsing failed.
  bool FromString(const string& range);

  // Returns the string representation of this object.
  string ToString() const {
    if (IsInitialized()) {
      return absl::StrCat(prefix_.ToString(), "/", prefix_length_);
    }
    return "(uninitialized)";
  }

  // Whether this object is initialized.
  bool IsInitialized() const { return prefix_.IsInitialized(); }

  // Returns the first available IP address in this IpRange. The resulting
  // address will be uninitialized if there is no available address.
  QuicIpAddress FirstAddressInRange();

  // The address family of this IpRange.
  IpAddressFamily address_family() const { return prefix_.address_family(); }

  // The subnet's prefix address.
  QuicIpAddress prefix() const { return prefix_; }

  // The subnet's prefix length.
  size_t prefix_length() const { return prefix_length_; }

 private:
  QuicIpAddress prefix_;
  size_t prefix_length_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_IP_RANGE_H_
