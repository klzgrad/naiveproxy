// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_config_service_defaults.h"

namespace net {

SSLConfigServiceDefaults::SSLConfigServiceDefaults() = default;

void SSLConfigServiceDefaults::GetSSLConfig(SSLConfig* config) {
  *config = default_config_;
}

SSLConfigServiceDefaults::~SSLConfigServiceDefaults() = default;

}  // namespace net
