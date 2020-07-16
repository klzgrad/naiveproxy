// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/proxy_server.h"

#include <CoreFoundation/CoreFoundation.h>

#include <string>

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/strings/sys_string_conversions.h"

namespace net {

// static
ProxyServer ProxyServer::FromDictionary(Scheme scheme,
                                        CFDictionaryRef dict,
                                        CFStringRef host_key,
                                        CFStringRef port_key) {
  if (scheme == SCHEME_INVALID || scheme == SCHEME_DIRECT) {
    // No hostname port to extract; we are done.
    return ProxyServer(scheme, HostPortPair());
  }

  CFStringRef host_ref =
      base::mac::GetValueFromDictionary<CFStringRef>(dict, host_key);
  if (!host_ref) {
    LOG(WARNING) << "Could not find expected key "
                 << base::SysCFStringRefToUTF8(host_key)
                 << " in the proxy dictionary";
    return ProxyServer();  // Invalid.
  }
  std::string host = base::SysCFStringRefToUTF8(host_ref);

  CFNumberRef port_ref =
      base::mac::GetValueFromDictionary<CFNumberRef>(dict, port_key);
  int port;
  if (port_ref) {
    CFNumberGetValue(port_ref, kCFNumberIntType, &port);
  } else {
    port = GetDefaultPortForScheme(scheme);
  }

  return ProxyServer(scheme, HostPortPair(host, port));
}

}  // namespace net
