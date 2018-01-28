// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_hostname_utils_impl.h"

#include "net/base/url_util.h"
#include "url/gurl.h"
#include "url/url_canon.h"

using std::string;

namespace net {

// static
bool QuicHostnameUtilsImpl::IsValidSNI(QuicStringPiece sni) {
  // TODO(rtenneti): Support RFC2396 hostname.
  // NOTE: Microsoft does NOT enforce this spec, so if we throw away hostnames
  // based on the above spec, we may be losing some hostnames that windows
  // would consider valid. By far the most common hostname character NOT
  // accepted by the above spec is '_'.
  url::CanonHostInfo host_info;
  string canonicalized_host(CanonicalizeHost(sni.as_string(), &host_info));
  return !host_info.IsIPAddress() &&
         IsCanonicalizedHostCompliant(canonicalized_host) &&
         sni.find_last_of('.') != string::npos;
}

// static
char* QuicHostnameUtilsImpl::NormalizeHostname(char* hostname) {
  url::CanonHostInfo host_info;
  string host(CanonicalizeHost(hostname, &host_info));

  // Walk backwards over the string, stopping at the first trailing dot.
  size_t host_end = host.length();
  while (host_end != 0 && host[host_end - 1] == '.') {
    host_end--;
  }

  // Erase the trailing dots.
  if (host_end != host.length()) {
    host.erase(host_end, host.length() - host_end);
  }

  memcpy(hostname, host.c_str(), host.length());
  hostname[host.length()] = '\0';

  return hostname;
}

// static
void QuicHostnameUtilsImpl::StringToQuicServerId(const string& str,
                                                 QuicServerId* out) {
  GURL url(str);
  if (!url.is_valid()) {
    *out = QuicServerId();
    return;
  }
  *out = QuicServerId(HostPortPair::FromURL(url), url.path_piece() == "/private"
                                                      ? PRIVACY_MODE_ENABLED
                                                      : PRIVACY_MODE_DISABLED);
}

}  // namespace net
