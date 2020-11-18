// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_CONFIG_WATCHER_MAC_H_
#define NET_DNS_DNS_CONFIG_WATCHER_MAC_H_

#include "base/callback_forward.h"
#include "net/dns/dns_config_service_posix.h"
#include "net/dns/notify_watcher_mac.h"

namespace net {
namespace internal {

// Watches DNS configuration on Mac.
class DnsConfigWatcher {
 public:
  bool Watch(const base::RepeatingCallback<void(bool succeeded)>& callback);

  // Returns an error if the DNS configuration is invalid.
  // Returns CONFIG_PARSE_POSIX_OK otherwise.
  static ConfigParsePosixResult CheckDnsConfig();

 private:
  NotifyWatcherMac watcher_;
};

}  // namespace internal
}  // namespace net

#endif  // NET_DNS_DNS_CONFIG_WATCHER_MAC_H_
