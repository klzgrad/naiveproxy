// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_connectivity_monitor.h"

#include "base/metrics/histogram_macros.h"

namespace net {

QuicConnectivityMonitor::QuicConnectivityMonitor(
    NetworkChangeNotifier::NetworkHandle default_network)
    : default_network_(default_network) {}

QuicConnectivityMonitor::~QuicConnectivityMonitor() = default;

size_t QuicConnectivityMonitor::GetNumDegradingSessions() const {
  return degrading_sessions_.size();
}

void QuicConnectivityMonitor::SetInitialDefaultNetwork(
    NetworkChangeNotifier::NetworkHandle default_network) {
  default_network_ = default_network;
}

void QuicConnectivityMonitor::OnSessionPathDegrading(
    QuicChromiumClientSession* session,
    NetworkChangeNotifier::NetworkHandle network) {
  if (network == default_network_)
    degrading_sessions_.insert(session);
}

void QuicConnectivityMonitor::OnSessionResumedPostPathDegrading(
    QuicChromiumClientSession* session,
    NetworkChangeNotifier::NetworkHandle network) {
  if (network == default_network_)
    degrading_sessions_.erase(session);
}

void QuicConnectivityMonitor::OnSessionRemoved(
    QuicChromiumClientSession* session) {
  degrading_sessions_.erase(session);
}

void QuicConnectivityMonitor::OnDefaultNetworkUpdated(
    NetworkChangeNotifier::NetworkHandle default_network) {
  default_network_ = default_network;
  degrading_sessions_.clear();
}

void QuicConnectivityMonitor::OnIPAddressChanged() {
  // If NetworkHandle is supported, connectivity monitor will receive
  // notifications via OnDefaultNetworkUpdated.
  if (NetworkChangeNotifier::AreNetworkHandlesSupported())
    return;

  DCHECK_EQ(default_network_, NetworkChangeNotifier::kInvalidNetworkHandle);
  degrading_sessions_.clear();
}

void QuicConnectivityMonitor::OnSessionGoingAwayOnIPAddressChange(
    QuicChromiumClientSession* session) {
  // This should only be called after ConnectivityMonitor gets notified via
  // OnIPAddressChanged().
  DCHECK(degrading_sessions_.empty());
  // |session| that encounters IP address change will lose track which network
  // it is bound to. Future connectivity monitoring may be misleading.
  session->RemoveConnectivityObserver(this);
}

}  // namespace net
