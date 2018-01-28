// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_STH_REPORTER_H_
#define NET_CERT_STH_REPORTER_H_

#include <set>

#include "net/base/net_export.h"

namespace net {

namespace ct {

class STHObserver;

// Interface for registering/unregistering observers.
class NET_EXPORT STHReporter {
 public:
  virtual ~STHReporter() {}

  virtual void RegisterObserver(STHObserver* observer) = 0;
  virtual void UnregisterObserver(STHObserver* observer) = 0;
};

}  // namespace ct

}  // namespace net

#endif  // NET_CERT_STH_REPORTER_H_
