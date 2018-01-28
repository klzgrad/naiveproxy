// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_POLLING_PROXY_CONFIG_SERVICE_H_
#define NET_PROXY_POLLING_PROXY_CONFIG_SERVICE_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/proxy/proxy_config_service.h"

namespace net {

// PollingProxyConfigService is a base class for creating ProxyConfigService
// implementations that use polling to notice when settings have change.
//
// It runs code to get the current proxy settings on a background worker
// thread, and notifies registered observers when the value changes.
class NET_EXPORT_PRIVATE PollingProxyConfigService : public ProxyConfigService {
 public:
  // ProxyConfigService implementation:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  ConfigAvailability GetLatestProxyConfig(ProxyConfig* config) override;
  void OnLazyPoll() override;

 protected:
  // Function for retrieving the current proxy configuration.
  // Implementors must be threadsafe as the function will be invoked from
  // worker threads.
  typedef void (*GetConfigFunction)(ProxyConfig*);

  // Creates a polling-based ProxyConfigService which will test for new
  // settings at most every |poll_interval| time by calling |get_config_func|
  // on a worker thread.
  PollingProxyConfigService(
      base::TimeDelta poll_interval,
      GetConfigFunction get_config_func);

  ~PollingProxyConfigService() override;

  // Polls for changes by posting a task to the worker pool.
  void CheckForChangesNow();

 private:
  class Core;
  scoped_refptr<Core> core_;

  DISALLOW_COPY_AND_ASSIGN(PollingProxyConfigService);
};

}  // namespace net

#endif  // NET_PROXY_POLLING_PROXY_CONFIG_SERVICE_H_
