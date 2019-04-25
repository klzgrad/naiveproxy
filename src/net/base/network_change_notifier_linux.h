// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_CHANGE_NOTIFIER_LINUX_H_
#define NET_BASE_NETWORK_CHANGE_NOTIFIER_LINUX_H_

#include <memory>
#include <unordered_set>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"

namespace net {

class NET_EXPORT_PRIVATE NetworkChangeNotifierLinux
    : public NetworkChangeNotifier {
 public:
  // Creates NetworkChangeNotifierLinux with a list of ignored interfaces.
  // |ignored_interfaces| is the list of interfaces to ignore. An ignored
  // interface will not trigger IP address or connection type notifications.
  // NOTE: Only ignore interfaces not used to connect to the internet. Adding
  // interfaces used to connect to the internet can cause critical network
  // changed signals to be lost allowing incorrect stale state to persist.
  explicit NetworkChangeNotifierLinux(
      const std::unordered_set<std::string>& ignored_interfaces);

 private:
  class Thread;

  ~NetworkChangeNotifierLinux() override;
  static NetworkChangeCalculatorParams NetworkChangeCalculatorParamsLinux();

  // NetworkChangeNotifier:
  ConnectionType GetCurrentConnectionType() const override;

  const internal::AddressTrackerLinux* GetAddressTrackerInternal()
      const override;

  // The thread used to listen for notifications.  This relays the notification
  // to the registered observers without posting back to the thread the object
  // was created on.
  // Also used for DnsConfigService which requires TYPE_IO message loop.
  std::unique_ptr<Thread> notifier_thread_;

  DISALLOW_COPY_AND_ASSIGN(NetworkChangeNotifierLinux);
};

}  // namespace net

#endif  // NET_BASE_NETWORK_CHANGE_NOTIFIER_LINUX_H_
