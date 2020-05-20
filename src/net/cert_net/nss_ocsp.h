// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_NET_NSS_OCSP_H_
#define NET_CERT_NET_NSS_OCSP_H_

#include <nspr.h>

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "url/gurl.h"

namespace net {

struct NET_EXPORT OCSPRequestSessionParams {
  OCSPRequestSessionParams();
  ~OCSPRequestSessionParams();

  GURL url;  // The URL to initially fetch
  std::string http_request_method;
  base::TimeDelta timeout;
  HttpRequestHeaders extra_request_headers;

  // HTTP POST payload.
  std::string upload_content;
  std::string upload_content_type;  // MIME type of POST payload
};

struct NET_EXPORT OCSPRequestSessionResult {
  OCSPRequestSessionResult();
  ~OCSPRequestSessionResult();

  int response_code = -1;  // HTTP status code for the request
  std::string response_content_type;
  scoped_refptr<HttpResponseHeaders> response_headers;
  std::string data;  // Results of the request
};

// This interface should be implemented to provide synchronous loading of OCSP
// requests specified by OCSPRequestSessionParams. Returns an instance of
// OCSPRequestSessionResult.
class NET_EXPORT OCSPRequestSessionDelegate
    : public base::RefCountedThreadSafe<OCSPRequestSessionDelegate> {
 public:
  REQUIRE_ADOPTION_FOR_REFCOUNTED_TYPE();

  // Starts the load using the parameters specified in |params|, and then blocks
  // the thread until the result is received. Returns the result, or nullptr on
  // error.
  virtual std::unique_ptr<OCSPRequestSessionResult> StartAndWait(
      const OCSPRequestSessionParams* params) = 0;

 protected:
  friend class base::RefCountedThreadSafe<OCSPRequestSessionDelegate>;

  virtual ~OCSPRequestSessionDelegate();
};

class NET_EXPORT OCSPRequestSessionDelegateFactory {
 public:
  OCSPRequestSessionDelegateFactory();

  // Not thread-safe, but can be called on different threads with the use of
  // mutual exclusion.
  virtual scoped_refptr<OCSPRequestSessionDelegate>
  CreateOCSPRequestSessionDelegate() = 0;

  virtual ~OCSPRequestSessionDelegateFactory();
};

// Sets the factory that creates OCSPRequestSessions.
NET_EXPORT void SetOCSPRequestSessionDelegateFactory(
    std::unique_ptr<OCSPRequestSessionDelegateFactory> factory);

// Initializes HTTP client functions for NSS.  This function is thread-safe,
// and HTTP handlers will only ever be initialized once.
NET_EXPORT void EnsureNSSHttpIOInit();
}  // namespace net

#endif  // NET_CERT_NET_NSS_OCSP_H_
