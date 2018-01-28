// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Some socket related helper methods for quic.

#ifndef NET_TOOLS_QUIC_PLATFORM_IMPL_QUIC_SOCKET_UTILS_H_
#define NET_TOOLS_QUIC_PLATFORM_IMPL_QUIC_SOCKET_UTILS_H_

#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>

#include <string>

#include "base/macros.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_types.h"

namespace net {
class QuicIpAddress;
class QuicSocketAddress;

// This is the structure that SO_TIMESTAMPING fills into the cmsg header. It is
// well-defined, but does not have a definition in a public header. See
// https://www.kernel.org/doc/Documentation/networking/timestamping.txt for more
// information.
struct LinuxTimestamping {
  // The converted system time of the timestamp.
  struct timespec systime;
  // Deprecated; serves only as padding.
  struct timespec hwtimetrans;
  // The raw hardware timestamp.
  struct timespec hwtimeraw;
};

class QuicSocketUtils {
 public:
  // The first integer is for overflow. The in6_pktinfo is the larger of the
  // address structures present. LinuxTimestamping is present for socket
  // timestamping.  The subsequent int is for ttl.
  // The final int is a sentinel so the msg_controllen feedback
  // can be used to detect larger control messages than there is space for.
  static const int kSpaceForCmsg =
      CMSG_SPACE(CMSG_LEN(sizeof(int)) + CMSG_LEN(sizeof(in6_pktinfo)) +
                 CMSG_LEN(sizeof(LinuxTimestamping)) +
                 CMSG_LEN(sizeof(int)) +
                 CMSG_LEN(sizeof(int)));

  // Fills in |address| if |hdr| contains IP_PKTINFO or IPV6_PKTINFO. Fills in
  // |timestamp| if |hdr| contains |SO_TIMESTAMPING|. |address| and |timestamp|
  // must not be null.
  static void GetAddressAndTimestampFromMsghdr(struct msghdr* hdr,
                                               QuicIpAddress* address,
                                               QuicWallTime* walltimestamp);

  // If the msghdr contains an SO_RXQ_OVFL entry, this will set dropped_packets
  // to the correct value and return true. Otherwise it will return false.
  static bool GetOverflowFromMsghdr(struct msghdr* hdr,
                                    QuicPacketCount* dropped_packets);

  // If the msghdr contains an IP_TTL entry, this will set ttl to the correct
  // value and return true. Otherwise it will return false.
  static bool GetTtlFromMsghdr(struct msghdr* hdr, int* ttl);

  // Sets either IP_PKTINFO or IPV6_PKTINFO on the socket, based on
  // address_family.  Returns the return code from setsockopt.
  static int SetGetAddressInfo(int fd, int address_family);

  // Sets SO_TIMESTAMPING on the socket for software receive timestamping.
  // Returns the return code from setsockopt.
  static int SetGetSoftwareReceiveTimestamp(int fd);

  // Sets the send buffer size to |size| and returns false if it fails.
  static bool SetSendBufferSize(int fd, size_t size);

  // Sets the receive buffer size to |size| and returns false if it fails.
  static bool SetReceiveBufferSize(int fd, size_t size);

  // Reads buf_len from the socket.  If reading is successful, returns bytes
  // read and sets peer_address to the peer address.  Otherwise returns -1.
  //
  // If dropped_packets is non-null, it will be set to the number of packets
  // dropped on the socket since the socket was created, assuming the kernel
  // supports this feature.
  //
  // If self_address is non-null, it will be set to the address the peer sent
  // packets to, assuming a packet was read.
  //
  // If timestamp is non-null, it will be filled with the timestamp of the
  // received packet, assuming a packet was read and the platform supports
  // packet receipt timestamping. If the platform does not support packet
  // receipt timestamping, timestamp will not be changed.
  static int ReadPacket(int fd,
                        char* buffer,
                        size_t buf_len,
                        QuicPacketCount* dropped_packets,
                        QuicIpAddress* self_address,
                        QuicWallTime* walltimestamp,
                        QuicSocketAddress* peer_address);

  // Writes buf_len to the socket. If writing is successful, sets the result's
  // status to WRITE_STATUS_OK and sets bytes_written.  Otherwise sets the
  // result's status to WRITE_STATUS_BLOCKED or WRITE_STATUS_ERROR and sets
  // error_code to errno.
  static WriteResult WritePacket(int fd,
                                 const char* buffer,
                                 size_t buf_len,
                                 const QuicIpAddress& self_address,
                                 const QuicSocketAddress& peer_address);

  // A helper for WritePacket which fills in the cmsg with the supplied self
  // address.
  // Returns the length of the packet info structure used.
  static size_t SetIpInfoInCmsg(const QuicIpAddress& self_address,
                                cmsghdr* cmsg);

  // Creates a UDP socket and sets appropriate socket options for QUIC.
  // Returns the created FD if successful, -1 otherwise.
  // |overflow_supported| is set to true if the socket supports it.
  static int CreateUDPSocket(const QuicSocketAddress& address,
                             bool* overflow_supported);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicSocketUtils);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_PLATFORM_IMPL_QUIC_SOCKET_UTILS_H_
