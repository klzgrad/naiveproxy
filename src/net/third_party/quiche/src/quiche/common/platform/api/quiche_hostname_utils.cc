// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/platform/api/quiche_hostname_utils.h"

#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_googleurl.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

// TODO(vasilvv): the functions below are forked from Chromium's
// net/base/url_util.h; those should be moved to googleurl.
namespace {

std::string CanonicalizeHost(absl::string_view host,
                             url::CanonHostInfo* host_info) {
  // Try to canonicalize the host.
  const url::Component raw_host_component(0, static_cast<int>(host.length()));
  std::string canon_host;
  url::StdStringCanonOutput canon_host_output(&canon_host);
  url::CanonicalizeHostVerbose(host.data(), raw_host_component,
                               &canon_host_output, host_info);

  if (host_info->out_host.is_nonempty() &&
      host_info->family != url::CanonHostInfo::BROKEN) {
    // Success!  Assert that there's no extra garbage.
    canon_host_output.Complete();
    QUICHE_DCHECK_EQ(host_info->out_host.len,
                     static_cast<int>(canon_host.length()));
  } else {
    // Empty host, or canonicalization failed.  We'll return empty.
    canon_host.clear();
  }

  return canon_host;
}

bool IsHostCharAlphanumeric(char c) {
  // We can just check lowercase because uppercase characters have already been
  // normalized.
  return ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9'));
}

bool IsCanonicalizedHostCompliant(const std::string& host) {
  if (host.empty()) {
    return false;
  }

  bool in_component = false;
  bool most_recent_component_started_alphanumeric = false;

  for (char c : host) {
    if (!in_component) {
      most_recent_component_started_alphanumeric = IsHostCharAlphanumeric(c);
      if (!most_recent_component_started_alphanumeric && (c != '-') &&
          (c != '_')) {
        return false;
      }
      in_component = true;
    } else if (c == '.') {
      in_component = false;
    } else if (!IsHostCharAlphanumeric(c) && (c != '-') && (c != '_')) {
      return false;
    }
  }

  return most_recent_component_started_alphanumeric;
}

}  // namespace

// static
bool QuicheHostnameUtils::IsValidSNI(absl::string_view sni) {
  // TODO(rtenneti): Support RFC2396 hostname.
  // NOTE: Microsoft does NOT enforce this spec, so if we throw away hostnames
  // based on the above spec, we may be losing some hostnames that windows
  // would consider valid. By far the most common hostname character NOT
  // accepted by the above spec is '_'.
  url::CanonHostInfo host_info;
  std::string canonicalized_host = CanonicalizeHost(sni, &host_info);
  return !host_info.IsIPAddress() &&
         IsCanonicalizedHostCompliant(canonicalized_host);
}

// static
std::string QuicheHostnameUtils::NormalizeHostname(absl::string_view hostname) {
  url::CanonHostInfo host_info;
  std::string host = CanonicalizeHost(hostname, &host_info);

  // Walk backwards over the string, stopping at the first trailing dot.
  size_t host_end = host.length();
  while (host_end != 0 && host[host_end - 1] == '.') {
    host_end--;
  }

  // Erase the trailing dots.
  if (host_end != host.length()) {
    host.erase(host_end, host.length() - host_end);
  }

  return host;
}

}  // namespace quiche
