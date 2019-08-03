#ifndef NET_QUIC_ADDRESS_UTILS_H_
#define NET_QUIC_ADDRESS_UTILS_H_

#include "net/base/ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address_family.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

namespace net {

inline IPEndPoint ToIPEndPoint(quic::QuicSocketAddress address) {
  if (!address.IsInitialized()) {
    return IPEndPoint();
  }

  IPEndPoint result;
  sockaddr_storage storage = address.generic_address();
  const bool success = result.FromSockAddr(
      reinterpret_cast<const sockaddr*>(&storage), sizeof(storage));
  DCHECK(success);
  return result;
}

inline IPAddress ToIPAddress(quic::QuicIpAddress address) {
  if (!address.IsInitialized()) {
    return IPAddress();
  }

  switch (address.address_family()) {
    case quic::IpAddressFamily::IP_V4: {
      in_addr raw_address = address.GetIPv4();
      return IPAddress(reinterpret_cast<const uint8_t*>(&raw_address),
                       sizeof(raw_address));
    }
    case quic::IpAddressFamily::IP_V6: {
      in6_addr raw_address = address.GetIPv6();
      return IPAddress(reinterpret_cast<const uint8_t*>(&raw_address),
                       sizeof(raw_address));
    }
    default:
      DCHECK_EQ(address.address_family(), quic::IpAddressFamily::IP_UNSPEC);
      return IPAddress();
  }
}

}  // namespace net

#endif  // NET_QUIC_ADDRESS_UTILS_H_
