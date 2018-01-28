// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/network_error_logging_delegate.h"

namespace net {

NetworkErrorLoggingDelegate::ErrorDetails::ErrorDetails() {}

NetworkErrorLoggingDelegate::ErrorDetails::ErrorDetails(
    const ErrorDetails& other)
    : uri(other.uri),
      referrer(other.referrer),
      server_ip(other.server_ip),
      protocol(other.protocol),
      status_code(other.status_code),
      elapsed_time(other.elapsed_time),
      type(other.type) {}

NetworkErrorLoggingDelegate::ErrorDetails::~ErrorDetails() {}

const char NetworkErrorLoggingDelegate::kHeaderName[] = "NEL";

NetworkErrorLoggingDelegate::~NetworkErrorLoggingDelegate() {}

}  // namespace net
