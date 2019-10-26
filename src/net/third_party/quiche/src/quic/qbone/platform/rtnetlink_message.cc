// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/rtnetlink_message.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"

namespace quic {

RtnetlinkMessage::RtnetlinkMessage(uint16_t type,
                                   uint16_t flags,
                                   uint32_t seq,
                                   uint32_t pid,
                                   const void* payload_header,
                                   size_t payload_header_length) {
  auto* buf = new uint8_t[NLMSG_SPACE(payload_header_length)];
  memset(buf, 0, NLMSG_SPACE(payload_header_length));

  auto* message_header = reinterpret_cast<struct nlmsghdr*>(buf);
  message_header->nlmsg_len = NLMSG_LENGTH(payload_header_length);
  message_header->nlmsg_type = type;
  message_header->nlmsg_flags = flags;
  message_header->nlmsg_seq = seq;
  message_header->nlmsg_pid = pid;

  if (payload_header != nullptr) {
    memcpy(NLMSG_DATA(message_header), payload_header, payload_header_length);
  }
  message_.push_back({buf, NLMSG_SPACE(payload_header_length)});
}

RtnetlinkMessage::~RtnetlinkMessage() {
  for (const auto& iov : message_) {
    delete[] reinterpret_cast<uint8_t*>(iov.iov_base);
  }
}

void RtnetlinkMessage::AppendAttribute(uint16_t type,
                                       const void* data,
                                       uint16_t data_length) {
  auto* buf = new uint8_t[RTA_SPACE(data_length)];
  memset(buf, 0, RTA_SPACE(data_length));

  auto* rta = reinterpret_cast<struct rtattr*>(buf);
  static_assert(sizeof(uint16_t) == sizeof(rta->rta_len),
                "struct rtattr uses unsigned short, it's no longer 16bits");
  static_assert(sizeof(uint16_t) == sizeof(rta->rta_type),
                "struct rtattr uses unsigned short, it's no longer 16bits");

  rta->rta_len = RTA_LENGTH(data_length);
  rta->rta_type = type;
  memcpy(RTA_DATA(rta), data, data_length);

  message_.push_back({buf, RTA_SPACE(data_length)});
  AdjustMessageLength(rta->rta_len);
}

std::unique_ptr<struct iovec[]> RtnetlinkMessage::BuildIoVec() const {
  auto message = QuicMakeUnique<struct iovec[]>(message_.size());
  int idx = 0;
  for (const auto& vec : message_) {
    message[idx++] = vec;
  }
  return message;
}

size_t RtnetlinkMessage::IoVecSize() const {
  return message_.size();
}

void RtnetlinkMessage::AdjustMessageLength(size_t additional_data_length) {
  MessageHeader()->nlmsg_len =
      NLMSG_ALIGN(MessageHeader()->nlmsg_len) + additional_data_length;
}

struct nlmsghdr* RtnetlinkMessage::MessageHeader() {
  return reinterpret_cast<struct nlmsghdr*>(message_[0].iov_base);
}

LinkMessage LinkMessage::New(RtnetlinkMessage::Operation request_operation,
                             uint16_t flags,
                             uint32_t seq,
                             uint32_t pid,
                             const struct ifinfomsg* interface_info_header) {
  uint16_t request_type;
  switch (request_operation) {
    case RtnetlinkMessage::Operation::NEW:
      request_type = RTM_NEWLINK;
      break;
    case RtnetlinkMessage::Operation::DEL:
      request_type = RTM_DELLINK;
      break;
    case RtnetlinkMessage::Operation::GET:
      request_type = RTM_GETLINK;
      break;
  }
  bool is_get = request_type == RTM_GETLINK;

  if (is_get) {
    struct rtgenmsg g = {AF_UNSPEC};
    return LinkMessage(request_type, flags, seq, pid, &g, sizeof(g));
  }
  return LinkMessage(request_type, flags, seq, pid, interface_info_header,
                     sizeof(struct ifinfomsg));
}

AddressMessage AddressMessage::New(
    RtnetlinkMessage::Operation request_operation,
    uint16_t flags,
    uint32_t seq,
    uint32_t pid,
    const struct ifaddrmsg* interface_address_header) {
  uint16_t request_type;
  switch (request_operation) {
    case RtnetlinkMessage::Operation::NEW:
      request_type = RTM_NEWADDR;
      break;
    case RtnetlinkMessage::Operation::DEL:
      request_type = RTM_DELADDR;
      break;
    case RtnetlinkMessage::Operation::GET:
      request_type = RTM_GETADDR;
      break;
  }
  bool is_get = request_type == RTM_GETADDR;

  if (is_get) {
    struct rtgenmsg g = {AF_UNSPEC};
    return AddressMessage(request_type, flags, seq, pid, &g, sizeof(g));
  }
  return AddressMessage(request_type, flags, seq, pid, interface_address_header,
                        sizeof(struct ifaddrmsg));
}

RouteMessage RouteMessage::New(RtnetlinkMessage::Operation request_operation,
                               uint16_t flags,
                               uint32_t seq,
                               uint32_t pid,
                               const struct rtmsg* route_message_header) {
  uint16_t request_type;
  switch (request_operation) {
    case RtnetlinkMessage::Operation::NEW:
      request_type = RTM_NEWROUTE;
      break;
    case RtnetlinkMessage::Operation::DEL:
      request_type = RTM_DELROUTE;
      break;
    case RtnetlinkMessage::Operation::GET:
      request_type = RTM_GETROUTE;
      break;
  }
  return RouteMessage(request_type, flags, seq, pid, route_message_header,
                      sizeof(struct rtmsg));
}

RuleMessage RuleMessage::New(RtnetlinkMessage::Operation request_operation,
                             uint16_t flags,
                             uint32_t seq,
                             uint32_t pid,
                             const struct rtmsg* rule_message_header) {
  uint16_t request_type;
  switch (request_operation) {
    case RtnetlinkMessage::Operation::NEW:
      request_type = RTM_NEWRULE;
      break;
    case RtnetlinkMessage::Operation::DEL:
      request_type = RTM_DELRULE;
      break;
    case RtnetlinkMessage::Operation::GET:
      request_type = RTM_GETRULE;
      break;
  }
  return RuleMessage(request_type, flags, seq, pid, rule_message_header,
                     sizeof(rtmsg));
}
}  // namespace quic
