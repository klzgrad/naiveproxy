// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_NET_NSS_OCSP_H_
#define NET_CERT_NET_NSS_OCSP_H_

#include "net/base/net_export.h"

namespace net {

class URLRequestContext;

// Initializes HTTP client functions for NSS.  This function is thread-safe,
// and HTTP handlers will only ever be initialized once.
NET_EXPORT void EnsureNSSHttpIOInit();

// Sets the URLRequestContext and MessageLoop for HTTP requests issued by NSS
// (i.e. OCSP, CA certificate and CRL fetches).  Must be called again with
// |request_context|=nullptr before the URLRequestContext is destroyed.
NET_EXPORT void SetURLRequestContextForNSSHttpIO(
    URLRequestContext* request_context);

}  // namespace net

#endif  // NET_CERT_NET_NSS_OCSP_H_
