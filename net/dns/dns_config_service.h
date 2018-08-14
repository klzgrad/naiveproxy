// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_CONFIG_SERVICE_H_
#define NET_DNS_DNS_CONFIG_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
// Needed on shared build with MSVS2010 to avoid multiple definitions of
// std::vector<IPEndPoint>.
#include "net/base/address_list.h"
#include "net/base/ip_endpoint.h"  // win requires size of IPEndPoint
#include "net/base/net_export.h"
#include "net/dns/dns_hosts.h"
#include "url/gurl.h"

namespace base {
class Value;
}

namespace net {

// Default to 1 second timeout (before exponential backoff).
const int64_t kDnsDefaultTimeoutMs = 1000;

// DnsConfig stores configuration of the system resolver.
struct NET_EXPORT DnsConfig {
  DnsConfig();
  DnsConfig(const DnsConfig& other);
  ~DnsConfig();

  bool Equals(const DnsConfig& d) const;

  bool EqualsIgnoreHosts(const DnsConfig& d) const;

  void CopyIgnoreHosts(const DnsConfig& src);

  // Returns a Value representation of |this|. For performance reasons, the
  // Value only contains the number of hosts rather than the full list.
  std::unique_ptr<base::Value> ToValue() const;

  bool IsValid() const {
    return !nameservers.empty();
  }

  struct NET_EXPORT DnsOverHttpsServerConfig {
    DnsOverHttpsServerConfig(const GURL& server, bool use_post);

    GURL server;
    bool use_post;
  };

  // List of name server addresses.
  std::vector<IPEndPoint> nameservers;
  // Suffix search list; used on first lookup when number of dots in given name
  // is less than |ndots|.
  std::vector<std::string> search;

  DnsHosts hosts;

  // True if there are options set in the system configuration that are not yet
  // supported by DnsClient.
  bool unhandled_options;

  // AppendToMultiLabelName: is suffix search performed for multi-label names?
  // True, except on Windows where it can be configured.
  bool append_to_multi_label_name;

  // Indicates that source port randomization is required. This uses additional
  // resources on some platforms.
  bool randomize_ports;

  // Resolver options; see man resolv.conf.

  // Minimum number of dots before global resolution precedes |search|.
  int ndots;
  // Time between retransmissions, see res_state.retrans.
  base::TimeDelta timeout;
  // Maximum number of attempts, see res_state.retry.
  int attempts;
  // Round robin entries in |nameservers| for subsequent requests.
  bool rotate;

  // Indicates system configuration uses local IPv6 connectivity, e.g.,
  // DirectAccess. This is exposed for HostResolver to skip IPv6 probes,
  // as it may cause them to return incorrect results.
  bool use_local_ipv6;

  // List of servers to query over HTTPS, queried in order
  // (https://tools.ietf.org/id/draft-ietf-doh-dns-over-https-02.txt).
  std::vector<DnsOverHttpsServerConfig> dns_over_https_servers;
};

// Service for reading system DNS settings, on demand or when signalled by
// internal watchers and NetworkChangeNotifier.
class NET_EXPORT_PRIVATE DnsConfigService {
 public:
  // Callback interface for the client, called on the same thread as
  // ReadConfig() and WatchConfig().
  typedef base::Callback<void(const DnsConfig& config)> CallbackType;

  // Creates the platform-specific DnsConfigService.
  static std::unique_ptr<DnsConfigService> CreateSystemService();

  DnsConfigService();
  virtual ~DnsConfigService();

  // Attempts to read the configuration. Will run |callback| when succeeded.
  // Can be called at most once.
  void ReadConfig(const CallbackType& callback);

  // Registers systems watchers. Will attempt to read config after watch starts,
  // but only if watchers started successfully. Will run |callback| iff config
  // changes from last call or has to be withdrawn. Can be called at most once.
  // Might require MessageLoopForIO.
  void WatchConfig(const CallbackType& callback);

 protected:
  enum WatchStatus {
    DNS_CONFIG_WATCH_STARTED = 0,
    DNS_CONFIG_WATCH_FAILED_TO_START_CONFIG,
    DNS_CONFIG_WATCH_FAILED_TO_START_HOSTS,
    DNS_CONFIG_WATCH_FAILED_CONFIG,
    DNS_CONFIG_WATCH_FAILED_HOSTS,
    DNS_CONFIG_WATCH_MAX,
  };

  // Immediately attempts to read the current configuration.
  virtual void ReadNow() = 0;
  // Registers system watchers. Returns true iff succeeds.
  virtual bool StartWatching() = 0;

  // Called when the current config (except hosts) has changed.
  void InvalidateConfig();
  // Called when the current hosts have changed.
  void InvalidateHosts();

  // Called with new config. |config|.hosts is ignored.
  void OnConfigRead(const DnsConfig& config);
  // Called with new hosts. Rest of the config is assumed unchanged.
  void OnHostsRead(const DnsHosts& hosts);

  void set_watch_failed(bool value) { watch_failed_ = value; }

  THREAD_CHECKER(thread_checker_);

 private:
  // The timer counts from the last Invalidate* until complete config is read.
  void StartTimer();
  void OnTimeout();
  // Called when the config becomes complete. Stops the timer.
  void OnCompleteConfig();

  CallbackType callback_;

  DnsConfig dns_config_;

  // True if any of the necessary watchers failed. In that case, the service
  // will communicate changes via OnTimeout, but will only send empty DnsConfig.
  bool watch_failed_;
  // True after On*Read, before Invalidate*. Tells if the config is complete.
  bool have_config_;
  bool have_hosts_;
  // True if receiver needs to be updated when the config becomes complete.
  bool need_update_;
  // True if the last config sent was empty (instead of |dns_config_|).
  // Set when |timer_| expires.
  bool last_sent_empty_;

  // Initialized and updated on Invalidate* call.
  base::TimeTicks last_invalidate_config_time_;
  base::TimeTicks last_invalidate_hosts_time_;
  // Initialized and updated when |timer_| expires.
  base::TimeTicks last_sent_empty_time_;

  // Started in Invalidate*, cleared in On*Read.
  base::OneShotTimer timer_;

  DISALLOW_COPY_AND_ASSIGN(DnsConfigService);
};

}  // namespace net

#endif  // NET_DNS_DNS_CONFIG_SERVICE_H_
