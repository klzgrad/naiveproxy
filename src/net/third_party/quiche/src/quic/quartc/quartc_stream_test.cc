// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_stream.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_clock.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_mem_slice_vector.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_factory.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

namespace quic {

namespace {

static const QuicStreamId kStreamId = 5;

// MockQuicSession that does not create streams and writes data from
// QuicStream to a string.
class MockQuicSession : public QuicSession {
 public:
  MockQuicSession(QuicConnection* connection,
                  const QuicConfig& config,
                  std::string* write_buffer)
      : QuicSession(connection,
                    nullptr /*visitor*/,
                    config,
                    CurrentSupportedVersions(),
                    /*num_expected_unidirectional_static_streams = */ 0),
        write_buffer_(write_buffer) {}

  ~MockQuicSession() override {}

  // Writes outgoing data from QuicStream to a string.
  QuicConsumedData WritevData(QuicStream* stream,
                              QuicStreamId /*id*/,
                              size_t write_length,
                              QuicStreamOffset offset,
                              StreamSendingState state) override {
    if (!writable_) {
      return QuicConsumedData(0, false);
    }

    // WritevData does not pass down a iovec, data is saved in stream before
    // data is consumed. Retrieve data from stream.
    char* buf = new char[write_length];
    QuicDataWriter writer(write_length, buf, NETWORK_BYTE_ORDER);
    if (write_length > 0) {
      stream->WriteStreamData(offset, write_length, &writer);
    }
    write_buffer_->append(buf, write_length);
    delete[] buf;
    return QuicConsumedData(write_length, state != StreamSendingState::NO_FIN);
  }

  QuartcStream* CreateIncomingStream(QuicStreamId /*id*/) override {
    return nullptr;
  }

  QuartcStream* CreateIncomingStream(PendingStream* /*pending*/) override {
    return nullptr;
  }

  const QuicCryptoStream* GetCryptoStream() const override { return nullptr; }
  QuicCryptoStream* GetMutableCryptoStream() override { return nullptr; }
  bool ShouldKeepConnectionAlive() const override {
    return GetNumActiveStreams() > 0;
  }

  // Called by QuicStream when they want to close stream.
  void SendRstStream(QuicStreamId /*id*/,
                     QuicRstStreamErrorCode /*error*/,
                     QuicStreamOffset /*bytes_written*/) override {}

  // Sets whether data is written to buffer, or else if this is write blocked.
  void set_writable(bool writable) { writable_ = writable; }

  // Tracks whether the stream is write blocked and its priority.
  void RegisterReliableStream(QuicStreamId stream_id,
                              spdy::SpdyPriority priority) {
    write_blocked_streams()->RegisterStream(
        stream_id,
        /*is_static_stream=*/false, spdy::SpdyStreamPrecedence(priority));
  }

  // The session take ownership of the stream.
  void ActivateReliableStream(std::unique_ptr<QuicStream> stream) {
    ActivateStream(std::move(stream));
  }

 private:
  // Stores written data from ReliableQuicStreamAdapter.
  std::string* write_buffer_;
  // Whether data is written to write_buffer_.
  bool writable_ = true;
};

// Packet writer that does nothing. This is required for QuicConnection but
// isn't used for writing data.
class DummyPacketWriter : public QuicPacketWriter {
 public:
  DummyPacketWriter() {}

  // QuicPacketWriter overrides.
  WriteResult WritePacket(const char* /*buffer*/,
                          size_t /*buf_len*/,
                          const QuicIpAddress& /*self_address*/,
                          const QuicSocketAddress& /*peer_address*/,
                          PerPacketOptions* /*options*/) override {
    return WriteResult(WRITE_STATUS_ERROR, 0);
  }

  bool IsWriteBlocked() const override { return false; }

  void SetWritable() override {}

  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& /*peer_address*/) const override {
    return 0;
  }

  bool SupportsReleaseTime() const override { return false; }

  bool IsBatchMode() const override { return false; }

  char* GetNextWriteLocation(
      const QuicIpAddress& /*self_address*/,
      const QuicSocketAddress& /*peer_address*/) override {
    return nullptr;
  }

  WriteResult Flush() override { return WriteResult(WRITE_STATUS_OK, 0); }
};

class MockQuartcStreamDelegate : public QuartcStream::Delegate {
 public:
  MockQuartcStreamDelegate(QuicStreamId id, std::string* read_buffer)
      : id_(id), read_buffer_(read_buffer) {}

  void OnBufferChanged(QuartcStream* stream) override {
    last_bytes_buffered_ = stream->BufferedDataBytes();
    last_bytes_pending_retransmission_ = stream->BytesPendingRetransmission();
  }

