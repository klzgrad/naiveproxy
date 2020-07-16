// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_UDP_SOCKET_H_
#define QUICHE_QUIC_CORE_QUIC_UDP_SOCKET_H_

#include <cstddef>
#include <cstdint>

#include <type_traits>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

namespace quic {

#if defined(_WIN32)
using QuicUdpSocketFd = SOCKET;
const QuicUdpSocketFd kQuicInvalidSocketFd = INVALID_SOCKET;
#else
using QuicUdpSocketFd = int;
const QuicUdpSocketFd kQuicInvalidSocketFd = -1;
#endif

const size_t kDefaultUdpPacketControlBufferSize = 512;

enum class QuicUdpPacketInfoBit : uint8_t {
  DROPPED_PACKETS = 0,   // Read
  V4_SELF_IP,            // Read
  V6_SELF_IP,            // Read
  PEER_ADDRESS,          // Read & Write
  RECV_TIMESTAMP,        // Read
  TTL,                   // Read & Write
  GOOGLE_PACKET_HEADER,  // Read
  NUM_BITS,
};
static_assert(static_cast<size_t>(QuicUdpPacketInfoBit::NUM_BITS) <=
                  BitMask64::NumBits(),
              "BitMask64 not wide enough to hold all bits.");

// BufferSpan points to an unowned buffer, copying this structure only copies
// the pointer and length, not the buffer itself.
struct QUIC_EXPORT_PRIVATE BufferSpan {
  BufferSpan(char* buffer, size_t buffer_len)
      : buffer(buffer), buffer_len(buffer_len) {}

  BufferSpan() = default;
  BufferSpan(const BufferSpan& other) = default;
  BufferSpan& operator=(const BufferSpan& other) = default;

  char* buffer = nullptr;
  size_t buffer_len = 0;
};

// QuicUdpPacketInfo contains per-packet information used for sending and
// receiving.
class QUIC_EXPORT_PRIVATE QuicUdpPacketInfo {
 public:
  BitMask64 bitmask() const { return bitmask_; }

  void Reset() { bitmask_.ClearAll(); }

  bool HasValue(QuicUdpPacketInfoBit bit) const { return bitmask_.IsSet(bit); }

  QuicPacketCount dropped_packets() const {
    DCHECK(HasValue(QuicUdpPacketInfoBit::DROPPED_PACKETS));
    return dropped_packets_;
  }

  void SetDroppedPackets(QuicPacketCount dropped_packets) {
    dropped_packets_ = dropped_packets;
    bitmask_.Set(QuicUdpPacketInfoBit::DROPPED_PACKETS);
  }

  const QuicIpAddress& self_v4_ip() const {
    DCHECK(HasValue(QuicUdpPacketInfoBit::V4_SELF_IP));
    return self_v4_ip_;
  }

  void SetSelfV4Ip(QuicIpAddress self_v4_ip) {
    self_v4_ip_ = self_v4_ip;
    bitmask_.Set(QuicUdpPacketInfoBit::V4_SELF_IP);
  }

  const QuicIpAddress& self_v6_ip() const {
    DCHECK(HasValue(QuicUdpPacketInfoBit::V6_SELF_IP));
    return self_v6_ip_;
  }

  void SetSelfV6Ip(QuicIpAddress self_v6_ip) {
    self_v6_ip_ = self_v6_ip;
    bitmask_.Set(QuicUdpPacketInfoBit::V6_SELF_IP);
  }

  void SetSelfIp(QuicIpAddress self_ip) {
    if (self_ip.IsIPv4()) {
      SetSelfV4Ip(self_ip);
    } else {
      SetSelfV6Ip(self_ip);
    }
  }

  const QuicSocketAddress& peer_address() const {
    DCHECK(HasValue(QuicUdpPacketInfoBit::PEER_ADDRESS));
    return peer_address_;
  }

  void SetPeerAddress(QuicSocketAddress peer_address) {
    peer_address_ = peer_address;
    bitmask_.Set(QuicUdpPacketInfoBit::PEER_ADDRESS);
  }

  QuicWallTime receive_timestamp() const {
    DCHECK(HasValue(QuicUdpPacketInfoBit::RECV_TIMESTAMP));
    return receive_timestamp_;
  }

  void SetReceiveTimestamp(QuicWallTime receive_timestamp) {
    receive_timestamp_ = receive_timestamp;
    bitmask_.Set(QuicUdpPacketInfoBit::RECV_TIMESTAMP);
  }

  int ttl() const {
    DCHECK(HasValue(QuicUdpPacketInfoBit::TTL));
    return ttl_;
  }

  void SetTtl(int ttl) {
    ttl_ = ttl;
    bitmask_.Set(QuicUdpPacketInfoBit::TTL);
  }

  BufferSpan google_packet_headers() const {
    DCHECK(HasValue(QuicUdpPacketInfoBit::GOOGLE_PACKET_HEADER));
    return google_packet_headers_;
  }

