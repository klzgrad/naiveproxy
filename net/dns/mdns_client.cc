// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mdns_client.h"

#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/network_interfaces.h"
#include "net/dns/dns_protocol.h"
#include "net/dns/mdns_client_impl.h"
#include "net/log/net_log.h"
#include "net/log/net_log_source.h"

namespace net {

namespace {

const char kMDnsMulticastGroupIPv4[] = "224.0.0.251";
const char kMDnsMulticastGroupIPv6[] = "FF02::FB";

IPEndPoint GetMDnsIPEndPoint(const char* address) {
  IPAddress multicast_group_number;
  bool success = multicast_group_number.AssignFromIPLiteral(address);
  DCHECK(success);
  return IPEndPoint(multicast_group_number,
                    dns_protocol::kDefaultPortMulticast);
}

int Bind(const IPEndPoint& multicast_addr,
         uint32_t interface_index,
         DatagramServerSocket* socket) {
  IPAddress address_any(IPAddress::AllZeros(multicast_addr.address().size()));
  IPEndPoint bind_endpoint(address_any, multicast_addr.port());

  socket->AllowAddressReuse();
  socket->SetMulticastInterface(interface_index);

  int rv = socket->Listen(bind_endpoint);
  if (rv < OK)
    return rv;

  return socket->JoinGroup(multicast_addr.address());
}

}  // namespace

// static
std::unique_ptr<MDnsSocketFactory> MDnsSocketFactory::CreateDefault() {
  return std::unique_ptr<MDnsSocketFactory>(new MDnsSocketFactoryImpl);
}

// static
std::unique_ptr<MDnsClient> MDnsClient::CreateDefault() {
  return std::unique_ptr<MDnsClient>(new MDnsClientImpl());
}

IPEndPoint GetMDnsIPEndPoint(AddressFamily address_family) {
  switch (address_family) {
    case ADDRESS_FAMILY_IPV4:
      return GetMDnsIPEndPoint(kMDnsMulticastGroupIPv4);
    case ADDRESS_FAMILY_IPV6:
      return GetMDnsIPEndPoint(kMDnsMulticastGroupIPv6);
    default:
      NOTREACHED();
      return IPEndPoint();
  }
}

InterfaceIndexFamilyList GetMDnsInterfacesToBind() {
  NetworkInterfaceList network_list;
  InterfaceIndexFamilyList interfaces;
  if (!GetNetworkList(&network_list, INCLUDE_HOST_SCOPE_VIRTUAL_INTERFACES))
    return interfaces;
  for (size_t i = 0; i < network_list.size(); ++i) {
    AddressFamily family = GetAddressFamily(network_list[i].address);
    if (family == ADDRESS_FAMILY_IPV4 || family == ADDRESS_FAMILY_IPV6) {
      interfaces.push_back(
          std::make_pair(network_list[i].interface_index, family));
    }
  }
  std::sort(interfaces.begin(), interfaces.end());
  // Interfaces could have multiple addresses. Filter out duplicate entries.
  interfaces.erase(std::unique(interfaces.begin(), interfaces.end()),
                   interfaces.end());
  return interfaces;
}

std::unique_ptr<DatagramServerSocket> CreateAndBindMDnsSocket(
    AddressFamily address_family,
    uint32_t interface_index,
    NetLog* net_log) {
  std::unique_ptr<DatagramServerSocket> socket(
      new UDPServerSocket(net_log, NetLogSource()));

  IPEndPoint multicast_addr = GetMDnsIPEndPoint(address_family);
  int rv = Bind(multicast_addr, interface_index, socket.get());
  if (rv != OK) {
    socket.reset();
    VLOG(1) << "Bind failed, endpoint=" << multicast_addr.ToStringWithoutPort()
            << ", error=" << rv;
  }
  return socket;
}

}  // namespace net
