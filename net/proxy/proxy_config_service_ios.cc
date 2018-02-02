// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_config_service_ios.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFProxySupport.h>

#include "base/mac/foundation_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/sys_string_conversions.h"
#include "net/proxy/proxy_config.h"

namespace net {

namespace {

const int kPollIntervalSec = 10;

// Utility function to pull out a boolean value from a dictionary and return it,
// returning a default value if the key is not present.
bool GetBoolFromDictionary(CFDictionaryRef dict,
                           CFStringRef key,
                           bool default_value) {
  CFNumberRef number =
      base::mac::GetValueFromDictionary<CFNumberRef>(dict, key);
  if (!number)
    return default_value;

  int int_value;
  if (CFNumberGetValue(number, kCFNumberIntType, &int_value))
    return int_value;
  else
    return default_value;
}

void GetCurrentProxyConfig(ProxyConfig* config) {
  base::ScopedCFTypeRef<CFDictionaryRef> config_dict(
      CFNetworkCopySystemProxySettings());
  DCHECK(config_dict);

  // Auto-detect is not supported.
  // The kCFNetworkProxiesProxyAutoDiscoveryEnable key is not available on iOS.

  // PAC file

  if (GetBoolFromDictionary(config_dict.get(),
                            kCFNetworkProxiesProxyAutoConfigEnable,
                            false)) {
    CFStringRef pac_url_ref = base::mac::GetValueFromDictionary<CFStringRef>(
        config_dict.get(), kCFNetworkProxiesProxyAutoConfigURLString);
    if (pac_url_ref)
      config->set_pac_url(GURL(base::SysCFStringRefToUTF8(pac_url_ref)));
  }

  // Proxies (for now http).

  // The following keys are not available on iOS:
  //   kCFNetworkProxiesFTPEnable
  //   kCFNetworkProxiesFTPProxy
  //   kCFNetworkProxiesFTPPort
  //   kCFNetworkProxiesHTTPSEnable
  //   kCFNetworkProxiesHTTPSProxy
  //   kCFNetworkProxiesHTTPSPort
  //   kCFNetworkProxiesSOCKSEnable
  //   kCFNetworkProxiesSOCKSProxy
  //   kCFNetworkProxiesSOCKSPort
  if (GetBoolFromDictionary(config_dict.get(),
                            kCFNetworkProxiesHTTPEnable,
                            false)) {
    ProxyServer proxy_server =
        ProxyServer::FromDictionary(ProxyServer::SCHEME_HTTP,
                                    config_dict.get(),
                                    kCFNetworkProxiesHTTPProxy,
                                    kCFNetworkProxiesHTTPPort);
    if (proxy_server.is_valid()) {
      config->proxy_rules().type =
          ProxyConfig::ProxyRules::TYPE_PROXY_PER_SCHEME;
      config->proxy_rules().proxies_for_http.SetSingleProxyServer(proxy_server);
      // Desktop Safari applies the HTTP proxy to http:// URLs only, but
      // Mobile Safari applies the HTTP proxy to https:// URLs as well.
      config->proxy_rules().proxies_for_https.SetSingleProxyServer(
          proxy_server);
    }
  }

  // Proxy bypass list is not supported.
  // The kCFNetworkProxiesExceptionsList key is not available on iOS.

  // Proxy bypass boolean is not supported.
  // The kCFNetworkProxiesExcludeSimpleHostnames key is not available on iOS.

  // Source
  config->set_source(PROXY_CONFIG_SOURCE_SYSTEM);
}

}  // namespace

ProxyConfigServiceIOS::ProxyConfigServiceIOS()
    : PollingProxyConfigService(base::TimeDelta::FromSeconds(kPollIntervalSec),
                                GetCurrentProxyConfig) {
}

ProxyConfigServiceIOS::~ProxyConfigServiceIOS() {
}

}  // namespace net
