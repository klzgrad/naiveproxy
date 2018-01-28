// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/data_protocol_handler.h"

#include "net/url_request/url_request_data_job.h"

namespace net {

DataProtocolHandler::DataProtocolHandler() {
}

URLRequestJob* DataProtocolHandler::MaybeCreateJob(
    URLRequest* request, NetworkDelegate* network_delegate) const {
  return new URLRequestDataJob(request, network_delegate);
}

bool DataProtocolHandler::IsSafeRedirectTarget(const GURL& location) const {
  return false;
}

}  // namespace net
