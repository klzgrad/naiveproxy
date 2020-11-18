// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A class to read incoming QUIC packets from the UDP socket.

#ifndef QUICHE_QUIC_CORE_QUIC_PACKET_READER_H_
#define QUICHE_QUIC_CORE_QUIC_PACKET_READER_H_

#include "net/third_party/quiche/src/quic/core/quic_clock.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_process_packet_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_udp_socket.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_aligned.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

namespace quic {

// Read in larger batches to minimize recvmmsg overhead.
const int kNumPacketsPerReadMmsgCall = 16;

class QUIC_EXPORT_PRIVATE QuicPacketReader {
 public:
  QuicPacketReader();
  QuicPacketReader(const QuicPacketReader&) = delete;
  QuicPacketReader& operator=(const QuicPacketReader&) = delete;

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
  // Return the self ip from |packet_info|.
  // For dual stack sockets, |packet_info| may contain both a v4 and a v6 ip, in
  // that case, |prefer_v6_ip| is used to determine which one is used as the
  // return value. If neither v4 nor v6 ip exists, return an uninitialized ip.
  static QuicIpAddress GetSelfIpFromPacketInfo(
      const QuicUdpPacketInfo& packet_info,
      bool prefer_v6_ip);

  struct QUIC_EXPORT_PRIVATE ReadBuffer {
    QUIC_CACHELINE_ALIGNED char
        control_buffer[kDefaultUdpPacketControlBufferSize];  // For ancillary
                                                             // data.
    QUIC_CACHELINE_ALIGNED char packet_buffer[kMaxIncomingPacketSize];
  };

  QuicUdpSocketApi socket_api_;
  std::vector<ReadBuffer> read_buffers_;
  QuicUdpSocketApi::ReadPacketResults read_results_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PACKET_READER_H_
