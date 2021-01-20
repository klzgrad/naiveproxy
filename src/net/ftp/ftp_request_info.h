// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FTP_FTP_REQUEST_INFO_H_
#define NET_FTP_FTP_REQUEST_INFO_H_

#include "url/gurl.h"

namespace net {

class FtpRequestInfo {
 public:
  // The requested URL.
  GURL url;
};

}  // namespace net

#endif  // NET_FTP_FTP_REQUEST_INFO_H_
