// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_CONFIG_SERVICE_FIXED_H_
#define NET_PROXY_PROXY_CONFIG_SERVICE_FIXED_H_

#include "base/compiler_specific.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_config_service.h"

namespace net {

// Implementation of ProxyConfigService that returns a fixed result.
class NET_EXPORT ProxyConfigServiceFixed : public ProxyConfigService {
 public:
  explicit ProxyConfigServiceFixed(const ProxyConfig& pc);
  ~ProxyConfigServiceFixed() override;

  // ProxyConfigService methods:
  void AddObserver(Observer* observer) override {}
  void RemoveObserver(Observer* observer) override {}
  ConfigAvailability GetLatestProxyConfig(ProxyConfig* config) override;

 private:
  ProxyConfig pc_;
};

}  // namespace net

#endif  // NET_PROXY_PROXY_CONFIG_SERVICE_FIXED_H_
