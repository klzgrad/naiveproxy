// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_OBSERVER_H_
#define NET_REPORTING_REPORTING_OBSERVER_H_

#include "base/macros.h"
#include "net/base/net_export.h"

namespace net {

class NET_EXPORT ReportingObserver {
 public:
  // Called whenever any change is made to the ReportingCache.
  virtual void OnCacheUpdated();

 protected:
  ReportingObserver();

  ~ReportingObserver();

  DISALLOW_COPY_AND_ASSIGN(ReportingObserver);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_OBSERVER_H_
