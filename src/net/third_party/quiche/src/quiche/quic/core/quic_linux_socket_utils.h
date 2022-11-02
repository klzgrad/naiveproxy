// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_LINUX_SOCKET_UTILS_H_
#define QUICHE_QUIC_CORE_QUIC_LINUX_SOCKET_UTILS_H_

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

#ifndef SOL_UDP
#define SOL_UDP 17
#endif

#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif

#ifndef UDP_MAX_SEGMENTS
#define UDP_MAX_SEGMENTS (1 << 6UL)
#endif

#ifndef SO_TXTIME
#define SO_TXTIME 61
#endif

namespace quic {

const int kCmsgSpaceForIpv4 = CMSG_SPACE(sizeof(in_pktinfo));
const int kCmsgSpaceForIpv6 = CMSG_SPACE(sizeof(in6_pktinfo));
// kCmsgSpaceForIp should be big enough to hold both IPv4 and IPv6 packet info.
const int kCmsgSpaceForIp = (kCmsgSpaceForIpv4 < kCmsgSpaceForIpv6)
                                ? kCmsgSpaceForIpv6
                                : kCmsgSpaceForIpv4;

const int kCmsgSpaceForSegmentSize = CMSG_SPACE(sizeof(uint16_t));

const int kCmsgSpaceForTxTime = CMSG_SPACE(sizeof(uint64_t));

const int kCmsgSpaceForTTL = CMSG_SPACE(sizeof(int));

// QuicMsgHdr is used to build msghdr objects that can be used send packets via
// ::sendmsg.
//
// Example:
//   // cbuf holds control messages(cmsgs). The size is determined from what
//   // cmsgs will be set for this msghdr.
//   char cbuf[kCmsgSpaceForIp + kCmsgSpaceForSegmentSize];
//   QuicMsgHdr hdr(packet_buf, packet_buf_len, peer_addr, cbuf, sizeof(cbuf));
//
//   // Set IP in cmsgs.
//   hdr.SetIpInNextCmsg(self_addr);
//
//   // Set GSO size in cmsgs.
//   *hdr.GetNextCmsgData<uint16_t>(SOL_UDP, UDP_SEGMENT) = 1200;
//
//   QuicLinuxSocketUtils::WritePacket(fd, hdr);
class QUIC_EXPORT_PRIVATE QuicMsgHdr {
 public:
  QuicMsgHdr(const char* buffer, size_t buf_len,
             const QuicSocketAddress& peer_address, char* cbuf,
             size_t cbuf_size);

  // Set IP info in the next cmsg. Both IPv4 and IPv6 are supported.
  void SetIpInNextCmsg(const QuicIpAddress& self_address);

  template <typename DataType>
  DataType* GetNextCmsgData(int cmsg_level, int cmsg_type) {
    return reinterpret_cast<DataType*>(
        GetNextCmsgDataInternal(cmsg_level, cmsg_type, sizeof(DataType)));
  }

  const msghdr* hdr() const { return &hdr_; }

 protected:
  void* GetNextCmsgDataInternal(int cmsg_level, int cmsg_type,
                                size_t data_size);

  msghdr hdr_;
  iovec iov_;
  sockaddr_storage raw_peer_address_;
  char* cbuf_;
  const size_t cbuf_size_;
  // The last cmsg populated so far. nullptr means nothing has been populated.
  cmsghdr* cmsg_;
};

// BufferedWrite holds all information needed to send a packet.
struct QUIC_EXPORT_PRIVATE BufferedWrite {
  BufferedWrite(const char* buffer, size_t buf_len,
                const QuicIpAddress& self_address,
                const QuicSocketAddress& peer_address)
      : BufferedWrite(buffer, buf_len, self_address, peer_address,
                      std::unique_ptr<PerPacketOptions>(),
                      /*release_time=*/0) {}

  BufferedWrite(const char* buffer, size_t buf_len,
                const QuicIpAddress& self_address,
                const QuicSocketAddress& peer_address,
                std::unique_ptr<PerPacketOptions> options,
                uint64_t release_time)
      : buffer(buffer),
        buf_len(buf_len),
        self_address(self_address),
        peer_address(peer_address),
        options(std::move(options)),
        release_time(release_time) {}

  const char* buffer;  // Not owned.
  size_t buf_len;
  QuicIpAddress self_address;
  QuicSocketAddress peer_address;
  std::unique_ptr<PerPacketOptions> options;

