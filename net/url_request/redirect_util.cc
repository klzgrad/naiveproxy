// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/redirect_util.h"

#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

// static
void RedirectUtil::UpdateHttpRequest(const GURL& original_url,
                                     const std::string& original_method,
                                     const RedirectInfo& redirect_info,
                                     HttpRequestHeaders* request_headers,
                                     bool* should_clear_upload) {
  DCHECK(request_headers);
  DCHECK(should_clear_upload);

  *should_clear_upload = false;

  if (redirect_info.new_method != original_method) {
    // TODO(davidben): This logic still needs to be replicated at the consumers.
    //
    // The Origin header is sent on anything that is not a GET or HEAD, which
    // suggests all redirects that change methods (since they always change to
    // GET) should drop the Origin header.
    // See https://fetch.spec.whatwg.org/#origin-header
    // TODO(jww): This is Origin header removal is probably layering violation
    // and should be refactored into //content. See https://crbug.com/471397.
    // See also: https://crbug.com/760487
    request_headers->RemoveHeader(HttpRequestHeaders::kOrigin);

    // The inclusion of a multipart Content-Type header can cause problems with
    // some servers:
    // http://code.google.com/p/chromium/issues/detail?id=843
    request_headers->RemoveHeader(HttpRequestHeaders::kContentLength);
    request_headers->RemoveHeader(HttpRequestHeaders::kContentType);

    *should_clear_upload = true;
  }

  // Cross-origin redirects should not result in an Origin header value that is
  // equal to the original request's Origin header. This is necessary to prevent
  // a reflection of POST requests to bypass CSRF protections. If the header was
  // not set to "null", a POST request from origin A to a malicious origin M
  // could be redirected by M back to A.
  //
  // This behavior is specified in step 10 of the HTTP-redirect fetch
  // algorithm[1] (which supercedes the behavior outlined in RFC 6454[2].
  //
  // [1]: https://fetch.spec.whatwg.org/#http-redirect-fetch
  // [2]: https://tools.ietf.org/html/rfc6454#section-7
  //
  // TODO(jww): This is a layering violation and should be refactored somewhere
  // up into //net's embedder. https://crbug.com/471397
  if (!url::Origin::Create(redirect_info.new_url)
           .IsSameOriginWith(url::Origin::Create(original_url)) &&
      request_headers->HasHeader(HttpRequestHeaders::kOrigin)) {
    request_headers->SetHeader(HttpRequestHeaders::kOrigin,
                               url::Origin().Serialize());
  }
}

}  // namespace net
