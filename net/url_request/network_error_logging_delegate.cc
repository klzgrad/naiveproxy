// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/network_error_logging_delegate.h"

namespace net {

NetworkErrorLoggingDelegate::ErrorDetails::ErrorDetails() = default;

NetworkErrorLoggingDelegate::ErrorDetails::ErrorDetails(
    const ErrorDetails& other) = default;

NetworkErrorLoggingDelegate::ErrorDetails::~ErrorDetails() = default;

const char NetworkErrorLoggingDelegate::kHeaderName[] = "NEL";

NetworkErrorLoggingDelegate::~NetworkErrorLoggingDelegate() = default;

}  // namespace net
