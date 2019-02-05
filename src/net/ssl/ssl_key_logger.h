// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_KEY_LOGGER_H_
#define NET_SSL_SSL_KEY_LOGGER_H_

#include <string>

#include "net/base/net_export.h"

namespace net {

// SSLKeyLogger logs SSL key material for debugging purposes. This should only
// be used when requested by the user, typically via the SSLKEYLOGFILE
// environment variable. See also
// https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSS/Key_Log_Format.
class NET_EXPORT SSLKeyLogger {
 public:
  virtual ~SSLKeyLogger() {}

  // Writes |line| followed by a newline. This may be called by multiple threads
  // simultaneously. If two calls race, the order of the lines is undefined, but
  // each line will be written atomically.
  virtual void WriteLine(const std::string& line) = 0;
};

}  // namespace net

#endif  // NET_SSL_SSL_KEY_LOGGER_H_
