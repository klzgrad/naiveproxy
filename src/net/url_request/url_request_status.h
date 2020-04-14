// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_STATUS_H_
#define NET_URL_REQUEST_URL_REQUEST_STATUS_H_

#include "net/base/net_errors.h"
#include "net/base/net_export.h"

namespace net {

// Represents the result of a URL request. It encodes errors and various
// types of success.
class NET_EXPORT URLRequestStatus {
 public:
  enum Status {
    // Request succeeded, |error_| will be 0.
    SUCCESS = 0,

    // An IO request is pending, and the caller will be informed when it is
    // completed.
    IO_PENDING,

    // Request was cancelled programatically.
    CANCELED,

    // The request failed for some reason. |error_| may have more information.
    FAILED,
  };

  // Creates a successful URLRequestStatus.
  URLRequestStatus() : status_(SUCCESS), error_(0) {}

  // Creates a URLRequestStatus with specified status and error parameters. New
  // consumers should use URLRequestStatus::FromError instead.
  URLRequestStatus(Status status, int error);

  // Creates a URLRequestStatus, initializing the status from |error|. OK maps
  // to SUCCESS, ERR_IO_PENDING maps to IO_PENDING, ERR_ABORTED maps to CANCELED
  // and all others map to FAILED. Other combinations of status and error are
  // deprecated. See https://crbug.com/490311.
  static URLRequestStatus FromError(int error);

  // Returns a Error corresponding to |status_|.
  //   OK for OK
  //   ERR_IO_PENDING for IO_PENDING
  //   ERR_ABORTED for CANCELLED
  //   Error for FAILED
  Error ToNetError() const;

  Status status() const { return status_; }
  int error() const { return error_; }

  // Returns true if the status is success, which makes some calling code more
  // convenient because this is the most common test.
  bool is_success() const {
    return status_ == SUCCESS || status_ == IO_PENDING;
  }

  // Returns true if the request is waiting for IO.
  bool is_io_pending() const {
    return status_ == IO_PENDING;
  }

 private:
  // Application level status.
  Status status_;

  // Error code from the network layer if an error was encountered.
  int error_;
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_STATUS_H_
