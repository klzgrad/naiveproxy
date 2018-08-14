// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/mock_network_change_notifier.h"

#include "base/run_loop.h"

namespace net {
namespace test {

MockNetworkChangeNotifier::MockNetworkChangeNotifier()
    : force_network_handles_supported_(false),
      connection_type_(CONNECTION_UNKNOWN) {}
MockNetworkChangeNotifier::~MockNetworkChangeNotifier() = default;

MockNetworkChangeNotifier::ConnectionType
MockNetworkChangeNotifier::GetCurrentConnectionType() const {
  return connection_type_;
}

void MockNetworkChangeNotifier::ForceNetworkHandlesSupported() {
  force_network_handles_supported_ = true;
}

bool MockNetworkChangeNotifier::AreNetworkHandlesCurrentlySupported() const {
  return force_network_handles_supported_;
}

void MockNetworkChangeNotifier::SetConnectedNetworksList(
    const NetworkList& network_list) {
  connected_networks_ = network_list;
}

void MockNetworkChangeNotifier::GetCurrentConnectedNetworks(
    NetworkList* network_list) const {
  network_list->clear();
  *network_list = connected_networks_;
}

void MockNetworkChangeNotifier::NotifyNetworkMadeDefault(
    NetworkChangeNotifier::NetworkHandle network) {
  QueueNetworkMadeDefault(network);
  // Spin the message loop so the notification is delivered.
  base::RunLoop().RunUntilIdle();
}

void MockNetworkChangeNotifier::QueueNetworkMadeDefault(
    NetworkChangeNotifier::NetworkHandle network) {
  NetworkChangeNotifier::NotifyObserversOfSpecificNetworkChange(
      NetworkChangeNotifier::MADE_DEFAULT, network);
}

void MockNetworkChangeNotifier::NotifyNetworkDisconnected(
    NetworkChangeNotifier::NetworkHandle network) {
  QueueNetworkDisconnected(network);
  // Spin the message loop so the notification is delivered.
  base::RunLoop().RunUntilIdle();
}

void MockNetworkChangeNotifier::QueueNetworkDisconnected(
    NetworkChangeNotifier::NetworkHandle network) {
  NetworkChangeNotifier::NotifyObserversOfSpecificNetworkChange(
      NetworkChangeNotifier::DISCONNECTED, network);
}

void MockNetworkChangeNotifier::NotifyNetworkConnected(
    NetworkChangeNotifier::NetworkHandle network) {
  NetworkChangeNotifier::NotifyObserversOfSpecificNetworkChange(
      NetworkChangeNotifier::CONNECTED, network);
  // Spin the message loop so the notification is delivered.
  base::RunLoop().RunUntilIdle();
}

ScopedMockNetworkChangeNotifier::ScopedMockNetworkChangeNotifier()
    : disable_network_change_notifier_for_tests_(
          new NetworkChangeNotifier::DisableForTest()),
      mock_network_change_notifier_(new MockNetworkChangeNotifier()) {}

ScopedMockNetworkChangeNotifier::~ScopedMockNetworkChangeNotifier() = default;

MockNetworkChangeNotifier*
ScopedMockNetworkChangeNotifier::mock_network_change_notifier() {
  return mock_network_change_notifier_.get();
}

}  // namespace test
}  // namespace net