  size_t OnReceived(QuartcStream* stream,
                    iovec* iov,
                    size_t iov_length,
                    bool /*fin*/) override {
    EXPECT_EQ(id_, stream->id());
    EXPECT_EQ(stream->ReadOffset(), read_buffer_->size());
    size_t bytes_consumed = 0;
    for (size_t i = 0; i < iov_length; ++i) {
      read_buffer_->append(static_cast<const char*>(iov[i].iov_base),
                           iov[i].iov_len);
      bytes_consumed += iov[i].iov_len;
    }
    return bytes_consumed;
  }

  void OnClose(QuartcStream* /*stream*/) override { closed_ = true; }

  bool closed() { return closed_; }

  QuicByteCount last_bytes_buffered() { return last_bytes_buffered_; }
  QuicByteCount last_bytes_pending_retransmission() {
    return last_bytes_pending_retransmission_;
  }

 protected:
  QuicStreamId id_;
  // Data read by the QuicStream.
  std::string* read_buffer_;
  // Whether the QuicStream is closed.
  bool closed_ = false;

  // Last amount of data observed as buffered.
  QuicByteCount last_bytes_buffered_ = 0;
  QuicByteCount last_bytes_pending_retransmission_ = 0;
};

class QuartcStreamTest : public QuicTest, public QuicConnectionHelperInterface {
 public:
  QuartcStreamTest() {
    // Required to correctly handle StopReading().
    SetQuicReloadableFlag(quic_stop_reading_when_level_triggered, true);
  }

  ~QuartcStreamTest() override = default;

  void CreateReliableQuicStream() {
    // Arbitrary values for QuicConnection.
    Perspective perspective = Perspective::IS_SERVER;
    QuicIpAddress ip;
    ip.FromString("0.0.0.0");
    bool owns_writer = true;

    alarm_factory_ = QuicMakeUnique<test::MockAlarmFactory>();

    connection_ = QuicMakeUnique<QuicConnection>(
        QuicUtils::CreateZeroConnectionId(
            CurrentSupportedVersions()[0].transport_version),
        QuicSocketAddress(ip, 0), this /*QuicConnectionHelperInterface*/,
        alarm_factory_.get(), new DummyPacketWriter(), owns_writer, perspective,
        ParsedVersionOfIndex(CurrentSupportedVersions(), 0));
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1));
    session_ = QuicMakeUnique<MockQuicSession>(connection_.get(), QuicConfig(),
                                               &write_buffer_);
    mock_stream_delegate_ =
        QuicMakeUnique<MockQuartcStreamDelegate>(kStreamId, &read_buffer_);
    stream_ = new QuartcStream(kStreamId, session_.get());
    stream_->SetDelegate(mock_stream_delegate_.get());
    session_->ActivateReliableStream(std::unique_ptr<QuartcStream>(stream_));
  }

  const QuicClock* GetClock() const override { return &clock_; }

  QuicRandom* GetRandomGenerator() override {
    return QuicRandom::GetInstance();
  }

  QuicBufferAllocator* GetStreamSendBufferAllocator() override {
    return &buffer_allocator_;
  }

 protected:
  // The QuicSession will take the ownership.
  QuartcStream* stream_;
  std::unique_ptr<MockQuartcStreamDelegate> mock_stream_delegate_;
  std::unique_ptr<MockQuicSession> session_;
  // Data written by the ReliableQuicStreamAdapterTest.
  std::string write_buffer_;
  // Data read by the ReliableQuicStreamAdapterTest.
  std::string read_buffer_;
  std::unique_ptr<QuicAlarmFactory> alarm_factory_;
  std::unique_ptr<QuicConnection> connection_;
  // Used to implement the QuicConnectionHelperInterface.
  SimpleBufferAllocator buffer_allocator_;
  MockClock clock_;
};

// Write an entire string.
TEST_F(QuartcStreamTest, WriteDataWhole) {
  CreateReliableQuicStream();
  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);
  EXPECT_EQ("Foo bar", write_buffer_);
}

// Write part of a string.
TEST_F(QuartcStreamTest, WriteDataPartial) {
  CreateReliableQuicStream();
  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 5)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);
  EXPECT_EQ("Foo b", write_buffer_);
}

