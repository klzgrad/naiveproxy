// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NQE_EXTERNAL_ESTIMATE_PROVIDER_H_
#define NET_NQE_EXTERNAL_ESTIMATE_PROVIDER_H_

#include <stdint.h>

#include "base/macros.h"
#include "base/time/time.h"
#include "net/base/net_export.h"

namespace net {

// Base class used by external providers such as operating system APIs to
// provide network quality estimates to NetworkQualityEstimator.
class NET_EXPORT ExternalEstimateProvider {
 public:
  class NET_EXPORT UpdatedEstimateDelegate {
   public:
    // Will be called with updated RTT, and downstream throughput (in kilobits
    // per second) when an updated estimate is available. If the estimate is
    // unavailable, it is set to a negative value.
    virtual void OnUpdatedEstimateAvailable(
        const base::TimeDelta& rtt,
        int32_t downstream_throughput_kbps) = 0;

   protected:
    UpdatedEstimateDelegate() {}
    virtual ~UpdatedEstimateDelegate() {}

   private:
    DISALLOW_COPY_AND_ASSIGN(UpdatedEstimateDelegate);
  };

  ExternalEstimateProvider() {}
  virtual ~ExternalEstimateProvider() {}

  // Requests the provider to clear its cached network quality estimate.
  virtual void ClearCachedEstimate() = 0;

  // Sets delegate that is notified when an updated estimate is available.
  // |delegate| should outlive |ExternalEstimateProvider|.
  virtual void SetUpdatedEstimateDelegate(
      UpdatedEstimateDelegate* delegate) = 0;

  // Requests an updated network quality estimate from the external estimate
  // provider.
  virtual void Update() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExternalEstimateProvider);
};

}  // namespace net

#endif  // NET_NQE_EXTERNAL_ESTIMATE_PROVIDER_H_
