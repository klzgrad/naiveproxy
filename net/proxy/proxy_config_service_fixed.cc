// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_config_service_fixed.h"

namespace net {

ProxyConfigServiceFixed::ProxyConfigServiceFixed(const ProxyConfig& pc)
    : pc_(pc) {
}

ProxyConfigServiceFixed::~ProxyConfigServiceFixed() {}

ProxyConfigService::ConfigAvailability
    ProxyConfigServiceFixed::GetLatestProxyConfig(ProxyConfig* config) {
  *config = pc_;
  return CONFIG_VALID;
}

}  // namespace net
