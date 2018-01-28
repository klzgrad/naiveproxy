// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_NETWORK_ERROR_LOGGING_DELEGATE_H_
#define NET_URL_REQUEST_NETWORK_ERROR_LOGGING_DELEGATE_H_

#include <string>

#include "base/callback.h"
#include "base/time/time.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/socket/next_proto.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

class ReportingService;

class NET_EXPORT NetworkErrorLoggingDelegate {
 public:
  // The details of a network error that are included in an NEL report.
  //
  // See http://wicg.github.io/network-error-logging/#dfn-network-error-object
  // for details on the semantics of each field.
  struct NET_EXPORT ErrorDetails {
    ErrorDetails();
    ErrorDetails(const ErrorDetails& other);
    ~ErrorDetails();

    GURL uri;
    GURL referrer;
    IPAddress server_ip;
    NextProto protocol;
    int status_code;
    base::TimeDelta elapsed_time;
    Error type;
  };

  static const char kHeaderName[];

  virtual ~NetworkErrorLoggingDelegate();

  // Sets the ReportingService that will be used to report network errors on
  // interested origins.
  //
  // |reporting_service| can be nullptr, in which case reports will not be made,
  // but Network Error Logging will continue observing headers and network
  // errors.
  virtual void SetReportingService(ReportingService* reporting_service) = 0;

  // Called when the network stack receives an "NEL" header over a secure
  // connection. |origin| is the origin from which the header was received;
  // |value| is the value of the header.
  //
  // See http://wicg.github.io/network-error-logging/#x`nel`-header-field for
  // details on the configuration available via the header.
  virtual void OnHeader(const url::Origin& origin,
                        const std::string& value) = 0;

  // Called when the network stack detects a network error.
  //
  // |details| is the details of the network error.
  virtual void OnNetworkError(const ErrorDetails& details) = 0;
};

}  // namespace net

#endif  // NET_URL_REQUEST_NETWORK_ERROR_LOGGING_DELEGATE_H_
