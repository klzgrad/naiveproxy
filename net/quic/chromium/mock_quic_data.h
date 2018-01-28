// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_MOCK_QUIC_DATA_H_
#define NET_QUIC_CHROMIUM_MOCK_QUIC_DATA_H_

#include "net/quic/core/quic_packets.h"
#include "net/socket/socket_test_util.h"

namespace net {
namespace test {

// Helper class to encapsulate MockReads and MockWrites for QUIC.
// Simplify ownership issues and the interaction with the MockSocketFactory.
class MockQuicData {
 public:
  MockQuicData();
  ~MockQuicData();

  // Makes the Connect() call return |rv| either
  // synchronusly or asynchronously based on |mode|.
  void AddConnect(IoMode mode, int rv);

  // Adds a synchronous read at the next sequence number which will read
  // |packet|.
  void AddSynchronousRead(std::unique_ptr<QuicEncryptedPacket> packet);

  // Adds an asynchronous read at the next sequence number which will read
  // |packet|.
  void AddRead(std::unique_ptr<QuicEncryptedPacket> packet);

  // Adds a read at the next sequence number which will return |rv| either
  // synchronously or asynchronously based on |mode|.
  void AddRead(IoMode mode, int rv);

  // Adds a synchronous write at the next sequence number which will write
  // |packet|.
  void AddWrite(std::unique_ptr<QuicEncryptedPacket> packet);

  // Adds an asynchronous write at the next sequence number which will write
  // |packet|.
  void AddAsyncWrite(std::unique_ptr<QuicEncryptedPacket> packet);

  // Adds a write at the next sequence number which will return |rv| either
  // synchronously or asynchronously based on |mode|.
  void AddWrite(IoMode mode, int rv);

  // Adds the reads and writes to |factory|.
  void AddSocketDataToFactory(MockClientSocketFactory* factory);

  // Returns true if all reads have been consumed.
  bool AllReadDataConsumed();

  // Returns true if all writes have been consumed.
  bool AllWriteDataConsumed();

  // Resumes I/O after it is paused.
  void Resume();

  // Creates a new SequencedSocketData owned by this instance of MockQuicData.
  // Returns a pointer to the newly created SequencedSocketData.
  SequencedSocketData* InitializeAndGetSequencedSocketData();

  SequencedSocketData* GetSequencedSocketData();

 private:
  std::vector<std::unique_ptr<QuicEncryptedPacket>> packets_;
  std::unique_ptr<MockConnect> connect_;
  std::vector<MockWrite> writes_;
  std::vector<MockRead> reads_;
  size_t sequence_number_;
  std::unique_ptr<SequencedSocketData> socket_data_;
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_CHROMIUM_MOCK_QUIC_DATA_H_
