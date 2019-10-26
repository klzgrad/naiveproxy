// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_INTERCEPTOR_H_
#define NET_URL_REQUEST_URL_REQUEST_INTERCEPTOR_H_

#include "base/macros.h"
#include "net/base/net_export.h"

class GURL;

namespace net {

class URLRequest;
class URLRequestJob;
class NetworkDelegate;

// A URLRequestInterceptor is given a chance to create a URLRequestJob to
// handle URLRequests before they're handed off to the ProtocolHandler for
// the request's scheme.
class NET_EXPORT URLRequestInterceptor {
 public:
  URLRequestInterceptor();
  virtual ~URLRequestInterceptor();

  // Returns a URLRequestJob to handle |request|, if the interceptor wants to
  // take over the handling the request instead of the default ProtocolHandler.
  // Otherwise, returns NULL.
  virtual URLRequestJob* MaybeInterceptRequest(
      URLRequest* request, NetworkDelegate* network_delegate) const = 0;

  // Returns a URLRequestJob to handle |request|, if the interceptor wants to
  // take over the handling of the request after a redirect is received,
  // instead of using the default ProtocolHandler. Otherwise, returns NULL.
  virtual URLRequestJob* MaybeInterceptRedirect(
      URLRequest* request,
      NetworkDelegate* network_delegate,
      const GURL& location) const;

  // Returns a URLRequestJob to handle |request, if the interceptor wants to
  // take over the handling of the request after a response has started,
  // instead of using the default ProtocolHandler. Otherwise, returns NULL.
  virtual URLRequestJob* MaybeInterceptResponse(
      URLRequest* request, NetworkDelegate* network_delegate) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(URLRequestInterceptor);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_INTERCEPTOR_H_
