// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/net_errors.h"

namespace net {

const char kErrorDomain[] = "net";

std::string ErrorToString(int error) {
  return "net::" + ErrorToShortString(error);
}

std::string ErrorToShortString(int error) {
  if (error == 0)
    return "OK";

  const char* error_string;
  switch (error) {
#define NET_ERROR(label, value) \
  case ERR_ ## label: \
    error_string = # label; \
    break;
#include "net/base/net_error_list.h"
#undef NET_ERROR
  default:
    NOTREACHED();
    error_string = "<unknown>";
  }
  return std::string("ERR_") + error_string;
}

bool IsCertificateError(int error) {
  // Certificate errors are negative integers from net::ERR_CERT_BEGIN
  // (inclusive) to net::ERR_CERT_END (exclusive) in *decreasing* order.
  // ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN is currently an exception to this
  // rule.
  return (error <= ERR_CERT_BEGIN && error > ERR_CERT_END) ||
         (error == ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN);
}

bool IsClientCertificateError(int error) {
  switch (error) {
    case ERR_BAD_SSL_CLIENT_AUTH_CERT:
    case ERR_SSL_CLIENT_AUTH_PRIVATE_KEY_ACCESS_DENIED:
    case ERR_SSL_CLIENT_AUTH_CERT_NO_PRIVATE_KEY:
    case ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED:
      return true;
    default:
      return false;
  }
}

bool IsDnsError(int error) {
  return (error == ERR_NAME_NOT_RESOLVED ||
          error == ERR_NAME_RESOLUTION_FAILED);
}

Error FileErrorToNetError(base::File::Error file_error) {
  switch (file_error) {
    case base::File::FILE_OK:
      return OK;
    case base::File::FILE_ERROR_ACCESS_DENIED:
      return ERR_ACCESS_DENIED;
    case base::File::FILE_ERROR_INVALID_URL:
      return ERR_INVALID_URL;
    case base::File::FILE_ERROR_NOT_FOUND:
      return ERR_FILE_NOT_FOUND;
    default:
      return ERR_FAILED;
  }
}

}  // namespace net
