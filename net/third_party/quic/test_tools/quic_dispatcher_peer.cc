// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/test_tools/quic_dispatcher_peer.h"

#include "net/third_party/quic/core/quic_dispatcher.h"
#include "net/third_party/quic/core/quic_packet_writer_wrapper.h"

namespace quic {
namespace test {

// static
void QuicDispatcherPeer::SetTimeWaitListManager(
    QuicDispatcher* dispatcher,
    QuicTimeWaitListManager* time_wait_list_manager) {
  dispatcher->time_wait_list_manager_.reset(time_wait_list_manager);
}

// static
void QuicDispatcherPeer::UseWriter(QuicDispatcher* dispatcher,
                                   QuicPacketWriterWrapper* writer) {
  writer->set_writer(dispatcher->writer_.release());
  dispatcher->writer_.reset(writer);
}

// static
QuicPacketWriter* QuicDispatcherPeer::GetWriter(QuicDispatcher* dispatcher) {
  return dispatcher->writer_.get();
}

// static
QuicCompressedCertsCache* QuicDispatcherPeer::GetCache(
    QuicDispatcher* dispatcher) {
  return dispatcher->compressed_certs_cache();
}

// static
QuicConnectionHelperInterface* QuicDispatcherPeer::GetHelper(
    QuicDispatcher* dispatcher) {
  return dispatcher->helper_.get();
}

// static
QuicAlarmFactory* QuicDispatcherPeer::GetAlarmFactory(
    QuicDispatcher* dispatcher) {
  return dispatcher->alarm_factory_.get();
}

// static
QuicDispatcher::WriteBlockedList* QuicDispatcherPeer::GetWriteBlockedList(
    QuicDispatcher* dispatcher) {
  return &dispatcher->write_blocked_list_;
}

// static
QuicErrorCode QuicDispatcherPeer::GetAndClearLastError(
    QuicDispatcher* dispatcher) {
  QuicErrorCode ret = dispatcher->last_error_;
  dispatcher->last_error_ = QUIC_NO_ERROR;
  return ret;
}

// static
QuicBufferedPacketStore* QuicDispatcherPeer::GetBufferedPackets(
    QuicDispatcher* dispatcher) {
  return &(dispatcher->buffered_packets_);
}

// static
const QuicDispatcher::SessionMap& QuicDispatcherPeer::session_map(
    QuicDispatcher* dispatcher) {
  return dispatcher->session_map();
}

// static
void QuicDispatcherPeer::set_new_sessions_allowed_per_event_loop(
    QuicDispatcher* dispatcher,
    size_t num_session_allowed) {
  return dispatcher->set_new_sessions_allowed_per_event_loop(
      num_session_allowed);
}

// static
void QuicDispatcherPeer::SendPublicReset(
    QuicDispatcher* dispatcher,
    const QuicSocketAddress& server_address,
    const QuicSocketAddress& client_address,
    QuicConnectionId connection_id) {
  dispatcher->time_wait_list_manager()->SendPublicReset(
      server_address, client_address, connection_id,
      dispatcher->framer_.last_packet_is_ietf_quic());
}

// static
std::unique_ptr<QuicDispatcher::PerPacketContext>
QuicDispatcherPeer::GetPerPacketContext(QuicDispatcher* dispatcher) {
  return dispatcher->GetPerPacketContext();
}

// static
void QuicDispatcherPeer::RestorePerPacketContext(
    QuicDispatcher* dispatcher,
    std::unique_ptr<QuicDispatcher::PerPacketContext> context) {
  dispatcher->RestorePerPacketContext(std::move(context));
}

}  // namespace test
}  // namespace quic
