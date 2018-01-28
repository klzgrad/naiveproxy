// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_TEST_TOOLS_QUIC_DISPATCHER_PEER_H_
#define NET_TOOLS_QUIC_TEST_TOOLS_QUIC_DISPATCHER_PEER_H_

#include "base/macros.h"
#include "net/tools/quic/quic_dispatcher.h"

namespace net {

class QuicPacketWriterWrapper;

namespace test {

class QuicDispatcherPeer {
 public:
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

  static QuicDispatcher::WriteBlockedList* GetWriteBlockedList(
      QuicDispatcher* dispatcher);

  // Get the dispatcher's record of the last error reported to its framer
  // visitor's OnError() method.  Then set that record to QUIC_NO_ERROR.
  static QuicErrorCode GetAndClearLastError(QuicDispatcher* dispatcher);

  static QuicBufferedPacketStore* GetBufferedPackets(
      QuicDispatcher* dispatcher);

  static const QuicDispatcher::SessionMap& session_map(
      QuicDispatcher* dispatcher);

  static void set_new_sessions_allowed_per_event_loop(
      QuicDispatcher* dispatcher,
      size_t num_session_allowed);

  static void SendPublicReset(QuicDispatcher* dispatcher,
                              const QuicSocketAddress& server_address,
                              const QuicSocketAddress& client_address,
                              QuicConnectionId connection_id);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicDispatcherPeer);
};

}  // namespace test
}  // namespace net

#endif  // NET_TOOLS_QUIC_TEST_TOOLS_QUIC_DISPATCHER_PEER_H_
