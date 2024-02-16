// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_name_lookup.h"

#include <cstring>
#include <string>

#include "absl/strings/str_cat.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else  // else assume POSIX
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace quic::tools {

QuicSocketAddress LookupAddress(int address_family_for_lookup, std::string host,
                                std::string port) {
  addrinfo hint;
  memset(&hint, 0, sizeof(hint));
  hint.ai_family = address_family_for_lookup;
  hint.ai_protocol = IPPROTO_UDP;

  addrinfo* info_list = nullptr;
  int result = getaddrinfo(host.c_str(), port.c_str(), &hint, &info_list);
  if (result != 0) {
    QUIC_LOG(ERROR) << "Failed to look up " << host << ": "
                    << gai_strerror(result);
    return QuicSocketAddress();
  }

  QUICHE_CHECK(info_list != nullptr);
  std::unique_ptr<addrinfo, void (*)(addrinfo*)> info_list_owned(
      info_list, [](addrinfo* ai) { freeaddrinfo(ai); });
  return QuicSocketAddress(info_list->ai_addr, info_list->ai_addrlen);
}

QuicSocketAddress LookupAddress(int address_family_for_lookup,
                                const QuicServerId& server_id) {
  return LookupAddress(address_family_for_lookup,
                       std::string(server_id.GetHostWithoutIpv6Brackets()),
                       absl::StrCat(server_id.port()));
}

}  // namespace quic::tools
