// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_RESOLUTION_WIN_WINDOWS_SYSTEM_PROXY_RESOLUTION_REQUEST_H_
#define NET_PROXY_RESOLUTION_WIN_WINDOWS_SYSTEM_PROXY_RESOLUTION_REQUEST_H_

#include "net/proxy_resolution/proxy_resolution_request.h"

namespace net {

// This is the concrete implementation of ProxyResolutionRequest used by
// WindowsSystemProxyResolutionService. Manages a single asynchronous proxy
// resolution request.
class WindowsSystemProxyResolutionRequest final
    : public ProxyResolutionRequest {
 public:
  WindowsSystemProxyResolutionRequest();

  WindowsSystemProxyResolutionRequest(
      const WindowsSystemProxyResolutionRequest&) = delete;
  WindowsSystemProxyResolutionRequest& operator=(
      const WindowsSystemProxyResolutionRequest&) = delete;

  ~WindowsSystemProxyResolutionRequest() override;

  LoadState GetLoadState() const override;
};

}  // namespace net

#endif  // NET_PROXY_RESOLUTION_WIN_WINDOWS_SYSTEM_PROXY_RESOLUTION_REQUEST_H_
