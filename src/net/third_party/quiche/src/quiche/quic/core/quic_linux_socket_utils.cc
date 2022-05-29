// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_linux_socket_utils.h"

#include <linux/net_tstamp.h>
#include <netinet/in.h>

#include <cstdint>

#include "quiche/quic/core/quic_syscall_wrapper.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

QuicMsgHdr::QuicMsgHdr(const char* buffer, size_t buf_len,
                       const QuicSocketAddress& peer_address, char* cbuf,
                       size_t cbuf_size)
    : iov_{const_cast<char*>(buffer), buf_len},
      cbuf_(cbuf),
      cbuf_size_(cbuf_size),
      cmsg_(nullptr) {
  // Only support unconnected sockets.
  QUICHE_DCHECK(peer_address.IsInitialized());

  raw_peer_address_ = peer_address.generic_address();
  hdr_.msg_name = &raw_peer_address_;
  hdr_.msg_namelen = raw_peer_address_.ss_family == AF_INET
                         ? sizeof(sockaddr_in)
                         : sizeof(sockaddr_in6);

  hdr_.msg_iov = &iov_;
  hdr_.msg_iovlen = 1;
  hdr_.msg_flags = 0;

  hdr_.msg_control = nullptr;
  hdr_.msg_controllen = 0;
}

void QuicMsgHdr::SetIpInNextCmsg(const QuicIpAddress& self_address) {
  if (!self_address.IsInitialized()) {
    return;
  }

  if (self_address.IsIPv4()) {
    QuicLinuxSocketUtils::SetIpInfoInCmsgData(
        self_address, GetNextCmsgData<in_pktinfo>(IPPROTO_IP, IP_PKTINFO));
  } else {
    QuicLinuxSocketUtils::SetIpInfoInCmsgData(
        self_address, GetNextCmsgData<in6_pktinfo>(IPPROTO_IPV6, IPV6_PKTINFO));
  }
}

void* QuicMsgHdr::GetNextCmsgDataInternal(int cmsg_level, int cmsg_type,
                                          size_t data_size) {
  // msg_controllen needs to be increased first, otherwise CMSG_NXTHDR will
  // return nullptr.
  hdr_.msg_controllen += CMSG_SPACE(data_size);
  QUICHE_DCHECK_LE(hdr_.msg_controllen, cbuf_size_);

  if (cmsg_ == nullptr) {
    QUICHE_DCHECK_EQ(nullptr, hdr_.msg_control);
    memset(cbuf_, 0, cbuf_size_);
    hdr_.msg_control = cbuf_;
    cmsg_ = CMSG_FIRSTHDR(&hdr_);
  } else {
    QUICHE_DCHECK_NE(nullptr, hdr_.msg_control);
    cmsg_ = CMSG_NXTHDR(&hdr_, cmsg_);
  }

  QUICHE_DCHECK_NE(nullptr, cmsg_) << "Insufficient control buffer space";

  cmsg_->cmsg_len = CMSG_LEN(data_size);
  cmsg_->cmsg_level = cmsg_level;
  cmsg_->cmsg_type = cmsg_type;

  return CMSG_DATA(cmsg_);
}

void QuicMMsgHdr::InitOneHeader(int i, const BufferedWrite& buffered_write) {
  mmsghdr* mhdr = GetMMsgHdr(i);
  msghdr* hdr = &mhdr->msg_hdr;
  iovec* iov = GetIov(i);

  iov->iov_base = const_cast<char*>(buffered_write.buffer);
  iov->iov_len = buffered_write.buf_len;
  hdr->msg_iov = iov;
  hdr->msg_iovlen = 1;
  hdr->msg_control = nullptr;
  hdr->msg_controllen = 0;

  // Only support unconnected sockets.
  QUICHE_DCHECK(buffered_write.peer_address.IsInitialized());

  sockaddr_storage* peer_address_storage = GetPeerAddressStorage(i);
  *peer_address_storage = buffered_write.peer_address.generic_address();
  hdr->msg_name = peer_address_storage;
  hdr->msg_namelen = peer_address_storage->ss_family == AF_INET
                         ? sizeof(sockaddr_in)
                         : sizeof(sockaddr_in6);
}

