// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_PUBLIC_DNS_QUERY_TYPE_H_
#define NET_DNS_PUBLIC_DNS_QUERY_TYPE_H_

#include "net/base/net_export.h"

namespace net {

// DNS query type for HostResolver requests.
// See:
// https://www.iana.org/assignments/dns-parameters/dns-parameters.xhtml#dns-parameters-4
//
// TODO(crbug.com/846423): Add support for non-address types.
enum class DnsQueryType {
  UNSPECIFIED,
  A,
  AAAA,
};

// |true| iff |dns_query_type| is an address-resulting type, convertable to and
// from net::AddressFamily.
bool NET_EXPORT IsAddressType(DnsQueryType dns_query_type);

}  // namespace net

#endif  // NET_DNS_PUBLIC_DNS_QUERY_TYPE_H_
