// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_CONFIG_SERVICE_DEFAULTS_H_
#define NET_SSL_SSL_CONFIG_SERVICE_DEFAULTS_H_

#include <string_view>

#include "net/base/net_export.h"
#include "net/ssl/ssl_config_service.h"

namespace net {

// This SSLConfigService always returns the default SSLConfig settings.  It is
// mainly useful for unittests, or for platforms that do not have a native
// implementation of SSLConfigService yet.
class NET_EXPORT SSLConfigServiceDefaults : public SSLConfigService {
 public:
  SSLConfigServiceDefaults();

  SSLConfigServiceDefaults(const SSLConfigServiceDefaults&) = delete;
  SSLConfigServiceDefaults& operator=(const SSLConfigServiceDefaults&) = delete;

  ~SSLConfigServiceDefaults() override;

  // Returns the default SSL config settings.
  SSLContextConfig GetSSLContextConfig() override;

  bool CanShareConnectionWithClientCerts(
      std::string_view hostname) const override;

 private:
  // Default value of prefs.
  const SSLContextConfig default_config_;
};

}  // namespace net

#endif  // NET_SSL_SSL_CONFIG_SERVICE_DEFAULTS_H_
