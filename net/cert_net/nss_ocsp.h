// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_NET_NSS_OCSP_H_
#define NET_CERT_NET_NSS_OCSP_H_

#include "net/base/net_export.h"

namespace net {

class URLRequestContext;

// Sets the MessageLoop for NSS's HTTP client functions (i.e. OCSP, CA
// certificate and CRL fetches) to the current message loop.  This should be
// called before EnsureNSSHttpIOInit() if you want to control the message loop.
NET_EXPORT void SetMessageLoopForNSSHttpIO();

// Initializes HTTP client functions for NSS.  This must be called before any
// certificate verification functions.  This function is thread-safe, and HTTP
// handlers will only ever be initialized once.  ShutdownNSSHttpIO() must be
// called on shutdown.
NET_EXPORT void EnsureNSSHttpIOInit();

// This should be called once on shutdown to stop issuing URLRequests for NSS
// related HTTP fetches.
NET_EXPORT void ShutdownNSSHttpIO();

// Can be called after a call to |ShutdownNSSHttpIO()| to reset internal state
// and associate it with the current thread.
NET_EXPORT void ResetNSSHttpIOForTesting();

// Sets the URLRequestContext for HTTP requests issued by NSS.
NET_EXPORT void SetURLRequestContextForNSSHttpIO(
    URLRequestContext* request_context);

}  // namespace net

#endif  // NET_CERT_NET_NSS_OCSP_H_
