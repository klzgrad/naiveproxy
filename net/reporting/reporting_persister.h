// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_PERSISTER_H_
#define NET_REPORTING_REPORTING_PERSISTER_H_

#include <memory>

#include "base/callback.h"
#include "net/base/net_export.h"

namespace net {

class ReportingContext;

// Will persist the state of the Reporting system to (reasonably) stable
// storage using an as-yet-unwritten persistence mechanism within //net.
class NET_EXPORT ReportingPersister {
 public:
  // Creates a ReportingPersister. |context| must outlive the persister.
  static std::unique_ptr<ReportingPersister> Create(ReportingContext* context);

  virtual ~ReportingPersister();
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_PERSISTER_H_
