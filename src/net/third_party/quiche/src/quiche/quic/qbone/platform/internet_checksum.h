// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_INTERNET_CHECKSUM_H_
#define QUICHE_QUIC_QBONE_PLATFORM_INTERNET_CHECKSUM_H_

#include <cstddef>
#include <cstdint>

namespace quic {

// Incrementally compute an Internet header checksum as described in RFC 1071.
class InternetChecksum {
 public:
  // Update the checksum with the specified data.  Note that while the checksum
  // is commutative, the data has to be supplied in the units of two-byte words.
  // If there is an extra byte at the end, the function has to be called on it
  // last.
  void Update(const char* data, size_t size);

  void Update(const uint8_t* data, size_t size);

  uint16_t Value() const;

 private:
  uint32_t accumulator_ = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_INTERNET_CHECKSUM_H_
