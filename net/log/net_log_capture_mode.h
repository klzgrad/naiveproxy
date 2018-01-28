// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_NET_LOG_CAPTURE_MODE_H_
#define NET_LOG_NET_LOG_CAPTURE_MODE_H_

#include <stdint.h>

#include <string>

#include "net/base/net_export.h"

namespace net {

// NetLogCaptureMode specifies the granularity of events that should be emitted
// to the log. It is a simple wrapper around an integer, so it should be passed
// to functions by value rather than by reference.
class NET_EXPORT NetLogCaptureMode {
 public:
  // NOTE: Default assignment and copy constructor are OK.

  // The default constructor creates a capture mode equivalent to
  // Default().
  NetLogCaptureMode();

  // Constructs a capture mode which logs basic events and event parameters.
  //    include_cookies_and_credentials() --> false
  //    include_socket_bytes() --> false
  static NetLogCaptureMode Default();

  // Constructs a capture mode which logs basic events, and additionally makes
  // no effort to strip cookies and credentials.
  //    include_cookies_and_credentials() --> true
  //    include_socket_bytes() --> false
  // TODO(bnc): Consider renaming to IncludePrivacyInfo().
  static NetLogCaptureMode IncludeCookiesAndCredentials();

  // Constructs a capture mode which logs the data sent/received from sockets.
  //    include_cookies_and_credentials() --> true
  //    include_socket_bytes() --> true
  static NetLogCaptureMode IncludeSocketBytes();

  // If include_cookies_and_credentials() is true , then it is OK to log
  // events which contain cookies, credentials or other privacy sensitive data.
  // TODO(bnc): Consider renaming to include_privacy_info().
  bool include_cookies_and_credentials() const;

  // If include_socket_bytes() is true, then it is OK to output the actual
  // bytes read/written from the network, even if it contains private data.
  bool include_socket_bytes() const;

  bool operator==(NetLogCaptureMode mode) const;
  bool operator!=(NetLogCaptureMode mode) const;

 private:
  explicit NetLogCaptureMode(uint32_t value);

  int32_t value_;
};

}  // namespace net

#endif  // NET_LOG_NET_LOG_CAPTURE_MODE_H_
