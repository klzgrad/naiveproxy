// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_flow_controller_peer.h"

#include <list>

#include "net/third_party/quiche/src/quic/core/quic_flow_controller.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"

namespace quic {
namespace test {

// static
void QuicFlowControllerPeer::SetSendWindowOffset(
    QuicFlowController* flow_controller,
    QuicStreamOffset offset) {
  flow_controller->send_window_offset_ = offset;
}

// static
void QuicFlowControllerPeer::SetReceiveWindowOffset(
    QuicFlowController* flow_controller,
    QuicStreamOffset offset) {
  flow_controller->receive_window_offset_ = offset;
}

// static
void QuicFlowControllerPeer::SetMaxReceiveWindow(
    QuicFlowController* flow_controller,
    QuicByteCount window_size) {
  flow_controller->receive_window_size_ = window_size;
}

// static
QuicStreamOffset QuicFlowControllerPeer::SendWindowOffset(
    QuicFlowController* flow_controller) {
  return flow_controller->send_window_offset_;
}

// static
QuicByteCount QuicFlowControllerPeer::SendWindowSize(
    QuicFlowController* flow_controller) {
  return flow_controller->SendWindowSize();
}

// static
QuicStreamOffset QuicFlowControllerPeer::ReceiveWindowOffset(
    QuicFlowController* flow_controller) {
  return flow_controller->receive_window_offset_;
}

// static
QuicByteCount QuicFlowControllerPeer::ReceiveWindowSize(
    QuicFlowController* flow_controller) {
  return flow_controller->receive_window_offset_ -
         flow_controller->highest_received_byte_offset_;
}

// static
QuicByteCount QuicFlowControllerPeer::WindowUpdateThreshold(
    QuicFlowController* flow_controller) {
  return flow_controller->WindowUpdateThreshold();
}

}  // namespace test
}  // namespace quic
