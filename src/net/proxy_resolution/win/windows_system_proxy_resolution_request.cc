// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/win/windows_system_proxy_resolution_request.h"

namespace net {

WindowsSystemProxyResolutionRequest::WindowsSystemProxyResolutionRequest() =
    default;
WindowsSystemProxyResolutionRequest::~WindowsSystemProxyResolutionRequest() =
    default;

LoadState WindowsSystemProxyResolutionRequest::GetLoadState() const {
  return LOAD_STATE_IDLE;
}

}  // namespace net
