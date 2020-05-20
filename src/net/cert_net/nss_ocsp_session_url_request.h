// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_NET_NSS_OCSP_SESSION_URL_REQUEST_H_
#define NET_CERT_NET_NSS_OCSP_SESSION_URL_REQUEST_H_

#include "net/base/net_export.h"
#include "net/url_request/url_request_context.h"

namespace net {

class URLRequestContext;

// Sets the URLRequestContext and MessageLoop for HTTP requests issued by NSS
// (i.e. OCSP, CA certificate and CRL fetches).  Must be called again with
// |request_context|=nullptr before the URLRequestContext is destroyed.
// Must be called from IO task runner.
// This will call SetOCSPRequestSessionDelegateFactory in nss_ocsp.h with a new
// factory instance using |request_context|, or call
// SetOCSPRequestSessionFactory with nullptr if |request_context|=nullptr.
NET_EXPORT void SetURLRequestContextForNSSHttpIO(
    URLRequestContext* request_context);

}  // namespace net

#endif  // NET_CERT_NET_NSS_OCSP_SESSION_URL_REQUEST_H_
