// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// UDP socket implementation for Windows.  Feature overview:
// * Supported: setting and getting self IP address.
// * Unsupported but could work on a sufficiently new Windows version:
//   - Timestamping
//   - Setting and getting TTL.

#include <mswsock.h>
#include <winsock2.h>

#include <cstddef>

#include "quiche/quic/core/quic_utils.h"

namespace quic {

namespace {
constexpr size_t kMinCmsgSpaceForRead =
    CMSG_SPACE(sizeof(in_pktinfo))      // V4 Self IP
    + CMSG_SPACE(sizeof(in6_pktinfo));  // V6 Self IP

constexpr int kIpv6RecvPacketInfo = IPV6_PKTINFO;

void SetV4SelfIpInControlMessage(const QuicIpAddress& self_address,
                                 WSACMSGHDR* cmsg) {
  QUICHE_DCHECK(self_address.IsIPv4());
  in_pktinfo* pktinfo = reinterpret_cast<in_pktinfo*>(WSA_CMSG_DATA(cmsg));
  memset(pktinfo, 0, sizeof(in_pktinfo));
  pktinfo->ipi_addr = self_address.GetIPv4();
}

void SetV6SelfIpInControlMessage(const QuicIpAddress& self_address,
                                 cmsghdr* cmsg) {
  QUICHE_DCHECK(self_address.IsIPv6());
  in6_pktinfo* pktinfo = reinterpret_cast<in6_pktinfo*>(WSA_CMSG_DATA(cmsg));
  memset(pktinfo, 0, sizeof(in6_pktinfo));
  pktinfo->ipi6_addr = self_address.GetIPv6();
}

bool NextCmsg(WSAMSG* hdr, char* control_buffer, size_t control_buffer_len,
              int cmsg_level, int cmsg_type, size_t data_size,
              WSACMSGHDR** cmsg /*in, out*/) {
  // msg_controllen needs to be increased first, otherwise CMSG_NXTHDR will
  // return nullptr.
  hdr->Control.len += WSA_CMSG_SPACE(data_size);
  if (hdr->Control.len > control_buffer_len) {
    return false;
  }

  if ((*cmsg) == nullptr) {
    QUICHE_DCHECK_EQ(nullptr, hdr->Control.buf);
    memset(control_buffer, 0, control_buffer_len);
    hdr->Control.buf = control_buffer;
    (*cmsg) = WSA_CMSG_FIRSTHDR(hdr);
  } else {
    QUICHE_DCHECK_NE(nullptr, hdr->Control.buf);
    (*cmsg) = WSA_CMSG_NXTHDR(hdr, (*cmsg));
  }

  if (nullptr == (*cmsg)) {
    return false;
  }

  (*cmsg)->cmsg_len = WSA_CMSG_LEN(data_size);
  (*cmsg)->cmsg_level = cmsg_level;
  (*cmsg)->cmsg_type = cmsg_type;

  return true;
}
}  // namespace

bool QuicUdpSocketApi::SetupSocket(QuicUdpSocketFd fd, int address_family,
                                   int receive_buffer_size,
                                   int send_buffer_size, bool /*ipv6_only*/) {
  // Receive buffer size.
  if (absl::Status status =
          socket_api::SetReceiveBufferSize(fd, receive_buffer_size);
      !status.ok()) {
    QUIC_LOG_FIRST_N(ERROR, 100)
        << "Failed to set socket recv size: " << status;
    return false;
  }

  // Send buffer size.
  if (absl::Status status = socket_api::SetSendBufferSize(fd, send_buffer_size);
      !status.ok()) {
    QUIC_LOG_FIRST_N(ERROR, 100)
        << "Failed to set socket send size: " << status;
    return false;
  }

  if (address_family == AF_INET) {
    if (!EnableReceiveSelfIpAddressForV4(fd)) {
      QUIC_LOG_FIRST_N(ERROR, 100)
          << "Failed to enable receiving of self v4 ip";
      return false;
    }
  }

  if (address_family == AF_INET6) {
    if (!EnableReceiveSelfIpAddressForV6(fd)) {
      QUIC_LOG_FIRST_N(ERROR, 100)
          << "Failed to enable receiving of self v6 ip";
      return false;
    }
  }

  return true;
}

void QuicUdpSocketApi::ReadPacket(
    QuicUdpSocketFd fd, QuicUdpPacketInfoBitMask packet_info_interested,
    ReadPacketResult* result) {
  result->ok = false;

  // WSARecvMsg is an extension to Windows Socket API that requires us to fetch
  // the function pointer via an ioctl.
  DWORD recvmsg_fn_out_bytes;
  LPFN_WSARECVMSG recvmsg_fn = nullptr;
  GUID recvmsg_guid = WSAID_WSARECVMSG;
  int ioctl_result =
      WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &recvmsg_guid,
               sizeof(recvmsg_guid), &recvmsg_fn, sizeof(recvmsg_fn),
               &recvmsg_fn_out_bytes, nullptr, nullptr);
  if (ioctl_result != 0) {
    QUICHE_LOG(ERROR) << "Failed to load WSARecvMsg() function, error code: "
                      << WSAGetLastError();
    return;
  }

