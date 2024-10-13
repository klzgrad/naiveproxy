// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_bandwidth.h"

#include <cinttypes>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace quic {

std::string QuicBandwidth::ToDebuggingValue() const {
  if (bits_per_second_ < 80000) {
    return absl::StrFormat("%d bits/s (%d bytes/s)", bits_per_second_,
                           bits_per_second_ / 8);
  }

  double divisor;
  char unit;
  if (bits_per_second_ < 8 * 1000 * 1000) {
    divisor = 1e3;
    unit = 'k';
  } else if (bits_per_second_ < INT64_C(8) * 1000 * 1000 * 1000) {
    divisor = 1e6;
    unit = 'M';
  } else {
    divisor = 1e9;
    unit = 'G';
  }

  double bits_per_second_with_unit = bits_per_second_ / divisor;
  double bytes_per_second_with_unit = bits_per_second_with_unit / 8;
  return absl::StrFormat("%.2f %cbits/s (%.2f %cbytes/s)",
                         bits_per_second_with_unit, unit,
                         bytes_per_second_with_unit, unit);
}

}  // namespace quic
