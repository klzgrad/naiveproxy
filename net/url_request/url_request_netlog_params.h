// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_NETLOG_PARAMS_H_
#define NET_URL_REQUEST_URL_REQUEST_NETLOG_PARAMS_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "net/base/net_export.h"
#include "net/base/request_priority.h"

class GURL;

namespace base {
class Value;
}

namespace net {

class NetLogCaptureMode;

// Returns a Value containing NetLog parameters for constructing a URLRequest.
NET_EXPORT std::unique_ptr<base::Value> NetLogURLRequestConstructorCallback(
    const GURL* url,
    RequestPriority priority,
    NetLogCaptureMode /* capture_mode */);

// Returns a Value containing NetLog parameters for starting a URLRequest.
NET_EXPORT std::unique_ptr<base::Value> NetLogURLRequestStartCallback(
    const GURL* url,
    const std::string* method,
    int load_flags,
    int64_t upload_id,
    NetLogCaptureMode /* capture_mode */);

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_NETLOG_PARAMS_H_
