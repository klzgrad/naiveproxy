// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socket_net_log_params.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/values.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_endpoint.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_with_source.h"

namespace net {

base::Value NetLogSocketErrorParams(int net_error, int os_error) {
  base::DictionaryValue dict;
  dict.SetInteger("net_error", net_error);
  dict.SetInteger("os_error", os_error);
  return std::move(dict);
}

void NetLogSocketError(const NetLogWithSource& net_log,
                       NetLogEventType type,
                       int net_error,
                       int os_error) {
  net_log.AddEvent(
      type, [&] { return NetLogSocketErrorParams(net_error, os_error); });
}

base::Value CreateNetLogHostPortPairParams(const HostPortPair* host_and_port) {
  base::DictionaryValue dict;
  dict.SetString("host_and_port", host_and_port->ToString());
  return std::move(dict);
}

base::Value CreateNetLogIPEndPointParams(const IPEndPoint* address) {
  base::DictionaryValue dict;
  dict.SetString("address", address->ToString());
  return std::move(dict);
}

base::Value CreateNetLogSourceAddressParams(const struct sockaddr* net_address,
                                            socklen_t address_len) {
  base::DictionaryValue dict;
  IPEndPoint ipe;
  bool result = ipe.FromSockAddr(net_address, address_len);
  DCHECK(result);
  dict.SetString("source_address", ipe.ToString());
  return std::move(dict);
}

}  // namespace net
