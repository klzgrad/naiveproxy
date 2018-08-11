// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_provider.h"

namespace net {

EffectiveConnectionType NetworkQualityProvider::GetEffectiveConnectionType()
    const {
  return EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
}

base::Optional<base::TimeDelta> NetworkQualityProvider::GetHttpRTT() const {
  return base::Optional<base::TimeDelta>();
}

base::Optional<base::TimeDelta> NetworkQualityProvider::GetTransportRTT()
    const {
  return base::Optional<base::TimeDelta>();
}

base::Optional<int32_t> NetworkQualityProvider::GetDownstreamThroughputKbps()
    const {
  return base::Optional<int32_t>();
}

base::Optional<int32_t> NetworkQualityProvider::GetBandwidthDelayProductKbits()
    const {
  return base ::Optional<int32_t>();
}

}  // namespace net
