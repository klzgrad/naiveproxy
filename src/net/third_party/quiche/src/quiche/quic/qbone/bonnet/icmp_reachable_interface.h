// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_INTERFACE_H_
#define QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_INTERFACE_H_

namespace quic {

class IcmpReachableInterface {
 public:
  IcmpReachableInterface() = default;
  virtual ~IcmpReachableInterface() = default;

  IcmpReachableInterface(const IcmpReachableInterface&) = delete;
  IcmpReachableInterface& operator=(const IcmpReachableInterface&) = delete;

  IcmpReachableInterface(IcmpReachableInterface&&) = delete;
  IcmpReachableInterface& operator=(IcmpReachableInterface&&) = delete;

  // Initializes this reachability probe.
  virtual bool Init() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_ICMP_REACHABLE_INTERFACE_H_
