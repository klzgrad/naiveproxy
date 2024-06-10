// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_DISPATCHER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_DISPATCHER_PEER_H_

#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_dispatcher.h"

namespace quic {

class QuicPacketWriterWrapper;

namespace test {

class QuicDispatcherPeer {
 public:
  QuicDispatcherPeer() = delete;

  static QuicTimeWaitListManager* GetTimeWaitListManager(
      QuicDispatcher* dispatcher);

  static void SetTimeWaitListManager(
      QuicDispatcher* dispatcher,
      QuicTimeWaitListManager* time_wait_list_manager);

  // Injects |writer| into |dispatcher| as the shared writer.
  static void UseWriter(QuicDispatcher* dispatcher,
                        QuicPacketWriterWrapper* writer);

  static QuicPacketWriter* GetWriter(QuicDispatcher* dispatcher);

  static QuicCompressedCertsCache* GetCache(QuicDispatcher* dispatcher);

  static QuicConnectionHelperInterface* GetHelper(QuicDispatcher* dispatcher);

  static QuicAlarmFactory* GetAlarmFactory(QuicDispatcher* dispatcher);

  static QuicBlockedWriterList* GetWriteBlockedList(QuicDispatcher* dispatcher);

  // Get the dispatcher's record of the last error reported to its framer
  // visitor's OnError() method.  Then set that record to QUIC_NO_ERROR.
  static QuicErrorCode GetAndClearLastError(QuicDispatcher* dispatcher);

  static QuicBufferedPacketStore* GetBufferedPackets(
      QuicDispatcher* dispatcher);

  static void set_new_sessions_allowed_per_event_loop(
      QuicDispatcher* dispatcher, size_t num_session_allowed);

  static void SendPublicReset(
      QuicDispatcher* dispatcher, const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, QuicConnectionId connection_id,
      bool ietf_quic, size_t received_packet_length,
      std::unique_ptr<QuicPerPacketContext> packet_context);

  static std::unique_ptr<QuicPerPacketContext> GetPerPacketContext(
      QuicDispatcher* dispatcher);

  static void RestorePerPacketContext(QuicDispatcher* dispatcher,
                                      std::unique_ptr<QuicPerPacketContext>);

  static std::string SelectAlpn(QuicDispatcher* dispatcher,
                                const std::vector<std::string>& alpns);

  // Get the first session in the session map. Returns nullptr if the map is
  // empty.
  static QuicSession* GetFirstSessionIfAny(QuicDispatcher* dispatcher);

  // Find the corresponding session if exsits.
  static const QuicSession* FindSession(const QuicDispatcher* dispatcher,
                                        QuicConnectionId id);

  static QuicAlarm* GetClearResetAddressesAlarm(QuicDispatcher* dispatcher);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_DISPATCHER_PEER_H_
