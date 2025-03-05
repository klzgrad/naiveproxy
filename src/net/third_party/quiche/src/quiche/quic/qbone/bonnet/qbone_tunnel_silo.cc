// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/qbone_tunnel_silo.h"

namespace quic {

void QboneTunnelSilo::Run() {
  while (ShouldRun()) {
    tunnel_->WaitForEvents();
  }

  QUIC_LOG(INFO) << "Tunnel has disconnected in state: "
                 << tunnel_->StateToString(tunnel_->Disconnect());
}

void QboneTunnelSilo::Quit() {
  QUIC_LOG(INFO) << "Quit called on QboneTunnelSilo";
  quitting_.Notify();
  tunnel_->Wake();
}

bool QboneTunnelSilo::ShouldRun() {
  bool post_init_shutdown_ready =
      only_setup_tun_ &&
      tunnel_->state() == quic::QboneTunnelInterface::STARTED;
  return !quitting_.HasBeenNotified() && !post_init_shutdown_ready;
}

}  // namespace quic
