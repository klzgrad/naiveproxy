// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_SILO_H_
#define QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_SILO_H_

#include "absl/synchronization/notification.h"
#include "quiche/quic/platform/api/quic_thread.h"
#include "quiche/quic/qbone/bonnet/qbone_tunnel_interface.h"

namespace quic {

// QboneTunnelSilo is a thread that initializes and evaluates a QboneTunnel's
// event loop.
class QboneTunnelSilo : public QuicThread {
 public:
  // Does not take ownership of |tunnel|
  explicit QboneTunnelSilo(QboneTunnelInterface* tunnel, bool only_setup_tun)
      : QuicThread("QboneTunnelSilo"),
        tunnel_(tunnel),
        only_setup_tun_(only_setup_tun) {}

  QboneTunnelSilo(const QboneTunnelSilo&) = delete;
  QboneTunnelSilo& operator=(const QboneTunnelSilo&) = delete;

  QboneTunnelSilo(QboneTunnelSilo&&) = delete;
  QboneTunnelSilo& operator=(QboneTunnelSilo&&) = delete;

  // Terminates the tunnel's event loop. This silo must still be joined.
  void Quit();

 protected:
  void Run() override;

 private:
  bool ShouldRun();

  QboneTunnelInterface* tunnel_;

  absl::Notification quitting_;

  const bool only_setup_tun_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_QBONE_TUNNEL_SILO_H_