  BufferSpan& packet_buffer = result->packet_buffer;
  BufferSpan& control_buffer = result->control_buffer;
  QuicUdpPacketInfo* packet_info = &result->packet_info;

  QUICHE_DCHECK_GE(control_buffer.buffer_len, kMinCmsgSpaceForRead);

  WSABUF iov;
  iov.buf = packet_buffer.buffer;
  iov.len = packet_buffer.buffer_len;
  sockaddr_storage raw_peer_address;

  if (control_buffer.buffer_len > 0) {
    reinterpret_cast<WSACMSGHDR*>(control_buffer.buffer)->cmsg_len =
        control_buffer.buffer_len;
  }

  WSAMSG hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.name = reinterpret_cast<sockaddr*>(&raw_peer_address);
  hdr.namelen = sizeof(raw_peer_address);
  hdr.lpBuffers = &iov;
  hdr.dwBufferCount = 1;
  hdr.dwFlags = 0;
  hdr.Control.buf = control_buffer.buffer;
  hdr.Control.len = control_buffer.buffer_len;

  DWORD bytes_read;
  int recvmsg_result = recvmsg_fn(fd, &hdr, &bytes_read, nullptr, nullptr);
  if (recvmsg_result != 0) {
    const int error_num = WSAGetLastError();
    if (error_num != WSAEWOULDBLOCK) {
      QUIC_LOG_FIRST_N(ERROR, 100) << "Error reading packet: " << error_num;
    }
    return;
  }

  packet_buffer.buffer_len = bytes_read;
  if (packet_info_interested.IsSet(QuicUdpPacketInfoBit::PEER_ADDRESS)) {
    packet_info->SetPeerAddress(QuicSocketAddress(raw_peer_address));
  }

  if (hdr.Control.len > 0) {
    for (WSACMSGHDR* cmsg = WSA_CMSG_FIRSTHDR(&hdr); cmsg != nullptr;
         cmsg = WSA_CMSG_NXTHDR(&hdr, cmsg)) {
      QuicUdpPacketInfoBitMask prior_bitmask = packet_info->bitmask();
      PopulatePacketInfoFromControlMessageBase(cmsg, packet_info,
                                               packet_info_interested);
      if (packet_info->bitmask() == prior_bitmask) {
        QUIC_DLOG(INFO) << "Ignored cmsg_level:" << cmsg->cmsg_level
                        << ", cmsg_type:" << cmsg->cmsg_type;
      }
    }
  }

  result->ok = true;
}

