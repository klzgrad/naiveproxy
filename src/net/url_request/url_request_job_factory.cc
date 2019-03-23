// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_job_factory.h"

namespace net {

URLRequestJobFactory::ProtocolHandler::~ProtocolHandler() = default;

bool URLRequestJobFactory::ProtocolHandler::IsSafeRedirectTarget(
    const GURL& location) const {
  return true;
}

URLRequestJobFactory::URLRequestJobFactory() = default;

URLRequestJobFactory::~URLRequestJobFactory() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

}  // namespace net