void QuicMMsgHdr::SetIpInNextCmsg(int i, const QuicIpAddress& self_address) {
  if (!self_address.IsInitialized()) {
    return;
  }

  if (self_address.IsIPv4()) {
    QuicLinuxSocketUtils::SetIpInfoInCmsgData(
        self_address, GetNextCmsgData<in_pktinfo>(i, IPPROTO_IP, IP_PKTINFO));
  } else {
    QuicLinuxSocketUtils::SetIpInfoInCmsgData(
        self_address,
        GetNextCmsgData<in6_pktinfo>(i, IPPROTO_IPV6, IPV6_PKTINFO));
  }
}

void* QuicMMsgHdr::GetNextCmsgDataInternal(int i, int cmsg_level, int cmsg_type,
                                           size_t data_size) {
  mmsghdr* mhdr = GetMMsgHdr(i);
  msghdr* hdr = &mhdr->msg_hdr;
  cmsghdr*& cmsg = *GetCmsgHdr(i);

  // msg_controllen needs to be increased first, otherwise CMSG_NXTHDR will
  // return nullptr.
  hdr->msg_controllen += CMSG_SPACE(data_size);
  QUICHE_DCHECK_LE(hdr->msg_controllen, cbuf_size_);

  if (cmsg == nullptr) {
    QUICHE_DCHECK_EQ(nullptr, hdr->msg_control);
    hdr->msg_control = GetCbuf(i);
    cmsg = CMSG_FIRSTHDR(hdr);
  } else {
    QUICHE_DCHECK_NE(nullptr, hdr->msg_control);
    cmsg = CMSG_NXTHDR(hdr, cmsg);
  }

  QUICHE_DCHECK_NE(nullptr, cmsg) << "Insufficient control buffer space";

  cmsg->cmsg_len = CMSG_LEN(data_size);
  cmsg->cmsg_level = cmsg_level;
  cmsg->cmsg_type = cmsg_type;

  return CMSG_DATA(cmsg);
}

int QuicMMsgHdr::num_bytes_sent(int num_packets_sent) {
  QUICHE_DCHECK_LE(0, num_packets_sent);
  QUICHE_DCHECK_LE(num_packets_sent, num_msgs_);

  int bytes_sent = 0;
  iovec* iov = GetIov(0);
  for (int i = 0; i < num_packets_sent; ++i) {
    bytes_sent += iov[i].iov_len;
  }
  return bytes_sent;
}

// static
int QuicLinuxSocketUtils::GetUDPSegmentSize(int fd) {
  int optval;
  socklen_t optlen = sizeof(optval);
  int rc = getsockopt(fd, SOL_UDP, UDP_SEGMENT, &optval, &optlen);
  if (rc < 0) {
    QUIC_LOG_EVERY_N_SEC(INFO, 10)
        << "getsockopt(UDP_SEGMENT) failed: " << strerror(errno);
    return -1;
  }
  QUIC_LOG_EVERY_N_SEC(INFO, 10)
      << "getsockopt(UDP_SEGMENT) returned segment size: " << optval;
  return optval;
}

// static
bool QuicLinuxSocketUtils::EnableReleaseTime(int fd, clockid_t clockid) {
  // TODO(wub): Change to sock_txtime once it is available in linux/net_tstamp.h
  struct LinuxSockTxTime {
    clockid_t clockid; /* reference clockid */
    uint32_t flags;    /* flags defined by enum txtime_flags */
  };

  LinuxSockTxTime so_txtime_val{clockid, 0};

  if (setsockopt(fd, SOL_SOCKET, SO_TXTIME, &so_txtime_val,
                 sizeof(so_txtime_val)) != 0) {
    QUIC_LOG_EVERY_N_SEC(INFO, 10)
        << "setsockopt(SOL_SOCKET,SO_TXTIME) failed: " << strerror(errno);
    return false;
  }

  return true;
}

// static
bool QuicLinuxSocketUtils::GetTtlFromMsghdr(struct msghdr* hdr, int* ttl) {
  if (hdr->msg_controllen > 0) {
    struct cmsghdr* cmsg;
    for (cmsg = CMSG_FIRSTHDR(hdr); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(hdr, cmsg)) {
      if ((cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_TTL) ||
          (cmsg->cmsg_level == IPPROTO_IPV6 &&
           cmsg->cmsg_type == IPV6_HOPLIMIT)) {
        *ttl = *(reinterpret_cast<int*>(CMSG_DATA(cmsg)));
        return true;
      }
    }
  }
  return false;
}

