// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_DELEGATE_H_
#define NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_DELEGATE_H_

#include <memory>

#include "base/macros.h"
#include "net/base/net_export.h"

namespace net {

class NET_EXPORT NetworkErrorLoggingDelegate {
 public:
  virtual ~NetworkErrorLoggingDelegate();

  static std::unique_ptr<NetworkErrorLoggingDelegate> Create();

 protected:
  NetworkErrorLoggingDelegate();

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkErrorLoggingDelegate);
};

}  // namespace net

#endif  // NET_NETWORK_ERROR_LOGGING_NETWORK_ERROR_LOGGING_DELEGATE_H_
