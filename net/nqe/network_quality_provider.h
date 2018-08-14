// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NQE_NETWORK_QUALITY_PROVIDER_H_
#define NET_NQE_NETWORK_QUALITY_PROVIDER_H_

#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/optional.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/nqe/effective_connection_type.h"

namespace net {

class EffectiveConnectionTypeObserver;

// Provides simple interface to obtain the network quality, and to listen to
// the changes in the network quality.
class NET_EXPORT NetworkQualityProvider {
 public:
  virtual ~NetworkQualityProvider() {}

  // Returns the current effective connection type.  The effective connection
  // type is computed by the network quality estimator at regular intervals and
  // at certain events (e.g., connection change).
  virtual EffectiveConnectionType GetEffectiveConnectionType() const;

  // Adds |observer| to a list of effective connection type observers.
  // The observer must register and unregister itself on the same thread.
  // |observer| would be notified on the thread on which it registered.
  // |observer| would be notified of the current effective connection
  // type in the next message pump.
  virtual void AddEffectiveConnectionTypeObserver(
      EffectiveConnectionTypeObserver* observer) {}

  // Removes |observer| from a list of effective connection type observers.
  virtual void RemoveEffectiveConnectionTypeObserver(
      EffectiveConnectionTypeObserver* observer) {}

  // Returns the current HTTP RTT estimate. If the estimate is unavailable,
  // the returned optional value is null. The RTT at the HTTP layer measures the
  // time from when the request was sent (this happens after the connection is
  // established) to the time when the response headers were received.
  virtual base::Optional<base::TimeDelta> GetHttpRTT() const;

  // Returns the current transport RTT estimate. If the estimate is
  // unavailable, the returned optional value is null.  The RTT at the transport
  // layer provides an aggregate estimate of the transport RTT as computed by
  // various underlying TCP and QUIC connections.
  virtual base::Optional<base::TimeDelta> GetTransportRTT() const;

  // Returns the current downstream throughput estimate (in kilobits per
  // second). If the estimate is unavailable, the returned optional value is
  // null.
  virtual base::Optional<int32_t> GetDownstreamThroughputKbps() const;

  // Returns the current bandwidth delay product estimate (in kilobits). If the
  // estimate is not available, the returned optional value is null. The
  // bandwidth delay product is calculated from the transport RTT and the
  // downlink bandwidth estimates.
  virtual base::Optional<int32_t> GetBandwidthDelayProductKbits() const;

 protected:
  NetworkQualityProvider() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkQualityProvider);
};

}  // namespace net

#endif  // NET_NQE_NETWORK_QUALITY_PROVIDER_H_
