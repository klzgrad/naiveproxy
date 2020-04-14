// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/resolve_context.h"

#include "base/logging.h"
#include "base/time/time.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/dns_session.h"

namespace net {

ResolveContext::ResolveContext(URLRequestContext* url_request_context)
    : url_request_context_(url_request_context) {
  DCHECK(url_request_context_);
}

ResolveContext::~ResolveContext() = default;

void ResolveContext::SetCurrentSession(DnsSession* session) {
  DCHECK(!IsCurrentSession(session));

  current_session_ = session;

  doh_server_availability_.clear();
  if (session) {
    doh_server_availability_.insert(
        doh_server_availability_.begin(),
        session->config().dns_over_https_servers.size(), false);
  }
}

base::Optional<size_t> ResolveContext::DohServerIndexToUse(
    size_t starting_doh_server_index,
    DnsConfig::SecureDnsMode secure_dns_mode,
    const DnsSession* session) {
  if (!IsCurrentSession(session))
    return base::nullopt;

  DCHECK_LT(starting_doh_server_index,
            session->config().dns_over_https_servers.size());
  size_t index = starting_doh_server_index;
  base::Time oldest_server_failure;
  base::Optional<size_t> oldest_available_server_failure_index;

  do {
    // For a server to be considered "available", the server must have a
    // successful probe status if we are in AUTOMATIC mode.
    if (secure_dns_mode == DnsConfig::SecureDnsMode::SECURE ||
        doh_server_availability_[index]) {
      // If number of failures on this server doesn't exceed |config_.attempts|,
      // return its index. |config_.attempts| will generally be more restrictive
      // than |kAutomaticModeFailureLimit|, although this is not guaranteed.
      if (current_session_->GetLastDohFailureCount(index) <
          session->config().attempts) {
        return index;
      }
      // Track oldest failed available server.
      base::Time cur_server_failure =
          current_session_->GetLastDohFailure(index);
      if (!oldest_available_server_failure_index ||
          cur_server_failure < oldest_server_failure) {
        oldest_server_failure = cur_server_failure;
        oldest_available_server_failure_index = index;
      }
    }
    index = (index + 1) % session->config().dns_over_https_servers.size();
  } while (index != starting_doh_server_index);

  // If we are here it means that there are either no available DoH servers or
  // that all available DoH servers have at least |config_.attempts| consecutive
  // failures. In the latter case, we'll return the available DoH server that
  // failed least recently. In the former case we return nullopt.
  return oldest_available_server_failure_index;
}

size_t ResolveContext::NumAvailableDohServers(const DnsSession* session) const {
  if (!IsCurrentSession(session))
    return 0;

  size_t count = 0;
  for (const auto& probe_result : doh_server_availability_) {
    if (probe_result)
      count++;
  }
  return count;
}

bool ResolveContext::GetDohServerAvailability(size_t doh_server_index,
                                              const DnsSession* session) const {
  if (!IsCurrentSession(session))
    return false;

  DCHECK_LT(doh_server_index, doh_server_availability_.size());

  return doh_server_availability_[doh_server_index];
}

void ResolveContext::SetProbeSuccess(size_t doh_server_index,
                                     bool success,
                                     const DnsSession* session) {
  if (!IsCurrentSession(session))
    return;

  DCHECK_LT(doh_server_index, session->config().dns_over_https_servers.size());

  bool doh_available_before = NumAvailableDohServers(session) > 0;
  doh_server_availability_[doh_server_index] = success;

  // TODO(crbug.com/1022059): Consider figuring out some way to only for the
  // first context enabling DoH or the last context disabling DoH.
  if (doh_available_before != NumAvailableDohServers(session) > 0)
    NetworkChangeNotifier::TriggerNonSystemDnsChange();
}

bool ResolveContext::IsCurrentSession(const DnsSession* session) const {
  DCHECK(session);
  return session == current_session_;
}

}  // namespace net
