// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_status.h"

#include "base/check_op.h"
#include "base/notreached.h"
#include "net/base/net_errors.h"

namespace net {

URLRequestStatus::URLRequestStatus(Status status, int error)
    : status_(status), error_(error) {
  // URLRequestStatus should get folded into error. However, it is possible to
  // create URLRequestStatuses with inconsistent |status_| and |error_|
  // fields. As callers are cleaned up, these assertions avoid regressing any
  // invariants that have been established.
  //
  // https://crbug.com/490311
  DCHECK_GE(0, error_);
  switch (status_) {
    case SUCCESS:
      DCHECK_EQ(OK, error_);
      break;
    case IO_PENDING:
      // TODO(davidben): Switch all IO_PENDING status to ERR_IO_PENDING.
      DCHECK(error_ == 0 || error_ == ERR_IO_PENDING);
      break;
    case CANCELED:
    case FAILED:
      DCHECK_NE(OK, error_);
      DCHECK_NE(ERR_IO_PENDING, error_);
      break;
  }
}

URLRequestStatus URLRequestStatus::FromError(int error) {
  if (error == OK) {
    return URLRequestStatus(SUCCESS, OK);
  } else if (error == ERR_IO_PENDING) {
    return URLRequestStatus(IO_PENDING, ERR_IO_PENDING);
  } else if (error == ERR_ABORTED) {
    return URLRequestStatus(CANCELED, ERR_ABORTED);
  } else {
    return URLRequestStatus(FAILED, error);
  }
}

Error URLRequestStatus::ToNetError() const {
  switch (status_) {
    case SUCCESS:
      return OK;
    case IO_PENDING:
      return ERR_IO_PENDING;
    case CANCELED:
      return ERR_ABORTED;
    case FAILED:
      return static_cast<Error>(error_);
  }
  NOTREACHED();
  return ERR_FAILED;
}

}  // namespace net
