// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A class to read incoming QUIC packets from the UDP socket.

#ifndef NET_TOOLS_QUIC_QUIC_PACKET_READER_H_
#define NET_TOOLS_QUIC_QUIC_PACKET_READER_H_

#include <netinet/in.h>
// Include here to guarantee this header gets included (for MSG_WAITFORONE)
// regardless of how the below transitive header include set may change.
#include <sys/socket.h>

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"
#include "net/tools/quic/quic_process_packet_interface.h"

#define MMSG_MORE 0

namespace net {

#if MMSG_MORE
// Read in larger batches to minimize recvmmsg overhead.
const int kNumPacketsPerReadMmsgCall = 16;
#endif

class QuicPacketReader {
 public:
  QuicPacketReader();

  virtual ~QuicPacketReader();

  // Reads a number of packets from the given fd, and then passes them off to
  // the PacketProcessInterface.  Returns true if there may be additional
  // packets available on the socket.
  // Populates |packets_dropped| if it is non-null and the socket is configured
  // to track dropped packets and some packets are read.
  // If the socket has timestamping enabled, the per packet timestamps will be
  // passed to the processor. Otherwise, |clock| will be used.
  virtual bool ReadAndDispatchPackets(int fd,
                                      int port,
                                      const QuicClock& clock,
                                      ProcessPacketInterface* processor,
                                      QuicPacketCount* packets_dropped);

 private:
  // Initialize the internal state of the reader.
  void Initialize();

  // Reads and dispatches many packets using recvmmsg.
  bool ReadAndDispatchManyPackets(int fd,
                                  int port,
                                  const QuicClock& clock,
                                  ProcessPacketInterface* processor,
                                  QuicPacketCount* packets_dropped);

  // Reads and dispatches a single packet using recvmsg.
  static bool ReadAndDispatchSinglePacket(int fd,
                                          int port,
                                          const QuicClock& clock,
                                          ProcessPacketInterface* processor,
                                          QuicPacketCount* packets_dropped);

  // Storage only used when recvmmsg is available.

#if MMSG_MORE
  // TODO(danzh): change it to be a pointer to avoid the allocation on the stack
  // from exceeding maximum allowed frame size.
  // packets_ and mmsg_hdr_ are used to supply cbuf and buf to the recvmmsg
  // call.

  struct PacketData {
    iovec iov;
    // raw_address is used for address information provided by the recvmmsg
    // call on the packets.
    struct sockaddr_storage raw_address;
    // cbuf is used for ancillary data from the kernel on recvmmsg.
    char cbuf[QuicSocketUtils::kSpaceForCmsg];
    // buf is used for the data read from the kernel on recvmmsg.
    char buf[kMaxPacketSize];
  };
  PacketData packets_[kNumPacketsPerReadMmsgCall];
  mmsghdr mmsg_hdr_[kNumPacketsPerReadMmsgCall];
#endif

  DISALLOW_COPY_AND_ASSIGN(QuicPacketReader);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_PACKET_READER_H_