  void SetGooglePacketHeaders(BufferSpan google_packet_headers) {
    google_packet_headers_ = google_packet_headers;
    bitmask_.Set(QuicUdpPacketInfoBit::GOOGLE_PACKET_HEADER);
  }

 private:
  BitMask64 bitmask_;
  QuicPacketCount dropped_packets_;
  QuicIpAddress self_v4_ip_;
  QuicIpAddress self_v6_ip_;
  QuicSocketAddress peer_address_;
  QuicWallTime receive_timestamp_ = QuicWallTime::Zero();
  int ttl_;
  BufferSpan google_packet_headers_;
};

// QuicUdpSocketApi provides a minimal set of apis for sending and receiving
// udp packets. The low level udp socket apis differ between kernels and kernel
// versions, the goal of QuicUdpSocketApi is to hide such differences.
// We use non-static functions because it is easier to be mocked in tests when
// needed.
class QUIC_EXPORT_PRIVATE QuicUdpSocketApi {
 public:
  // Creates a non-blocking udp socket, sets the receive/send buffer and enable
  // receiving of self ip addresses on read.
  // Return kQuicInvalidSocketFd if failed.
  QuicUdpSocketFd Create(int address_family,
                         int receive_buffer_size,
                         int send_buffer_size);

  // Closes |fd|. No-op if |fd| equals to kQuicInvalidSocketFd.
  void Destroy(QuicUdpSocketFd fd);

  // Bind |fd| to |address|. If |address|'s port number is 0, kernel will choose
  // a random port to bind to. Caller can use QuicSocketAddress::FromSocket(fd)
  // to get the bound random port.
  bool Bind(QuicUdpSocketFd fd, QuicSocketAddress address);

  // Enable receiving of various per-packet information. Return true if the
  // corresponding information can be received on read.
  bool EnableDroppedPacketCount(QuicUdpSocketFd fd);
  bool EnableReceiveTimestamp(QuicUdpSocketFd fd);
  bool EnableReceiveTtlForV4(QuicUdpSocketFd fd);
  bool EnableReceiveTtlForV6(QuicUdpSocketFd fd);

  // Wait for |fd| to become readable, up to |timeout|.
  // Return true if |fd| is readable upon return.
  bool WaitUntilReadable(QuicUdpSocketFd fd, QuicTime::Delta timeout);

  struct QUIC_EXPORT_PRIVATE ReadPacketResult {
    bool ok = false;
    QuicUdpPacketInfo packet_info;
    BufferSpan packet_buffer;
    BufferSpan control_buffer;

    void Reset(size_t packet_buffer_length) {
      ok = false;
      packet_info.Reset();
      packet_buffer.buffer_len = packet_buffer_length;
    }
  };
  // Read a packet from |fd|:
  // packet_info_interested: Bitmask indicating what information caller wants to
  //                         receive into |result->packet_info|.
  // result->packet_info:    Received per packet information.
  // result->packet_buffer:  The packet buffer, to be filled with packet data.
  //                         |result->packet_buffer.buffer_len| is set to the
  //                         packet length on a successful return.
  // result->control_buffer: The control buffer, used by ReadPacket internally.
  //                         It is recommended to be
  //                         |kDefaultUdpPacketControlBufferSize| bytes.
  // result->ok:             True iff a packet is successfully received.
  //
  // If |*result| is reused for subsequent ReadPacket() calls, caller needs to
  // call result->Reset() before each ReadPacket().
  void ReadPacket(QuicUdpSocketFd fd,
                  BitMask64 packet_info_interested,
                  ReadPacketResult* result);

  using ReadPacketResults = std::vector<ReadPacketResult>;
  // Read up to |results->size()| packets from |fd|. The meaning of each element
  // in |*results| has been documented on top of |ReadPacket|.
  // Return the number of elements populated into |*results|, note it is
  // possible for some of the populated elements to have ok=false.
  size_t ReadMultiplePackets(QuicUdpSocketFd fd,
                             BitMask64 packet_info_interested,
                             ReadPacketResults* results);

  // Write a packet to |fd|.
  // packet_buffer, packet_buffer_len:  The packet buffer to write.
  // packet_info:                       The per packet information to set.
  WriteResult WritePacket(QuicUdpSocketFd fd,
                          const char* packet_buffer,
                          size_t packet_buffer_len,
                          const QuicUdpPacketInfo& packet_info);

 protected:
  bool SetupSocket(QuicUdpSocketFd fd,
                   int address_family,
                   int receive_buffer_size,
                   int send_buffer_size);
  bool EnableReceiveSelfIpAddressForV4(QuicUdpSocketFd fd);
  bool EnableReceiveSelfIpAddressForV6(QuicUdpSocketFd fd);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_UDP_SOCKET_H_
