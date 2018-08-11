// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_TIME_WAIT_LIST_MANAGER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_TIME_WAIT_LIST_MANAGER_H_

#include "net/third_party/quic/core/quic_time_wait_list_manager.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {
namespace test {

class MockTimeWaitListManager : public QuicTimeWaitListManager {
 public:
  MockTimeWaitListManager(QuicPacketWriter* writer,
                          Visitor* visitor,
                          QuicConnectionHelperInterface* helper,
                          QuicAlarmFactory* alarm_factory);
  ~MockTimeWaitListManager() override;

  MOCK_METHOD5(AddConnectionIdToTimeWait,
               void(QuicConnectionId connection_id,
                    ParsedQuicVersion version,
                    bool ietf_quic,
                    bool connection_rejected_statelessly,
                    std::vector<std::unique_ptr<QuicEncryptedPacket>>*
                        termination_packets));

  void QuicTimeWaitListManager_AddConnectionIdToTimeWait(
      QuicConnectionId connection_id,
      ParsedQuicVersion version,
      bool ietf_quic,
      bool connection_rejected_statelessly,
      std::vector<std::unique_ptr<QuicEncryptedPacket>>* termination_packets) {
    QuicTimeWaitListManager::AddConnectionIdToTimeWait(
        connection_id, version, ietf_quic, connection_rejected_statelessly,
        termination_packets);
  }

  MOCK_METHOD3(ProcessPacket,
               void(const QuicSocketAddress& server_address,
                    const QuicSocketAddress& client_address,
                    QuicConnectionId connection_id));

  MOCK_METHOD5(SendVersionNegotiationPacket,
               void(QuicConnectionId connection_id,
                    bool ietf_quic,
                    const ParsedQuicVersionVector& supported_versions,
                    const QuicSocketAddress& server_address,
                    const QuicSocketAddress& client_address));
};

}  // namespace test
}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_TIME_WAIT_LIST_MANAGER_H_
