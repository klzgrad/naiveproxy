// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_DHCP_PROXY_SCRIPT_FETCHER_FACTORY_H_
#define NET_PROXY_DHCP_PROXY_SCRIPT_FETCHER_FACTORY_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/singleton.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/proxy/dhcp_proxy_script_fetcher.h"

namespace net {

class URLRequestContext;

// Factory object for creating the appropriate concrete base class of
// DhcpProxyScriptFetcher for your operating system and settings.
//
// You might think we could just implement a DHCP client at the protocol
// level and have cross-platform support for retrieving PAC configuration
// from DHCP, but unfortunately the DHCP protocol assumes there is a single
// client per machine (specifically per network interface card), and there
// is an implicit state machine between the client and server, so adding a
// second client to the machine would not be advisable (see e.g. some
// discussion of what can happen at this URL:
// http://www.net.princeton.edu/multi-dhcp-one-interface-handling.html).
//
// Therefore, we have platform-specific implementations, and so we use
// this factory to select the right one.
class NET_EXPORT DhcpProxyScriptFetcherFactory {
 public:
  DhcpProxyScriptFetcherFactory();

  virtual ~DhcpProxyScriptFetcherFactory();

  // url_request_context must be valid and its lifetime must exceed that of the
  // returned DhcpProxyScriptFetcher.
  //
  // Note that while a request is in progress, the fetcher may be holding a
  // reference to |url_request_context|. Be careful not to create cycles
  // between the fetcher and the context; you can break such cycles by calling
  // Cancel().
  virtual std::unique_ptr<DhcpProxyScriptFetcher> Create(
      URLRequestContext* url_request_context);

 private:
  DISALLOW_COPY_AND_ASSIGN(DhcpProxyScriptFetcherFactory);
};

}  // namespace net

#endif  // NET_PROXY_DHCP_PROXY_SCRIPT_FETCHER_FACTORY_H_
