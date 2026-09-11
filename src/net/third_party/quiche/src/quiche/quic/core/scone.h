// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_SCONE_H_
#define QUICHE_QUIC_CORE_SCONE_H_

// Constants relevant to the SCONE protocol (draft-ietf-quic-scone-04).

#include <cmath>
#include <cstdint>
#include <vector>

#include "absl/base/no_destructor.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"

namespace quic {

// SCONE capable clients append this to datagrams in the first flight.
static constexpr uint16_t kSconeIndicator = 0xc813;
static constexpr QuicByteCount kSconeIndicatorLength = sizeof(kSconeIndicator);

// If a QUIC Long Header contains a SCONE version, it is actually for a SCONE
// packet.
static constexpr QuicVersionLabel kSconeVersionHigh = 0xef7dc0fd;
static constexpr QuicVersionLabel kSconeVersionLow = 0x6f7dc0fd;

static constexpr int kNumSconeBandwidths = 127;
// Use an explicit lookup table to avoid floating point operations. Index 127
// is unknown.
// Returns the SCONE bandwidth table. Initialized once on first use.
inline const std::vector<QuicBandwidth>& GetSconeBandwidths() {
  static const absl::NoDestructor<std::vector<QuicBandwidth>> kBandwidths([] {
    std::vector<QuicBandwidth> v;
    v.reserve(kNumSconeBandwidths);
    double factor = 1.0;
    const double multiplier = 1.1220184543;  // 10^(1/20)
    for (int i = 0; i < kNumSconeBandwidths; ++i) {
      v.push_back(QuicBandwidth::FromKBitsPerSecond(
          static_cast<uint64_t>(std::round(100.0 * factor))));
      factor *= multiplier;
    }
    return v;
  }());
  return *kBandwidths;
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_SCONE_H_
