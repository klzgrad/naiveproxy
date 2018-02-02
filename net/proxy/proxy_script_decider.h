// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_PROXY_SCRIPT_DECIDER_H_
#define NET_PROXY_PROXY_SCRIPT_DECIDER_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/address_list.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/dns/host_resolver.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_resolver.h"
#include "url/gurl.h"

namespace base {
class Value;
}

namespace net {

class DhcpProxyScriptFetcher;
class NetLog;
class NetLogCaptureMode;
class ProxyResolver;
class ProxyScriptFetcher;

// ProxyScriptDecider is a helper class used by ProxyService to determine which
// PAC script to use given our proxy configuration.
//
// This involves trying to use PAC scripts in this order:
//
//   (1) WPAD (DHCP) if auto-detect is on.
//   (2) WPAD (DNS) if auto-detect is on.
//   (3) Custom PAC script if a URL was given.
//
// If no PAC script was successfully selected, then it fails with either a
// network error, or PAC_SCRIPT_FAILED (indicating it did not pass our
// validation).
//
// On successful completion, the fetched PAC script data can be accessed using
// script_data().
//
// Deleting ProxyScriptDecider while Init() is in progress, will
// cancel the request.
//
class NET_EXPORT_PRIVATE ProxyScriptDecider {
 public:
  // |proxy_script_fetcher|, |dhcp_proxy_script_fetcher| and
  // |net_log| must remain valid for the lifespan of ProxyScriptDecider.
  ProxyScriptDecider(ProxyScriptFetcher* proxy_script_fetcher,
                     DhcpProxyScriptFetcher* dhcp_proxy_script_fetcher,
                     NetLog* net_log);

  // Aborts any in-progress request.
  ~ProxyScriptDecider();

  // Evaluates the effective proxy settings for |config|, and downloads the
  // associated PAC script.
  // If |wait_delay| is positive, the initialization will pause for this
  // amount of time before getting started.
  // On successful completion, the "effective" proxy settings we ended up
  // deciding on will be available vial the effective_settings() accessor.
  // Note that this may differ from |config| since we will have stripped any
  // manual settings, and decided whether to use auto-detect or the custom PAC
  // URL. Finally, if auto-detect was used we may now have resolved that to a
  // specific script URL.
  int Start(const ProxyConfig& config,
            const base::TimeDelta wait_delay,
            bool fetch_pac_bytes,
            const CompletionCallback& callback);

  // Shuts down any in-progress DNS requests, and cancels any ScriptFetcher
  // requests.  Does not call OnShutdown on the [Dhcp]ProxyScriptFetcher.
  void OnShutdown();

  const ProxyConfig& effective_config() const;

  const scoped_refptr<ProxyResolverScriptData>& script_data() const;

  void set_quick_check_enabled(bool enabled) {
    quick_check_enabled_ = enabled;
  }

  bool quick_check_enabled() const { return quick_check_enabled_; }

 private:
  // Represents the sources from which we can get PAC files; two types of
  // auto-detect or a custom URL.
  struct PacSource {
    enum Type {
      WPAD_DHCP,
      WPAD_DNS,
      CUSTOM
    };

    PacSource(Type type, const GURL& url)
        : type(type), url(url) {}

    // Returns a Value representing the PacSource.  |effective_pac_url| must
    // be non-NULL and point to the URL derived from information contained in
    // |this|, if Type is not WPAD_DHCP.
    std::unique_ptr<base::Value> NetLogCallback(
        const GURL* effective_pac_url,
        NetLogCaptureMode capture_mode) const;

    Type type;
    GURL url;  // Empty unless |type == PAC_SOURCE_CUSTOM|.
  };

  typedef std::vector<PacSource> PacSourceList;

  enum State {
    STATE_NONE,
    STATE_WAIT,
    STATE_WAIT_COMPLETE,
    STATE_QUICK_CHECK,
    STATE_QUICK_CHECK_COMPLETE,
    STATE_FETCH_PAC_SCRIPT,
    STATE_FETCH_PAC_SCRIPT_COMPLETE,
    STATE_VERIFY_PAC_SCRIPT,
    STATE_VERIFY_PAC_SCRIPT_COMPLETE,
  };

  // Returns ordered list of PAC urls to try for |config|.
  PacSourceList BuildPacSourcesFallbackList(const ProxyConfig& config) const;

  void OnIOCompletion(int result);
  int DoLoop(int result);
  void DoCallback(int result);

  int DoWait();
  int DoWaitComplete(int result);

  int DoQuickCheck();
  int DoQuickCheckComplete(int result);

  int DoFetchPacScript();
  int DoFetchPacScriptComplete(int result);

  int DoVerifyPacScript();
  int DoVerifyPacScriptComplete(int result);

  // Tries restarting using the next fallback PAC URL:
  // |pac_sources_[++current_pac_source_index]|.
  // Returns OK and rewinds the state machine when there
  // is something to try, otherwise returns |error|.
  int TryToFallbackPacSource(int error);

  // Gets the initial state (we skip fetching when the
  // ProxyResolver doesn't |expect_pac_bytes()|.
  State GetStartState() const;

  void DetermineURL(const PacSource& pac_source, GURL* effective_pac_url);

  // Returns the current PAC URL we are fetching/testing.
  const PacSource& current_pac_source() const;

  void OnWaitTimerFired();
  void DidComplete();
  void Cancel();

  ProxyScriptFetcher* proxy_script_fetcher_;
  DhcpProxyScriptFetcher* dhcp_proxy_script_fetcher_;

  CompletionCallback callback_;

  size_t current_pac_source_index_;

  // Filled when the PAC script fetch completes.
  base::string16 pac_script_;

  // Flag indicating whether the caller requested a mandatory pac script
  // (i.e. fallback to direct connections are prohibited).
  bool pac_mandatory_;

  // Whether we have an existing custom PAC URL.
  bool have_custom_pac_url_;

  PacSourceList pac_sources_;
  State next_state_;

  NetLogWithSource net_log_;

  bool fetch_pac_bytes_;

  base::TimeDelta wait_delay_;
  base::OneShotTimer wait_timer_;

  // Whether to do DNS quick check
  bool quick_check_enabled_;

  // Results.
  ProxyConfig effective_config_;
  scoped_refptr<ProxyResolverScriptData> script_data_;

  AddressList wpad_addresses_;
  base::OneShotTimer quick_check_timer_;
  std::unique_ptr<HostResolver::Request> request_;
  base::Time quick_check_start_time_;

  DISALLOW_COPY_AND_ASSIGN(ProxyScriptDecider);
};

}  // namespace net

#endif  // NET_PROXY_PROXY_SCRIPT_DECIDER_H_
