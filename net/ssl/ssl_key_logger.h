// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_KEY_LOGGER_H_
#define NET_SSL_SSL_KEY_LOGGER_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"

namespace base {
class FilePath;
class SequencedTaskRunner;
}

namespace net {

// SSLKeyLogger logs SSL key material for debugging purposes. This should only
// be used when requested by the user, typically via the SSLKEYLOGFILE
// environment variable. See also
// https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSS/Key_Log_Format.
class SSLKeyLogger {
 public:
  // Creates a new SSLKeyLogger which writes to |path|, scheduling write
  // operations in the background.
  explicit SSLKeyLogger(const base::FilePath& path);
  ~SSLKeyLogger();

  // Writes |line| followed by a newline. This may be called by multiple threads
  // simultaneously. If two calls race, the order of the lines is undefined, but
  // each line will be written atomically.
  void WriteLine(const std::string& line);

 private:
  class Core;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  // Destroyed on |task_runner_|.
  std::unique_ptr<Core> core_;

  DISALLOW_COPY_AND_ASSIGN(SSLKeyLogger);
};

}  // namespace net

#endif  // NET_SSL_SSL_KEY_LOGGER_H_
