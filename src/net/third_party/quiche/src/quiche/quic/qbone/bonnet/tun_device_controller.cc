// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/tun_device_controller.h"

#include <linux/rtnetlink.h>

#include <utility>
#include <vector>

#include "absl/time/clock.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/qbone/qbone_constants.h"
#include "quiche/common/quiche_callbacks.h"

ABSL_FLAG(bool, qbone_tun_device_replace_default_routing_rules, true,
          "If true, will define a rule that points packets sourced from the "
          "qbone interface to the qbone table. This is unnecessary in "
          "environments with no other ipv6 route.");

ABSL_FLAG(int, qbone_route_init_cwnd, 0,
          "If non-zero, will add initcwnd to QBONE routing rules.  Setting "
          "a value below 10 is dangerous and not recommended.");

namespace quic {

bool TunDeviceController::UpdateAddress(const IpRange& desired_range) {
  if (!setup_tun_) {
    return true;
  }

  NetlinkInterface::LinkInfo link_info{};
  if (!netlink_->GetLinkInfo(ifname_, &link_info)) {
    return false;
  }

  std::vector<NetlinkInterface::AddressInfo> addresses;
  if (!netlink_->GetAddresses(link_info.index, 0, &addresses, nullptr)) {
    return false;
  }

  QuicIpAddress desired_address = desired_range.FirstAddressInRange();

  for (const auto& address : addresses) {
    if (!netlink_->ChangeLocalAddress(
            link_info.index, NetlinkInterface::Verb::kRemove,
            address.interface_address, address.prefix_length, 0, 0, {})) {
      return false;
    }
  }

  bool address_updated = netlink_->ChangeLocalAddress(
      link_info.index, NetlinkInterface::Verb::kAdd, desired_address,
      desired_range.prefix_length(), IFA_F_PERMANENT | IFA_F_NODAD,
      RT_SCOPE_LINK, {});

  if (address_updated) {
    current_address_ = desired_address;

    for (const auto& cb : address_update_cbs_) {
      cb(current_address_);
    }
  }

  return address_updated;
}

bool TunDeviceController::UpdateRoutes(
    const IpRange& desired_range, const std::vector<IpRange>& desired_routes) {
  if (!setup_tun_) {
    return true;
  }

  NetlinkInterface::LinkInfo link_info{};
  if (!netlink_->GetLinkInfo(ifname_, &link_info)) {
    QUIC_LOG(ERROR) << "Could not get link info for interface <" << ifname_
                    << ">";
    return false;
  }

  std::vector<NetlinkInterface::RoutingRule> routing_rules;
  if (!netlink_->GetRouteInfo(&routing_rules)) {
    QUIC_LOG(ERROR) << "Unable to get route info";
    return false;
  }

  for (const auto& rule : routing_rules) {
    if (rule.out_interface == link_info.index &&
        rule.table == QboneConstants::kQboneRouteTableId) {
      if (!netlink_->ChangeRoute(NetlinkInterface::Verb::kRemove, rule.table,
                                 rule.destination_subnet, rule.scope,
                                 rule.preferred_source, rule.out_interface,
                                 rule.init_cwnd)) {
        QUIC_LOG(ERROR) << "Unable to remove old route to <"
                        << rule.destination_subnet.ToString() << ">";
        return false;
      }
    }
  }

  if (!UpdateRules(desired_range)) {
    return false;
  }

  QuicIpAddress desired_address = desired_range.FirstAddressInRange();

  std::vector<IpRange> routes(desired_routes.begin(), desired_routes.end());
  routes.emplace_back(*QboneConstants::TerminatorLocalAddressRange());

  for (const auto& route : routes) {
    if (!netlink_->ChangeRoute(NetlinkInterface::Verb::kReplace,
                               QboneConstants::kQboneRouteTableId, route,
                               RT_SCOPE_LINK, desired_address, link_info.index,
                               absl::GetFlag(FLAGS_qbone_route_init_cwnd))) {
      QUIC_LOG(ERROR) << "Unable to add route <" << route.ToString() << ">";
      return false;
    }
  }

  return true;
}

bool TunDeviceController::UpdateRoutesWithRetries(
    const IpRange& desired_range, const std::vector<IpRange>& desired_routes,
    int retries) {
  while (retries-- > 0) {
    if (UpdateRoutes(desired_range, desired_routes)) {
      return true;
    }
    absl::SleepFor(absl::Milliseconds(100));
  }
  return false;
}

bool TunDeviceController::UpdateRules(IpRange desired_range) {
  if (!absl::GetFlag(FLAGS_qbone_tun_device_replace_default_routing_rules)) {
    return true;
  }

  std::vector<NetlinkInterface::IpRule> ip_rules;
  if (!netlink_->GetRuleInfo(&ip_rules)) {
    QUIC_LOG(ERROR) << "Unable to get rule info";
    return false;
  }

  for (const auto& rule : ip_rules) {
    if (rule.table == QboneConstants::kQboneRouteTableId) {
      if (!netlink_->ChangeRule(NetlinkInterface::Verb::kRemove, rule.table,
                                rule.source_range)) {
        QUIC_LOG(ERROR) << "Unable to remove old rule for table <" << rule.table
                        << "> from source <" << rule.source_range.ToString()
                        << ">";
        return false;
      }
    }
  }

  if (!netlink_->ChangeRule(NetlinkInterface::Verb::kAdd,
                            QboneConstants::kQboneRouteTableId,
                            desired_range)) {
    QUIC_LOG(ERROR) << "Unable to add rule for <" << desired_range.ToString()
                    << ">";
    return false;
  }

  return true;
}

QuicIpAddress TunDeviceController::current_address() {
  return current_address_;
}

void TunDeviceController::RegisterAddressUpdateCallback(
    quiche::MultiUseCallback<void(QuicIpAddress)> cb) {
  address_update_cbs_.push_back(std::move(cb));
}

}  // namespace quic
