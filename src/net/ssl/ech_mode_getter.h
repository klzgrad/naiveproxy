// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_ECH_MODE_GETTER_H_
#define NET_SSL_ECH_MODE_GETTER_H_

#include <string_view>

#include "net/base/ech_mode.h"
#include "net/base/net_export.h"

namespace net {

// The interface for retrieving the EchMode.
// Different platforms can provide OS-specific implementations using
// platform APIs.
class NET_EXPORT EchModeGetter {
 public:
  virtual ~EchModeGetter() = default;

  // Returns the desired EchMode for the given `hostname`.
  virtual EchMode GetEchMode(std::string_view hostname) const = 0;
};

}  // namespace net

#endif  // NET_SSL_ECH_MODE_GETTER_H_
