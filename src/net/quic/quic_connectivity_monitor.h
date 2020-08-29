// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUIC_CONNECTIVITY_MONITOR_H_
#define NET_QUIC_QUIC_CONNECTIVITY_MONITOR_H_

#include "net/base/network_change_notifier.h"
#include "net/quic/quic_chromium_client_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"

namespace net {

// Responsible for monitoring path degrading detection/recovery events on the
// default network interface.
// Reset all raw observations (reported by sessions) when the default network
// is changed, which happens either:
// - via OnDefaultNetworkUpdated if NetworkHandle is supported on the platform;
// - via OnIPAddressChanged otherwise.
class NET_EXPORT_PRIVATE QuicConnectivityMonitor
    : public QuicChromiumClientSession::ConnectivityObserver {
 public:
  explicit QuicConnectivityMonitor(
      NetworkChangeNotifier::NetworkHandle default_network);

  ~QuicConnectivityMonitor() override;

  // Returns the number of sessions that are currently degrading on the default
  // network interface.
  size_t GetNumDegradingSessions() const;

  // Called to set up the initial default network, which happens when the
  // default network tracking is lost upon |this| creation.
  void SetInitialDefaultNetwork(
      NetworkChangeNotifier::NetworkHandle default_network);

  // Called when NetworkHandle is supported and the default network interface
  // used by the platform is updated.
  void OnDefaultNetworkUpdated(
      NetworkChangeNotifier::NetworkHandle default_network);

  // Called when NetworkHandle is NOT supported and the IP address of the
  // primary interface changes. This includes when the primary interface itself
  // changes.
  void OnIPAddressChanged();

  // Called when |session| is marked as going away due to IP address change.
  void OnSessionGoingAwayOnIPAddressChange(QuicChromiumClientSession* session);

  // QuicChromiumClientSession::ConnectivityObserver implementation.
  void OnSessionPathDegrading(
      QuicChromiumClientSession* session,
      NetworkChangeNotifier::NetworkHandle network) override;

  void OnSessionResumedPostPathDegrading(
      QuicChromiumClientSession* session,
      NetworkChangeNotifier::NetworkHandle network) override;

  void OnSessionRemoved(QuicChromiumClientSession* session) override;

 private:
  // If NetworkHandle is not supported, always set to
  // NetworkChangeNotifier::kInvalidNetworkHandle.
  NetworkChangeNotifier::NetworkHandle default_network_;
  // Sessions that are currently degrading on the |default_network_|.
  quic::QuicHashSet<QuicChromiumClientSession*> degrading_sessions_;

  base::WeakPtrFactory<QuicConnectivityMonitor> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(QuicConnectivityMonitor);
};

}  // namespace net

#endif  // NET_QUIC_QUIC_CONNECTIVITY_MONITOR_H_
