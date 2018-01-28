// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_request_info.h"

namespace net {

HttpRequestInfo::HttpRequestInfo()
    : upload_data_stream(NULL),
      load_flags(0),
      motivation(NORMAL_MOTIVATION),
      privacy_mode(PRIVACY_MODE_DISABLED) {
}

HttpRequestInfo::HttpRequestInfo(const HttpRequestInfo& other) = default;

HttpRequestInfo::~HttpRequestInfo() {}

}  // namespace net
