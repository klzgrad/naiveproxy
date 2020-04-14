// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_RESOLVE_CONTEXT_H_
#define NET_DNS_RESOLVE_CONTEXT_H_

#include <vector>

#include "base/optional.h"
#include "net/base/net_export.h"
#include "net/dns/dns_config.h"

namespace net {

class DnsSession;
class URLRequestContext;

// Per-URLRequestContext data used by HostResolver. Expected to be owned by the
// ContextHostResolver, and all usage/references are expected to be cleaned up
// or cancelled before the URLRequestContext goes out of service.
class NET_EXPORT_PRIVATE ResolveContext {
 public:
  explicit ResolveContext(URLRequestContext* url_request_context);

  ResolveContext(const ResolveContext&) = delete;
  ResolveContext& operator=(const ResolveContext&) = delete;

  ~ResolveContext();

  // Sets the "current" session for this ResolveContext and clears all session-
  // specific properties.
  void SetCurrentSession(DnsSession* current_session);

  // Find the index of a DoH server to use for this attempt. Starts from
  // |starting_doh_server_index| and finds the first eligible server (wrapping
  // around as necessary) below failure limits, or of no eligible servers are
  // below failure limits, the one with the oldest last failure. If in AUTOMATIC
  // mode, a server is only eligible after a successful DoH probe. Returns
  // nullopt if there are no eligible DoH servers or |session| is not the
  // current session.
  base::Optional<size_t> DohServerIndexToUse(
      size_t starting_doh_server_index,
      DnsConfig::SecureDnsMode secure_dns_mode,
      const DnsSession* session);

  // Returns the number of DoH servers with successful probe states. Always 0 if
  // |session| is not the current session.
  size_t NumAvailableDohServers(const DnsSession* session) const;

  // Returns whether |doh_server_index| is marked available. Always |false| if
  // |session| is not the current session.
  bool GetDohServerAvailability(size_t doh_server_index,
                                const DnsSession* session) const;

  // Record the latest DoH probe state. Noop if |session| is not the current
  // session.
  void SetProbeSuccess(size_t doh_server_index,
                       bool success,
                       const DnsSession* session);

  URLRequestContext* url_request_context() { return url_request_context_; }

 private:
  bool IsCurrentSession(const DnsSession* session) const;

  URLRequestContext* url_request_context_;

  // Per-session data is only stored and valid for the latest session. Before
  // accessing, should check that |current_session_| is valid and matches a
  // passed in DnsSession.
  //
  // TODO(crbug.com/1022059): Make const DnsSession once server stats have been
  // moved and no longer need to be read from DnsSession for availability logic.
  DnsSession* current_session_;
  std::vector<bool> doh_server_availability_;
};

}  // namespace net

#endif  // NET_DNS_RESOLVE_CONTEXT_H_