size_t QuicUdpSocketApi::ReadMultiplePackets(
    QuicUdpSocketFd fd, QuicUdpPacketInfoBitMask packet_info_interested,
    ReadPacketResults* results) {
  size_t num_packets = 0;
  for (ReadPacketResult& result : *results) {
    result.ok = false;
  }
  for (ReadPacketResult& result : *results) {
    ReadPacket(fd, packet_info_interested, &result);
    if (!result.ok && WSAGetLastError() == WSAEWOULDBLOCK) {
      break;
    }
    ++num_packets;
  }
  return num_packets;
}

WriteResult QuicUdpSocketApi::WritePacket(
    QuicUdpSocketFd fd, const char* packet_buffer, size_t packet_buffer_len,
    const QuicUdpPacketInfo& packet_info) {
  if (!packet_info.HasValue(QuicUdpPacketInfoBit::PEER_ADDRESS)) {
    return WriteResult(WRITE_STATUS_ERROR, WSAEINVAL);
  }

  char control_buffer[512];
  sockaddr_storage raw_peer_address =
      packet_info.peer_address().generic_address();
  WSABUF iov;
  iov.buf = const_cast<char*>(packet_buffer);
  iov.len = packet_buffer_len;

  WSAMSG hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.name = reinterpret_cast<sockaddr*>(&raw_peer_address);
  hdr.namelen = packet_info.peer_address().host().IsIPv4()
                    ? sizeof(sockaddr_in)
                    : sizeof(sockaddr_in6);
  hdr.lpBuffers = &iov;
  hdr.dwBufferCount = 1;

  WSACMSGHDR* cmsg = nullptr;

  // Set self IP.
  if (packet_info.HasValue(QuicUdpPacketInfoBit::V4_SELF_IP) &&
      packet_info.self_v4_ip().IsInitialized()) {
    if (!NextCmsg(&hdr, control_buffer, sizeof(control_buffer), IPPROTO_IP,
                  IP_PKTINFO, sizeof(in_pktinfo), &cmsg)) {
      QUIC_LOG_FIRST_N(ERROR, 100)
          << "Not enough buffer to set self v4 ip address.";
      return WriteResult(WRITE_STATUS_ERROR, EINVAL);
    }
    SetV4SelfIpInControlMessage(packet_info.self_v4_ip(), cmsg);
  } else if (packet_info.HasValue(QuicUdpPacketInfoBit::V6_SELF_IP) &&
             packet_info.self_v6_ip().IsInitialized()) {
    if (!NextCmsg(&hdr, control_buffer, sizeof(control_buffer), IPPROTO_IPV6,
                  IPV6_PKTINFO, sizeof(in6_pktinfo), &cmsg)) {
      QUIC_LOG_FIRST_N(ERROR, 100)
          << "Not enough buffer to set self v6 ip address.";
      return WriteResult(WRITE_STATUS_ERROR, EINVAL);
    }
    SetV6SelfIpInControlMessage(packet_info.self_v6_ip(), cmsg);
  }

  DWORD bytes_sent;
  int result =
      WSASendMsg(fd, &hdr, /*dwFlags=*/0, &bytes_sent, nullptr, nullptr);
  if (result == 0) {
    return WriteResult(WRITE_STATUS_OK, bytes_sent);
  }
  int error = WSAGetLastError();
  return WriteResult(
      (error == WSAEWOULDBLOCK) ? WRITE_STATUS_BLOCKED : WRITE_STATUS_ERROR,
      error);
}

bool QuicUdpSocketApi::WaitUntilReadable(QuicUdpSocketFd fd,
                                         QuicTime::Delta timeout) {
  WSAPOLLFD polled_fd;
  polled_fd.fd = fd;
  polled_fd.events = POLLIN;
  polled_fd.revents = 0;

  int result = ::WSAPoll(&polled_fd, 1, timeout.ToMilliseconds());
  if (result == SOCKET_ERROR) {
    QUICHE_LOG(ERROR) << "Error while calling WSAPoll(): " << WSAGetLastError();
  }

  return result > 0;
}

}  // namespace quic
