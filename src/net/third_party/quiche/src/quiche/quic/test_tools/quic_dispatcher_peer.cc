// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_dispatcher_peer.h"

#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_packet_writer_wrapper.h"

namespace quic {
namespace test {

// static
QuicTimeWaitListManager* QuicDispatcherPeer::GetTimeWaitListManager(
    QuicDispatcher* dispatcher) {
  return dispatcher->time_wait_list_manager_.get();
}

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
void QuicDispatcherPeer::set_new_sessions_allowed_per_event_loop(
    QuicDispatcher* dispatcher, size_t num_session_allowed) {
  dispatcher->new_sessions_allowed_per_event_loop_ = num_session_allowed;
}

// static
void QuicDispatcherPeer::SendPublicReset(
    QuicDispatcher* dispatcher, const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, QuicConnectionId connection_id,
    bool ietf_quic, size_t received_packet_length,
    std::unique_ptr<QuicPerPacketContext> packet_context) {
  dispatcher->time_wait_list_manager()->SendPublicReset(
      self_address, peer_address, connection_id, ietf_quic,
      received_packet_length, std::move(packet_context));
}

// static
std::unique_ptr<QuicPerPacketContext> QuicDispatcherPeer::GetPerPacketContext(
    QuicDispatcher* dispatcher) {
  return dispatcher->GetPerPacketContext();
}

// static
void QuicDispatcherPeer::RestorePerPacketContext(
    QuicDispatcher* dispatcher, std::unique_ptr<QuicPerPacketContext> context) {
  dispatcher->RestorePerPacketContext(std::move(context));
}

// static
std::string QuicDispatcherPeer::SelectAlpn(
    QuicDispatcher* dispatcher, const std::vector<std::string>& alpns) {
  return dispatcher->SelectAlpn(alpns);
}

// static
QuicSession* QuicDispatcherPeer::GetFirstSessionIfAny(
    QuicDispatcher* dispatcher) {
  if (dispatcher->reference_counted_session_map_.empty()) {
    return nullptr;
  }
  return dispatcher->reference_counted_session_map_.begin()->second.get();
}

// static
const QuicSession* QuicDispatcherPeer::FindSession(
    const QuicDispatcher* dispatcher, QuicConnectionId id) {
  auto it = dispatcher->reference_counted_session_map_.find(id);
  return (it == dispatcher->reference_counted_session_map_.end())
             ? nullptr
             : it->second.get();
}

// static
QuicAlarm* QuicDispatcherPeer::GetClearResetAddressesAlarm(
    QuicDispatcher* dispatcher) {
  return dispatcher->clear_stateless_reset_addresses_alarm_.get();
}

}  // namespace test
}  // namespace quic
