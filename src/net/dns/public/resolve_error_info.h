// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_PUBLIC_RESOLVE_ERROR_INFO_H_
#define NET_DNS_PUBLIC_RESOLVE_ERROR_INFO_H_

#include "net/base/net_errors.h"
#include "net/base/net_export.h"

namespace net {

// Host resolution error info.
struct NET_EXPORT ResolveErrorInfo {
  ResolveErrorInfo();
  ResolveErrorInfo(int resolve_error);
  ResolveErrorInfo(const ResolveErrorInfo& resolve_error_info);
  ResolveErrorInfo(ResolveErrorInfo&& other);

  ResolveErrorInfo& operator=(const ResolveErrorInfo& other);
  ResolveErrorInfo& operator=(ResolveErrorInfo&& other);

  bool operator==(const ResolveErrorInfo& other) const;
  bool operator!=(const ResolveErrorInfo& other) const;

  int error = net::OK;
};

}  // namespace net

#endif  // NET_DNS_PUBLIC_RESOLVE_ERROR_INFO_H_
