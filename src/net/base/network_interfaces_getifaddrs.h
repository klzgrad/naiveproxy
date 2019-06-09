// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_INTERFACES_GETIFADDRS_H_
#define NET_BASE_NETWORK_INTERFACES_GETIFADDRS_H_

// network_interfaces_getaddrs.cc implements GetNetworkList() using getifaddrs()
// API. It is a non-standard API, so not all POSIX systems implement it (e.g.
// it doesn't exist on Android). It is used on MacOS, iOS and Fuchsia. On Linux
// and Android interface is used to implement GetNetworkList(), see
// network_interfaces_linux.cc.
// This file defines IfaddrsToNetworkInterfaceList() so it can be called in
// unittests.

#include "net/base/net_export.h"
#include "net/base/network_interfaces.h"

#include <string>

struct ifaddrs;

namespace net {
namespace internal {

class NET_EXPORT_PRIVATE IPAttributesGetter {
 public:
  IPAttributesGetter() {}
  virtual ~IPAttributesGetter() {}
  virtual bool IsInitialized() const = 0;

  // Returns false if the interface must be skipped. Otherwise sets |attributes|
  // and returns true.
  virtual bool GetAddressAttributes(const ifaddrs* if_addr,
                                    int* attributes) = 0;

  // Returns interface type for the given interface.
  virtual NetworkChangeNotifier::ConnectionType GetNetworkInterfaceType(
      const ifaddrs* if_addr) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(IPAttributesGetter);
};

// Converts ifaddrs list returned by getifaddrs() to NetworkInterfaceList. Also
// filters the list interfaces according to |policy| (see
// HostAddressSelectionPolicy).
NET_EXPORT_PRIVATE bool IfaddrsToNetworkInterfaceList(
    int policy,
    const ifaddrs* interfaces,
    IPAttributesGetter* ip_attributes_getter,
    NetworkInterfaceList* networks);

}  // namespace internal
}  // namespace net

#endif  // NET_BASE_NETWORK_INTERFACES_GETIFADDRS_H_
