// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_interfaces.h"

namespace net {

bool GetNetworkList(NetworkInterfaceList* networks, int policy) {
  NOTIMPLEMENTED();
  return false;
}

std::string GetWifiSSID() {
  NOTIMPLEMENTED();
  return std::string();
}

WifiPHYLayerProtocol GetWifiPHYLayerProtocol() {
  return WIFI_PHY_LAYER_PROTOCOL_UNKNOWN;
}

std::unique_ptr<ScopedWifiOptions> SetWifiOptions(int options) {
  return nullptr;
}

}  // namespace net
