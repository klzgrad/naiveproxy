// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_STH_OBSERVER_H_
#define NET_CERT_STH_OBSERVER_H_

#include <set>

#include "net/base/net_export.h"

namespace net {

namespace ct {

struct SignedTreeHead;

// Interface for receiving notifications of new STHs observed.
class NET_EXPORT STHObserver {
 public:
  virtual ~STHObserver() {}

  // Called with a new |sth| when one is observed.
  virtual void NewSTHObserved(const SignedTreeHead& sth) = 0;
};

}  // namespace ct

}  // namespace net

#endif  // NET_CERT_STH_OBSERVER_H_
