// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/dhcp_pac_file_fetcher_factory.h"

#include "net/base/net_errors.h"
#include "net/proxy_resolution/dhcp_pac_file_fetcher.h"

#if defined(OS_WIN)
#include "net/proxy_resolution/dhcp_pac_file_fetcher_win.h"
#endif

namespace net {

DhcpPacFileFetcherFactory::DhcpPacFileFetcherFactory() = default;

DhcpPacFileFetcherFactory::~DhcpPacFileFetcherFactory() = default;

std::unique_ptr<DhcpPacFileFetcher> DhcpPacFileFetcherFactory::Create(
    URLRequestContext* context) {
#if defined(OS_WIN)
  return std::make_unique<DhcpPacFileFetcherWin>(context);
#else
  return std::make_unique<DoNothingDhcpPacFileFetcher>();
#endif
}

}  // namespace net
