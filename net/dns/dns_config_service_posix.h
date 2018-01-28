// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_CONFIG_SERVICE_POSIX_H_
#define NET_DNS_DNS_CONFIG_SERVICE_POSIX_H_

#if !defined(OS_ANDROID)
#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
#endif

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/dns_config_service.h"

namespace base {
class Time;
}  // namespace base

namespace net {

// Use DnsConfigService::CreateSystemService to use it outside of tests.
namespace internal {

// Note: On Android NetworkChangeNotifier::OnNetworkChanged() signals must be
// passed in via calls to OnNetworkChanged().
class NET_EXPORT_PRIVATE DnsConfigServicePosix : public DnsConfigService {
 public:
  DnsConfigServicePosix();
  ~DnsConfigServicePosix() override;

#if defined(OS_ANDROID)
  // Returns whether the DnsConfigServicePosix witnessed a DNS configuration
  // change since |since_time|.  Requires that callers have started listening
  // for NetworkChangeNotifier::OnNetworkChanged() signals, and passing them in
  // via OnNetworkChanged(), prior to |since_time|.
  bool SeenChangeSince(const base::Time& since_time) const;
  // NetworkChangeNotifier::OnNetworkChanged() signals must be passed
  // in via calls to OnNetworkChanged().  Allowing external sources of
  // this signal allows users of DnsConfigServicePosix to start watching for
  // NetworkChangeNotifier::OnNetworkChanged() signals prior to the
  // DnsConfigServicePosix even being created.
  void OnNetworkChanged(NetworkChangeNotifier::ConnectionType type);
#endif

  void SetDnsConfigForTesting(const DnsConfig* dns_config);
  void SetHostsFilePathForTesting(const base::FilePath::CharType* file_path);

 protected:
  // DnsConfigService:
  void ReadNow() override;
  bool StartWatching() override;

 private:
  class Watcher;
  class ConfigReader;
  class HostsReader;

  void OnConfigChanged(bool succeeded);
  void OnHostsChanged(bool succeeded);

  std::unique_ptr<Watcher> watcher_;
  // Allow a mock hosts file for testing purposes.
  const base::FilePath::CharType* file_path_hosts_;
  // Allow a mock DNS server for testing purposes.
  const DnsConfig* dns_config_for_testing_;
  scoped_refptr<ConfigReader> config_reader_;
  scoped_refptr<HostsReader> hosts_reader_;
#if defined(OS_ANDROID)
  // Has DnsConfigWatcher detected any config changes yet?
  bool seen_config_change_;
#endif

  DISALLOW_COPY_AND_ASSIGN(DnsConfigServicePosix);
};

enum ConfigParsePosixResult {
  CONFIG_PARSE_POSIX_OK = 0,
  CONFIG_PARSE_POSIX_RES_INIT_FAILED,
  CONFIG_PARSE_POSIX_RES_INIT_UNSET,
  CONFIG_PARSE_POSIX_BAD_ADDRESS,
  CONFIG_PARSE_POSIX_BAD_EXT_STRUCT,
  CONFIG_PARSE_POSIX_NULL_ADDRESS,
  CONFIG_PARSE_POSIX_NO_NAMESERVERS,
  CONFIG_PARSE_POSIX_MISSING_OPTIONS,
  CONFIG_PARSE_POSIX_UNHANDLED_OPTIONS,
  CONFIG_PARSE_POSIX_NO_DNSINFO,
  CONFIG_PARSE_POSIX_MAX  // Bounding values for enumeration.
};

#if !defined(OS_ANDROID)
// Fills in |dns_config| from |res|.
ConfigParsePosixResult NET_EXPORT_PRIVATE ConvertResStateToDnsConfig(
    const struct __res_state& res, DnsConfig* dns_config);
#endif

}  // namespace internal

}  // namespace net

#endif  // NET_DNS_DNS_CONFIG_SERVICE_POSIX_H_
