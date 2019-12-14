// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_RTNETLINK_MESSAGE_H_
#define QUICHE_QUIC_QBONE_PLATFORM_RTNETLINK_MESSAGE_H_

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <memory>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {

// This base class is used to construct an array struct iovec that represents a
// rtnetlink message as defined in man 7 rtnet. Padding for message header
// alignment to conform NLMSG_* and RTA_* macros is added at the end of each
// iovec::iov_base.
class RtnetlinkMessage {
 public:
  virtual ~RtnetlinkMessage();

  enum class Operation {
    NEW,
    DEL,
    GET,
  };

  // Appends a struct rtattr to the message. nlmsg_len and rta_len is handled
  // properly.
  // Override this to perform check on type.
  virtual void AppendAttribute(uint16_t type,
                               const void* data,
                               uint16_t data_length);

  // Builds the array of iovec that can be fed into sendmsg directly.
  std::unique_ptr<struct iovec[]> BuildIoVec() const;

  // The size of the array of iovec if BuildIovec is called.
  size_t IoVecSize() const;

 protected:
  // Subclass should add their own message header immediately after the
  // nlmsghdr. Make this private to force the creation of such header.
  RtnetlinkMessage(uint16_t type,
                   uint16_t flags,
                   uint32_t seq,
                   uint32_t pid,
                   const void* payload_header,
                   size_t payload_header_length);

  // Adjusts nlmsg_len in the header assuming additional_data_length is appended
  // at the end.
  void AdjustMessageLength(size_t additional_data_length);

 private:
  // Convenient function for accessing the nlmsghdr.
  struct nlmsghdr* MessageHeader();

  std::vector<struct iovec> message_;
};

// Message for manipulating link level configuration as defined in man 7
// rtnetlink. RTM_NEWLINK, RTM_DELLINK and RTM_GETLINK are supported.
class LinkMessage : public RtnetlinkMessage {
 public:
  static LinkMessage New(RtnetlinkMessage::Operation request_operation,
                         uint16_t flags,
                         uint32_t seq,
                         uint32_t pid,
                         const struct ifinfomsg* interface_info_header);

 private:
  using RtnetlinkMessage::RtnetlinkMessage;
};

// Message for manipulating address level configuration as defined in man 7
// rtnetlink. RTM_NEWADDR, RTM_NEWADDR and RTM_GETADDR are supported.
class AddressMessage : public RtnetlinkMessage {
 public:
  static AddressMessage New(RtnetlinkMessage::Operation request_operation,
                            uint16_t flags,
                            uint32_t seq,
                            uint32_t pid,
                            const struct ifaddrmsg* interface_address_header);

 private:
  using RtnetlinkMessage::RtnetlinkMessage;
};

// Message for manipulating routing table as defined in man 7 rtnetlink.
// RTM_NEWROUTE, RTM_DELROUTE and RTM_GETROUTE are supported.
class RouteMessage : public RtnetlinkMessage {
 public:
  static RouteMessage New(RtnetlinkMessage::Operation request_operation,
                          uint16_t flags,
                          uint32_t seq,
                          uint32_t pid,
                          const struct rtmsg* route_message_header);

 private:
  using RtnetlinkMessage::RtnetlinkMessage;
};

class RuleMessage : public RtnetlinkMessage {
 public:
  static RuleMessage New(RtnetlinkMessage::Operation request_operation,
                         uint16_t flags,
                         uint32_t seq,
                         uint32_t pid,
                         const struct rtmsg* rule_message_header);

 private:
  using RtnetlinkMessage::RtnetlinkMessage;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_RTNETLINK_MESSAGE_H_