  // The release time according to the owning packet writer's clock, which is
  // often not a QuicClock. Calculated from packet writer's Now() and the
  // release time delay in |options|.
  // 0 means it can be sent at the same time as the previous packet in a batch,
  // or can be sent Now() if this is the first packet of a batch.
  uint64_t release_time;
};

// QuicMMsgHdr is used to build mmsghdr objects that can be used to send
// multiple packets at once via ::sendmmsg.
//
// Example:
//   quiche::QuicheCircularDeque<BufferedWrite> buffered_writes;
//   ... (Populate buffered_writes) ...
//
//   QuicMMsgHdr mhdr(
//       buffered_writes.begin(), buffered_writes.end(), kCmsgSpaceForIp,
//       [](QuicMMsgHdr* mhdr, int i, const BufferedWrite& buffered_write) {
//         mhdr->SetIpInNextCmsg(i, buffered_write.self_address);
//       });
//
//   int num_packets_sent;
//   QuicSocketUtils::WriteMultiplePackets(fd, &mhdr, &num_packets_sent);
class QUIC_EXPORT_PRIVATE QuicMMsgHdr {
 public:
  using ControlBufferInitializer = std::function<void(
      QuicMMsgHdr* mhdr, int i, const BufferedWrite& buffered_write)>;
  template <typename IteratorT>
  QuicMMsgHdr(const IteratorT& first, const IteratorT& last, size_t cbuf_size,
              ControlBufferInitializer cbuf_initializer)
      : num_msgs_(std::distance(first, last)), cbuf_size_(cbuf_size) {
    static_assert(
        std::is_same<typename std::iterator_traits<IteratorT>::value_type,
                     BufferedWrite>::value,
        "Must iterate over a collection of BufferedWrite.");

    QUICHE_DCHECK_LE(0, num_msgs_);
    if (num_msgs_ == 0) {
      return;
    }

    storage_.reset(new char[StorageSize()]);
    memset(&storage_[0], 0, StorageSize());

    int i = -1;
    for (auto it = first; it != last; ++it) {
      ++i;

      InitOneHeader(i, *it);
      if (cbuf_initializer) {
        cbuf_initializer(this, i, *it);
      }
    }
  }

  void SetIpInNextCmsg(int i, const QuicIpAddress& self_address);

  template <typename DataType>
  DataType* GetNextCmsgData(int i, int cmsg_level, int cmsg_type) {
    return reinterpret_cast<DataType*>(
        GetNextCmsgDataInternal(i, cmsg_level, cmsg_type, sizeof(DataType)));
  }

  mmsghdr* mhdr() { return GetMMsgHdr(0); }

  int num_msgs() const { return num_msgs_; }

  // Get the total number of bytes in the first |num_packets_sent| packets.
  int num_bytes_sent(int num_packets_sent);

 protected:
  void InitOneHeader(int i, const BufferedWrite& buffered_write);

  void* GetNextCmsgDataInternal(int i, int cmsg_level, int cmsg_type,
                                size_t data_size);

  size_t StorageSize() const {
    return num_msgs_ *
           (sizeof(mmsghdr) + sizeof(iovec) + sizeof(sockaddr_storage) +
            sizeof(cmsghdr*) + cbuf_size_);
  }

  mmsghdr* GetMMsgHdr(int i) {
    auto* first = reinterpret_cast<mmsghdr*>(&storage_[0]);
    return &first[i];
  }

  iovec* GetIov(int i) {
    auto* first = reinterpret_cast<iovec*>(GetMMsgHdr(num_msgs_));
    return &first[i];
  }

  sockaddr_storage* GetPeerAddressStorage(int i) {
    auto* first = reinterpret_cast<sockaddr_storage*>(GetIov(num_msgs_));
    return &first[i];
  }

  cmsghdr** GetCmsgHdr(int i) {
    auto* first = reinterpret_cast<cmsghdr**>(GetPeerAddressStorage(num_msgs_));
    return &first[i];
  }

  char* GetCbuf(int i) {
    auto* first = reinterpret_cast<char*>(GetCmsgHdr(num_msgs_));
    return &first[i * cbuf_size_];
  }

  const int num_msgs_;
  // Size of cmsg buffer for each message.
  const size_t cbuf_size_;
  // storage_ holds the memory of
  // |num_msgs_| mmsghdr
  // |num_msgs_| iovec
  // |num_msgs_| sockaddr_storage, for peer addresses
  // |num_msgs_| cmsghdr*
  // |num_msgs_| cbuf, each of size cbuf_size
  std::unique_ptr<char[]> storage_;
};

class QUIC_EXPORT_PRIVATE QuicLinuxSocketUtils {
 public:
  // Return the UDP segment size of |fd|, 0 means segment size has not been set
  // on this socket. If GSO is not supported, return -1.
  static int GetUDPSegmentSize(int fd);

  // Enable release time on |fd|.
  static bool EnableReleaseTime(int fd, clockid_t clockid);

  // If the msghdr contains an IP_TTL entry, this will set ttl to the correct
  // value and return true. Otherwise it will return false.
  static bool GetTtlFromMsghdr(struct msghdr* hdr, int* ttl);

  // Set IP(self_address) in |cmsg_data|. Does not touch other fields in the
  // containing cmsghdr.
  static void SetIpInfoInCmsgData(const QuicIpAddress& self_address,
                                  void* cmsg_data);

  // A helper for WritePacket which fills in the cmsg with the supplied self
  // address.
  // Returns the length of the packet info structure used.
  static size_t SetIpInfoInCmsg(const QuicIpAddress& self_address,
                                cmsghdr* cmsg);

  // Writes the packet in |hdr| to the socket, using ::sendmsg.
  static WriteResult WritePacket(int fd, const QuicMsgHdr& hdr);

  // Writes the packets in |mhdr| to the socket, using ::sendmmsg if available.
  static WriteResult WriteMultiplePackets(int fd, QuicMMsgHdr* mhdr,
                                          int* num_packets_sent);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_LINUX_SOCKET_UTILS_H_