// Test that a QuartcStream buffers writes correctly.
TEST_F(QuartcStreamTest, StreamBuffersData) {
  CreateReliableQuicStream();

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});

  // The stream is not yet writable, so data will be buffered.
  session_->set_writable(false);
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  // Check that data is buffered.
  EXPECT_TRUE(stream_->HasBufferedData());
  EXPECT_EQ(7u, stream_->BufferedDataBytes());

  // Check that the stream told its delegate about the buffer change.
  EXPECT_EQ(7u, mock_stream_delegate_->last_bytes_buffered());

  // Check that none of the data was written yet.
  // Note that |write_buffer_| actually holds data written by the QuicSession
  // (not data buffered by the stream).
  EXPECT_EQ(0ul, write_buffer_.size());

  char message1[] = "xyzzy";
  test::QuicTestMemSliceVector data1({std::make_pair(message1, 5)});

  // More writes go into the buffer.
  stream_->WriteMemSlices(data1.span(), /*fin=*/false);

  EXPECT_TRUE(stream_->HasBufferedData());
  EXPECT_EQ(12u, stream_->BufferedDataBytes());
  EXPECT_EQ(12u, mock_stream_delegate_->last_bytes_buffered());
  EXPECT_EQ(0ul, write_buffer_.size());

  // The stream becomes writable, so it sends the buffered data.
  session_->set_writable(true);
  stream_->OnCanWrite();

  EXPECT_FALSE(stream_->HasBufferedData());
  EXPECT_EQ(0u, stream_->BufferedDataBytes());
  EXPECT_EQ(0u, mock_stream_delegate_->last_bytes_buffered());
  EXPECT_EQ("Foo barxyzzy", write_buffer_);
}

// Finish writing to a stream.
// It delivers the fin bit and closes the write-side as soon as possible.
TEST_F(QuartcStreamTest, FinishWriting) {
  CreateReliableQuicStream();

  session_->set_writable(false);
  stream_->FinishWriting();
  EXPECT_FALSE(stream_->fin_sent());

  // Fin is sent as soon as the stream becomes writable.
  session_->set_writable(true);
  stream_->OnCanWrite();
  EXPECT_TRUE(stream_->fin_sent());
  EXPECT_TRUE(stream_->write_side_closed());
}

// Read an entire string.
TEST_F(QuartcStreamTest, ReadDataWhole) {
  CreateReliableQuicStream();
  QuicStreamFrame frame(kStreamId, false, 0, "Hello, World!");
  stream_->OnStreamFrame(frame);

  EXPECT_EQ("Hello, World!", read_buffer_);
}

// Read part of a string.
TEST_F(QuartcStreamTest, ReadDataPartial) {
  CreateReliableQuicStream();
  QuicStreamFrame frame(kStreamId, false, 0, "Hello, World!");
  frame.data_length = 5;
  stream_->OnStreamFrame(frame);

  EXPECT_EQ("Hello", read_buffer_);
}

// Streams do not call OnReceived() after StopReading().
// Note: this is tested here because Quartc relies on this behavior.
TEST_F(QuartcStreamTest, StopReading) {
  CreateReliableQuicStream();
  stream_->StopReading();

  QuicStreamFrame frame(kStreamId, false, 0, "Hello, World!");
  stream_->OnStreamFrame(frame);

  EXPECT_EQ(0ul, read_buffer_.size());

  QuicStreamFrame frame2(kStreamId, true, 0, "Hello, World!");
  stream_->OnStreamFrame(frame2);

  EXPECT_EQ(0ul, read_buffer_.size());
  EXPECT_TRUE(stream_->fin_received());
}

// Test that closing the stream results in a callback.
TEST_F(QuartcStreamTest, CloseStream) {
  CreateReliableQuicStream();
  EXPECT_FALSE(mock_stream_delegate_->closed());
  stream_->OnClose();
  EXPECT_TRUE(mock_stream_delegate_->closed());
}

// Both sending and receiving fin automatically closes a stream.
TEST_F(QuartcStreamTest, CloseOnFins) {
  CreateReliableQuicStream();
  QuicStreamFrame frame(kStreamId, true, 0, 0);
  stream_->OnStreamFrame(frame);

  test::QuicTestMemSliceVector data({});
  stream_->WriteMemSlices(data.span(), /*fin=*/true);

  // Check that the OnClose() callback occurred.
  EXPECT_TRUE(mock_stream_delegate_->closed());
}

TEST_F(QuartcStreamTest, TestCancelOnLossDisabled) {
  CreateReliableQuicStream();

  // This should be the default state.
  EXPECT_FALSE(stream_->cancel_on_loss());

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo bar", write_buffer_);
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_NO_ERROR);
}

TEST_F(QuartcStreamTest, TestCancelOnLossEnabled) {
  CreateReliableQuicStream();
  stream_->set_cancel_on_loss(true);

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo bar", write_buffer_);
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_CANCELLED);
}

TEST_F(QuartcStreamTest, MaxRetransmissionsAbsent) {
  CreateReliableQuicStream();

  // This should be the default state.
  EXPECT_EQ(stream_->max_retransmission_count(),
            std::numeric_limits<int>::max());

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo bar", write_buffer_);
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_NO_ERROR);
}

TEST_F(QuartcStreamTest, MaxRetransmissionsSet) {
  CreateReliableQuicStream();
  stream_->set_max_retransmission_count(2);

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo barFoo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo barFoo bar", write_buffer_);
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_CANCELLED);
}

