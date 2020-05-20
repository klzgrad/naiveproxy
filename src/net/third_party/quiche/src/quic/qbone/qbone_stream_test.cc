// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_stream.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_loopback.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_session_base.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

namespace quic {

namespace {

using ::testing::_;
using ::testing::StrictMock;

// MockQuicSession that does not create streams and writes data from
// QuicStream to a string.
class MockQuicSession : public QboneSessionBase {
 public:
  MockQuicSession(QuicConnection* connection, const QuicConfig& config)
      : QboneSessionBase(connection,
                         nullptr /*visitor*/,
                         config,
                         CurrentSupportedVersions(),
                         nullptr /*writer*/) {}

  ~MockQuicSession() override {}

  // Writes outgoing data from QuicStream to a string.
  QuicConsumedData WritevData(
      QuicStreamId id,
      size_t write_length,
      QuicStreamOffset offset,
      StreamSendingState state,
      TransmissionType type,
      quiche::QuicheOptional<EncryptionLevel> level) override {
    if (!writable_) {
      return QuicConsumedData(0, false);
    }

    return QuicConsumedData(write_length, state != StreamSendingState::NO_FIN);
  }

  QboneReadOnlyStream* CreateIncomingStream(QuicStreamId id) override {
    return nullptr;
  }

  const QuicCryptoStream* GetCryptoStream() const override { return nullptr; }
  QuicCryptoStream* GetMutableCryptoStream() override { return nullptr; }

  // Called by QuicStream when they want to close stream.
  MOCK_METHOD3(SendRstStream,
               void(QuicStreamId, QuicRstStreamErrorCode, QuicStreamOffset));

  // Sets whether data is written to buffer, or else if this is write blocked.
  void set_writable(bool writable) { writable_ = writable; }

  // Tracks whether the stream is write blocked and its priority.
  void RegisterReliableStream(QuicStreamId stream_id) {
    // The priority effectively does not matter. Put all streams on the same
    // priority.
    write_blocked_streams()->RegisterStream(
        stream_id,
        /*is_static_stream=*/false,
        /* precedence= */ spdy::SpdyStreamPrecedence(3));
  }

  // The session take ownership of the stream.
  void ActivateReliableStream(std::unique_ptr<QuicStream> stream) {
    ActivateStream(std::move(stream));
  }

  std::unique_ptr<QuicCryptoStream> CreateCryptoStream() override {
    return nullptr;
  }

  MOCK_METHOD1(ProcessPacketFromPeer, void(quiche::QuicheStringPiece));
  MOCK_METHOD1(ProcessPacketFromNetwork, void(quiche::QuicheStringPiece));

 private:
  // Whether data is written to write_buffer_.
  bool writable_ = true;
};

// Packet writer that does nothing. This is required for QuicConnection but
// isn't used for writing data.
class DummyPacketWriter : public QuicPacketWriter {
 public:
  DummyPacketWriter() {}

  // QuicPacketWriter overrides.
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override {
    return WriteResult(WRITE_STATUS_ERROR, 0);
  }

  bool IsWriteBlocked() const override { return false; };

  void SetWritable() override {}

  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& peer_address) const override {
    return 0;
  }

  bool SupportsReleaseTime() const override { return false; }

  bool IsBatchMode() const override { return false; }

  char* GetNextWriteLocation(const QuicIpAddress& self_address,
                             const QuicSocketAddress& peer_address) override {
    return nullptr;
  }

