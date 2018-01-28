// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log_capture_mode.h"

#include <algorithm>

namespace net {

namespace {

// Integer representation for the capture mode. The numeric value is depended on
// for methods of NetLogCaptureMode, which expect that higher values represent a
// strict superset of the capabilities of lower values.
enum InternalValue {
  // Log all events, but do not include the actual transferred bytes, and
  // remove cookies and HTTP credentials and HTTP/2 GOAWAY frame debug data.
  DEFAULT,

  // Log all events, but do not include the actual transferred bytes as
  // parameters for bytes sent/received events.
  // TODO(bnc): Consider renaming to INCLUDE_PRIVACY_INFO.
  INCLUDE_COOKIES_AND_CREDENTIALS,

  // Log everything possible, even if it is slow and memory expensive.
  // Includes logging of transferred bytes.
  INCLUDE_SOCKET_BYTES,
};

}  // namespace

NetLogCaptureMode::NetLogCaptureMode() : NetLogCaptureMode(DEFAULT) {
}

NetLogCaptureMode NetLogCaptureMode::Default() {
  return NetLogCaptureMode(DEFAULT);
}

NetLogCaptureMode NetLogCaptureMode::IncludeCookiesAndCredentials() {
  return NetLogCaptureMode(INCLUDE_COOKIES_AND_CREDENTIALS);
}

NetLogCaptureMode NetLogCaptureMode::IncludeSocketBytes() {
  return NetLogCaptureMode(INCLUDE_SOCKET_BYTES);
}

bool NetLogCaptureMode::include_cookies_and_credentials() const {
  return value_ >= INCLUDE_COOKIES_AND_CREDENTIALS;
}

bool NetLogCaptureMode::include_socket_bytes() const {
  return value_ >= INCLUDE_SOCKET_BYTES;
}

bool NetLogCaptureMode::operator==(NetLogCaptureMode mode) const {
  return value_ == mode.value_;
}

bool NetLogCaptureMode::operator!=(NetLogCaptureMode mode) const {
  return !(*this == mode);
}

NetLogCaptureMode::NetLogCaptureMode(uint32_t value) : value_(value) {
}

}  // namespace net