// static
void QuicLinuxSocketUtils::SetIpInfoInCmsgData(
    const QuicIpAddress& self_address, void* cmsg_data) {
  QUICHE_DCHECK(self_address.IsInitialized());
  const std::string& address_str = self_address.ToPackedString();
  if (self_address.IsIPv4()) {
    in_pktinfo* pktinfo = static_cast<in_pktinfo*>(cmsg_data);
    pktinfo->ipi_ifindex = 0;
    memcpy(&pktinfo->ipi_spec_dst, address_str.c_str(), address_str.length());
  } else if (self_address.IsIPv6()) {
    in6_pktinfo* pktinfo = static_cast<in6_pktinfo*>(cmsg_data);
    memcpy(&pktinfo->ipi6_addr, address_str.c_str(), address_str.length());
  } else {
    QUIC_BUG(quic_bug_10598_1) << "Unrecognized IPAddress";
  }
}

// static
size_t QuicLinuxSocketUtils::SetIpInfoInCmsg(const QuicIpAddress& self_address,
                                             cmsghdr* cmsg) {
  std::string address_string;
  if (self_address.IsIPv4()) {
    cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
    cmsg->cmsg_level = IPPROTO_IP;
    cmsg->cmsg_type = IP_PKTINFO;
    in_pktinfo* pktinfo = reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg));
    memset(pktinfo, 0, sizeof(in_pktinfo));
    pktinfo->ipi_ifindex = 0;
    address_string = self_address.ToPackedString();
    memcpy(&pktinfo->ipi_spec_dst, address_string.c_str(),
           address_string.length());
    return sizeof(in_pktinfo);
  } else if (self_address.IsIPv6()) {
    cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
    cmsg->cmsg_level = IPPROTO_IPV6;
    cmsg->cmsg_type = IPV6_PKTINFO;
    in6_pktinfo* pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
    memset(pktinfo, 0, sizeof(in6_pktinfo));
    address_string = self_address.ToPackedString();
    memcpy(&pktinfo->ipi6_addr, address_string.c_str(),
           address_string.length());
    return sizeof(in6_pktinfo);
  } else {
    QUIC_BUG(quic_bug_10598_2) << "Unrecognized IPAddress";
    return 0;
  }
}

// static
WriteResult QuicLinuxSocketUtils::WritePacket(int fd, const QuicMsgHdr& hdr) {
  int rc;
  do {
    rc = GetGlobalSyscallWrapper()->Sendmsg(fd, hdr.hdr(), 0);
  } while (rc < 0 && errno == EINTR);
  if (rc >= 0) {
    return WriteResult(WRITE_STATUS_OK, rc);
  }
  return WriteResult((errno == EAGAIN || errno == EWOULDBLOCK)
                         ? WRITE_STATUS_BLOCKED
                         : WRITE_STATUS_ERROR,
                     errno);
}

// static
WriteResult QuicLinuxSocketUtils::WriteMultiplePackets(int fd,
                                                       QuicMMsgHdr* mhdr,
                                                       int* num_packets_sent) {
  *num_packets_sent = 0;

  if (mhdr->num_msgs() <= 0) {
    return WriteResult(WRITE_STATUS_ERROR, EINVAL);
  }

  int rc;
  do {
    rc = GetGlobalSyscallWrapper()->Sendmmsg(fd, mhdr->mhdr(), mhdr->num_msgs(),
                                             0);
  } while (rc < 0 && errno == EINTR);

  if (rc > 0) {
    *num_packets_sent = rc;

    return WriteResult(WRITE_STATUS_OK, mhdr->num_bytes_sent(rc));
  } else if (rc == 0) {
    QUIC_BUG(quic_bug_10598_3)
        << "sendmmsg returned 0, returning WRITE_STATUS_ERROR. errno: "
        << errno;
    errno = EIO;
  }

  return WriteResult((errno == EAGAIN || errno == EWOULDBLOCK)
                         ? WRITE_STATUS_BLOCKED
                         : WRITE_STATUS_ERROR,
                     errno);
}

}  // namespace quic
