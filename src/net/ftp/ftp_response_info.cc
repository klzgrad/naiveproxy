// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ftp/ftp_response_info.h"

namespace net {

FtpResponseInfo::FtpResponseInfo()
    : needs_auth(false),
      expected_content_size(-1),
      is_directory_listing(false) {
}

FtpResponseInfo::~FtpResponseInfo() = default;

}  // namespace net
