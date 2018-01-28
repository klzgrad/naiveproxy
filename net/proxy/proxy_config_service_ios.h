// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_CONFIG_SERVICE_IOS_H_
#define NET_PROXY_PROXY_CONFIG_SERVICE_IOS_H_

#include "base/macros.h"
#include "net/proxy/polling_proxy_config_service.h"

namespace net {

class ProxyConfigServiceIOS : public PollingProxyConfigService {
 public:
  // Constructs a ProxyConfigService that watches the iOS system proxy settings.
  explicit ProxyConfigServiceIOS();
  ~ProxyConfigServiceIOS() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ProxyConfigServiceIOS);
};

}  // namespace net

#endif  // NET_PROXY_PROXY_CONFIG_SERVICE_IOS_H_
