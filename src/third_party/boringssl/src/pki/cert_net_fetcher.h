// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BSSL_PKI_CERT_NET_FETCHER_H_
#define BSSL_PKI_CERT_NET_FETCHER_H_

#include "webutil/url/url.h"
#include "fillins/openssl_util.h"
#include <stdint.h>

#include <memory>
#include <vector>

#include <memory>
#include "fillins/log.h"
#include "fillins/net_errors.h"



class URL;

namespace bssl {

// CertNetFetcher is a synchronous interface for fetching AIA URLs and CRL
// URLs. It is shared between a caller thread (which starts and waits for
// fetches), and a network thread (which does the actual fetches). It can be
// shutdown from the network thread to cancel outstanding requests.
//
// A Request object is returned when starting a fetch. The consumer can
// use this as a handle for aborting the request (by freeing it), or reading
// the result of the request (WaitForResult)
class OPENSSL_EXPORT CertNetFetcher
     {
 public:
  class Request {
   public:
    virtual ~Request() = default;

    // WaitForResult() can be called at most once.
    //
    // It will block and wait for the (network) request to complete, and
    // then write the result into the provided out-parameters.
    virtual void WaitForResult(Error* error, std::vector<uint8_t>* bytes) = 0;
  };

  // This value can be used in place of timeout or max size limits.
  enum { DEFAULT = -1 };

  CertNetFetcher() = default;

  CertNetFetcher(const CertNetFetcher&) = delete;
  CertNetFetcher& operator=(const CertNetFetcher&) = delete;

  // Shuts down the CertNetFetcher and cancels outstanding network requests. It
  // is not guaranteed that any outstanding or subsequent
  // Request::WaitForResult() calls will be completed. Shutdown() must be called
  // from the network thread. It can be called more than once, but must be
  // called before the CertNetFetcher is destroyed.
  virtual void Shutdown() = 0;

  // The Fetch*() methods start a request which can be cancelled by
  // deleting the returned Request. Here is the meaning of the common
  // parameters:
  //
  //   * url -- The http:// URL to fetch.
  //   * timeout_seconds -- The maximum allowed duration for the fetch job. If
  //         this delay is exceeded then the request will fail. To use a default
  //         timeout pass DEFAULT.
  //   * max_response_bytes -- The maximum size of the response body. If this
  //     size is exceeded then the request will fail. To use a default timeout
  //     pass DEFAULT.

  [[nodiscard]] virtual std::unique_ptr<Request> FetchCaIssuers(
      const URL& url,
      int timeout_milliseconds,
      int max_response_bytes) = 0;

  [[nodiscard]] virtual std::unique_ptr<Request> FetchCrl(
      const URL& url,
      int timeout_milliseconds,
      int max_response_bytes) = 0;

  [[nodiscard]] virtual std::unique_ptr<Request> FetchOcsp(
      const URL& url,
      int timeout_milliseconds,
      int max_response_bytes) = 0;

 protected:
  virtual ~CertNetFetcher() = default;

 private:
  
};

}  // namespace net

#endif  // BSSL_PKI_CERT_NET_FETCHER_H_
