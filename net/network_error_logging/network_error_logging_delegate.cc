// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/network_error_logging/network_error_logging_delegate.h"

#include <memory>

namespace net {

namespace {

class NetworkErrorLoggingDelegateImpl : public NetworkErrorLoggingDelegate {
 public:
  NetworkErrorLoggingDelegateImpl() = default;

  ~NetworkErrorLoggingDelegateImpl() override = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkErrorLoggingDelegateImpl);
};

}  // namespace

NetworkErrorLoggingDelegate::~NetworkErrorLoggingDelegate() = default;

// static
std::unique_ptr<NetworkErrorLoggingDelegate>
NetworkErrorLoggingDelegate::Create() {
  return std::make_unique<NetworkErrorLoggingDelegateImpl>();
}

NetworkErrorLoggingDelegate::NetworkErrorLoggingDelegate() = default;

}  // namespace net
