// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUARTC_QUARTC_CLOCK_INTERFACE_H_
#define NET_QUIC_QUARTC_QUARTC_CLOCK_INTERFACE_H_

#include <stdint.h>

namespace net {

// Implemented by the Quartc API user to provide a timebase.
class QuartcClockInterface {
 public:
  virtual ~QuartcClockInterface() {}
  virtual int64_t NowMicroseconds() = 0;
};

}  // namespace net

#endif  // NET_QUIC_QUARTC_QUARTC_CLOCK_INTERFACE_H_