TEST_F(QuartcStreamTest, MaxRetransmissionsDisjointFrames) {
  CreateReliableQuicStream();
  stream_->set_max_retransmission_count(2);

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  // Retransmit bytes [0, 3].
  stream_->OnStreamFrameLost(0, 4, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo ", write_buffer_);

  // Retransmit bytes [4, 6].  Everything has been retransmitted once.
  stream_->OnStreamFrameLost(4, 3, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo bar", write_buffer_);

  // Retransmit bytes [0, 6].  Everything can be retransmitted a second time.
  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo barFoo bar", write_buffer_);
}

TEST_F(QuartcStreamTest, MaxRetransmissionsOverlappingFrames) {
  CreateReliableQuicStream();
  stream_->set_max_retransmission_count(2);

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  // Retransmit bytes 0 to 3.
  stream_->OnStreamFrameLost(0, 4, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo ", write_buffer_);

  // Retransmit bytes 3 to 6.  Byte 3 has been retransmitted twice.
  stream_->OnStreamFrameLost(3, 4, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo  bar", write_buffer_);

  // Retransmit byte 3 a third time.  This should cause cancellation.
  stream_->OnStreamFrameLost(3, 1, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo  bar", write_buffer_);
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_CANCELLED);
}

TEST_F(QuartcStreamTest, MaxRetransmissionsWithAckedFrame) {
  CreateReliableQuicStream();
  stream_->set_max_retransmission_count(1);

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  // Retransmit bytes [0, 7).
  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo bar", write_buffer_);

  // Ack bytes [0, 7).  These bytes should be pruned from the data tracked by
  // the stream.
  QuicByteCount newly_acked_length = 0;
  stream_->OnStreamFrameAcked(0, 7, false, QuicTime::Delta::FromMilliseconds(1),
                              &newly_acked_length);
  EXPECT_EQ(7u, newly_acked_length);
  stream_->OnCanWrite();

  EXPECT_EQ("Foo barFoo bar", write_buffer_);

  // Retransmit bytes [0, 7) again.
  // QUIC will never mark frames as lost after they've been acked, but this lets
  // us test that QuartcStream stopped tracking these bytes after the acked.
  stream_->OnStreamFrameLost(0, 7, false);
  stream_->OnCanWrite();

  // QuartcStream should be cancelled, but it stopped tracking the lost bytes
  // after they were acked, so it's not.
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_NO_ERROR);
}

TEST_F(QuartcStreamTest, TestBytesPendingRetransmission) {
  CreateReliableQuicStream();
  stream_->set_cancel_on_loss(false);

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 4, false);
  EXPECT_EQ(stream_->BytesPendingRetransmission(), 4u);
  EXPECT_EQ(mock_stream_delegate_->last_bytes_pending_retransmission(), 4u);

  stream_->OnStreamFrameLost(4, 3, false);
  EXPECT_EQ(stream_->BytesPendingRetransmission(), 7u);
  EXPECT_EQ(mock_stream_delegate_->last_bytes_pending_retransmission(), 7u);

  stream_->OnCanWrite();
  EXPECT_EQ(stream_->BytesPendingRetransmission(), 0u);
  EXPECT_EQ(mock_stream_delegate_->last_bytes_pending_retransmission(), 0u);

  EXPECT_EQ("Foo barFoo bar", write_buffer_);
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_NO_ERROR);
}

TEST_F(QuartcStreamTest, TestBytesPendingRetransmissionWithCancelOnLoss) {
  CreateReliableQuicStream();
  stream_->set_cancel_on_loss(true);

  char message[] = "Foo bar";
  test::QuicTestMemSliceVector data({std::make_pair(message, 7)});
  stream_->WriteMemSlices(data.span(), /*fin=*/false);

  EXPECT_EQ("Foo bar", write_buffer_);

  stream_->OnStreamFrameLost(0, 4, false);
  EXPECT_EQ(stream_->BytesPendingRetransmission(), 0u);
  EXPECT_EQ(mock_stream_delegate_->last_bytes_pending_retransmission(), 0u);

  stream_->OnStreamFrameLost(4, 3, false);
  EXPECT_EQ(stream_->BytesPendingRetransmission(), 0u);
  EXPECT_EQ(mock_stream_delegate_->last_bytes_pending_retransmission(), 0u);

  stream_->OnCanWrite();
  EXPECT_EQ(stream_->BytesPendingRetransmission(), 0u);
  EXPECT_EQ(mock_stream_delegate_->last_bytes_pending_retransmission(), 0u);

  EXPECT_EQ("Foo bar", write_buffer_);
  EXPECT_EQ(stream_->stream_error(), QUIC_STREAM_CANCELLED);
}

}  // namespace

}  // namespace quic
