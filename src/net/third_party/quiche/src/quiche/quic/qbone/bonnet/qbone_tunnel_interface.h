// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_INTERFACE_H_
#define QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_INTERFACE_H_

#include "quiche/quic/qbone/qbone_client.h"

namespace quic {

// Interface for establishing bidirectional communication between a network
// device and a QboneClient.
class QboneTunnelInterface : public quic::QboneClientControlStream::Handler {
 public:
  QboneTunnelInterface() = default;

  QboneTunnelInterface(const QboneTunnelInterface&) = delete;
  QboneTunnelInterface& operator=(const QboneTunnelInterface&) = delete;

  QboneTunnelInterface(QboneTunnelInterface&&) = delete;
  QboneTunnelInterface& operator=(QboneTunnelInterface&&) = delete;

  enum State {
    UNINITIALIZED,
    IP_RANGE_REQUESTED,
    START_REQUESTED,
    STARTED,
    LAME_DUCK_REQUESTED,
    END_REQUESTED,
    ENDED,
    FAILED,
  };

  // Wait and handle any events which occur.
  // Returns true if there are any outstanding requests.
  virtual bool WaitForEvents() = 0;

  // Wakes the tunnel if it is currently in WaitForEvents.
  virtual void Wake() = 0;

  // Disconnect the tunnel, resetting it to an uninitialized state. This will
  // force ConnectIfNeeded to reconnect on the next epoll cycle.
  virtual void ResetTunnel() = 0;

  // Disconnect from the QBONE server.
  virtual State Disconnect() = 0;

  // Callback handling responses from the QBONE server.
  void OnControlRequest(const QboneClientRequest& request) override = 0;

  // Callback handling bad responses from the QBONE server. Currently, this is
  // only called when the response is unparsable.
  void OnControlError() override = 0;

  // Returns a string value of the given state.
  virtual std::string StateToString(State state) = 0;

  virtual QboneClient* client() = 0;

  virtual State state() = 0;

  virtual std::string HealthString() = 0;

  virtual std::string ServerRegionString() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_INTERFACE_H_