  WriteResult Flush() override { return WriteResult(WRITE_STATUS_OK, 0); }
};

class QboneReadOnlyStreamTest : public ::testing::Test,
                                public QuicConnectionHelperInterface {
 public:
  void CreateReliableQuicStream() {
    // Arbitrary values for QuicConnection.
    Perspective perspective = Perspective::IS_SERVER;
    bool owns_writer = true;

    alarm_factory_ = std::make_unique<test::MockAlarmFactory>();

    connection_.reset(new QuicConnection(
        test::TestConnectionId(0), QuicSocketAddress(TestLoopback(), 0),
        this /*QuicConnectionHelperInterface*/, alarm_factory_.get(),
        new DummyPacketWriter(), owns_writer, perspective,
        ParsedVersionOfIndex(CurrentSupportedVersions(), 0)));
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
    session_ = std::make_unique<StrictMock<MockQuicSession>>(connection_.get(),
                                                             QuicConfig());
    stream_ = new QboneReadOnlyStream(kStreamId, session_.get());
    session_->ActivateReliableStream(
        std::unique_ptr<QboneReadOnlyStream>(stream_));
  }

  ~QboneReadOnlyStreamTest() override {}

  const QuicClock* GetClock() const override { return &clock_; }

  QuicRandom* GetRandomGenerator() override {
    return QuicRandom::GetInstance();
  }

  QuicBufferAllocator* GetStreamSendBufferAllocator() override {
    return &buffer_allocator_;
  }

 protected:
  // The QuicSession will take the ownership.
  QboneReadOnlyStream* stream_;
  std::unique_ptr<StrictMock<MockQuicSession>> session_;
  std::unique_ptr<QuicAlarmFactory> alarm_factory_;
  std::unique_ptr<QuicConnection> connection_;
  // Used to implement the QuicConnectionHelperInterface.
  SimpleBufferAllocator buffer_allocator_;
  MockClock clock_;
  const QuicStreamId kStreamId = QuicUtils::GetFirstUnidirectionalStreamId(
      CurrentSupportedVersions()[0].transport_version,
      Perspective::IS_CLIENT);
};

// Read an entire string.
TEST_F(QboneReadOnlyStreamTest, ReadDataWhole) {
  std::string packet = "Stuff";
  CreateReliableQuicStream();
  QuicStreamFrame frame(kStreamId, true, 0, packet);
  EXPECT_CALL(*session_, ProcessPacketFromPeer("Stuff"));
  stream_->OnStreamFrame(frame);
}

// Test buffering.
TEST_F(QboneReadOnlyStreamTest, ReadBuffered) {
  CreateReliableQuicStream();
  std::string packet = "Stuf";
  {
    QuicStreamFrame frame(kStreamId, false, 0, packet);
    stream_->OnStreamFrame(frame);
  }
  // We didn't write 5 bytes yet...

  packet = "f";
  EXPECT_CALL(*session_, ProcessPacketFromPeer("Stuff"));
  {
    QuicStreamFrame frame(kStreamId, true, 4, packet);
    stream_->OnStreamFrame(frame);
  }
}

TEST_F(QboneReadOnlyStreamTest, ReadOutOfOrder) {
  CreateReliableQuicStream();
  std::string packet = "f";
  {
    QuicStreamFrame frame(kStreamId, true, 4, packet);
    stream_->OnStreamFrame(frame);
  }

  packet = "S";
  {
    QuicStreamFrame frame(kStreamId, false, 0, packet);
    stream_->OnStreamFrame(frame);
  }

  packet = "tuf";
  EXPECT_CALL(*session_, ProcessPacketFromPeer("Stuff"));
  {
    QuicStreamFrame frame(kStreamId, false, 1, packet);
    stream_->OnStreamFrame(frame);
  }
}

// Test buffering too many bytes.
TEST_F(QboneReadOnlyStreamTest, ReadBufferedTooLarge) {
  CreateReliableQuicStream();
  std::string packet = "0123456789";
  int iterations = (QboneConstants::kMaxQbonePacketBytes / packet.size()) + 2;
  EXPECT_CALL(*session_,
              SendRstStream(kStreamId, QUIC_BAD_APPLICATION_PAYLOAD, _));
  for (int i = 0; i < iterations; ++i) {
    QuicStreamFrame frame(kStreamId, i == (iterations - 1), i * packet.size(),
                          packet);
    if (!stream_->reading_stopped()) {
      stream_->OnStreamFrame(frame);
    }
  }
  // We should have nothing written to the network and the stream
  // should have stopped reading.
  EXPECT_TRUE(stream_->reading_stopped());
}

}  // namespace

}  // namespace quic
