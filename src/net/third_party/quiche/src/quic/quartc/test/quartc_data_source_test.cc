// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/quartc_data_source.h"

#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace test {
namespace {

class FakeDelegate : public QuartcDataSource::Delegate {
 public:
  void OnDataProduced(const char* data, size_t length) override;

  const std::vector<ParsedQuartcDataFrame>& frames() { return frames_; }

 private:
  std::vector<ParsedQuartcDataFrame> frames_;
};

void FakeDelegate::OnDataProduced(const char* data, size_t length) {
  ParsedQuartcDataFrame frame;
  QuicStringPiece message(data, length);
  if (ParsedQuartcDataFrame::Parse(message, &frame)) {
    frames_.push_back(frame);
  } else {
    QUIC_LOG(FATAL) << "Data source produced a frame it can't parse: "
                    << message;
  }
}

class QuartcDataSourceTest : public QuicTest {
 protected:
  QuartcDataSourceTest() : simulator_() {}

  simulator::Simulator simulator_;
  FakeDelegate delegate_;

  std::unique_ptr<QuartcDataSource> source_;
};

TEST_F(QuartcDataSourceTest, ProducesFrameEveryInterval) {
  QuartcDataSource::Config config;
  config.frame_interval = QuicTime::Delta::FromMilliseconds(20);
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(1000, config.frame_interval));
  source_->SetEnabled(true);

  simulator_.RunFor(config.frame_interval);
  EXPECT_EQ(delegate_.frames().size(), 1u);

  simulator_.RunFor(config.frame_interval);
  EXPECT_EQ(delegate_.frames().size(), 2u);

  simulator_.RunFor(config.frame_interval * 20);
  EXPECT_EQ(delegate_.frames().size(), 22u);
}

TEST_F(QuartcDataSourceTest, DoesNotProduceFramesUntilEnabled) {
  QuartcDataSource::Config config;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(1000, config.frame_interval));

  simulator_.RunFor(config.frame_interval * 20);
  EXPECT_EQ(delegate_.frames().size(), 0u);

  // The first frame is produced immediately (but asynchronously) upon enabling
  // the source.
  source_->SetEnabled(true);
  simulator_.RunFor(QuicTime::Delta::FromMicroseconds(1));
  EXPECT_EQ(delegate_.frames().size(), 1u);
}

TEST_F(QuartcDataSourceTest, DisableAndEnable) {
  QuartcDataSource::Config config;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(1000, config.frame_interval));

  source_->SetEnabled(true);
  simulator_.RunFor(config.frame_interval * 20);
  EXPECT_EQ(delegate_.frames().size(), 20u);

  // No new frames while the source is disabled.
  source_->SetEnabled(false);
  simulator_.RunFor(config.frame_interval * 20);
  EXPECT_EQ(delegate_.frames().size(), 20u);

  // The first frame is produced immediately (but asynchronously) upon enabling
  // the source.
  source_->SetEnabled(true);
  simulator_.RunFor(QuicTime::Delta::FromMicroseconds(1));
  ASSERT_EQ(delegate_.frames().size(), 21u);

  // The first frame after a pause should be no larger than previous frames.
  EXPECT_EQ(delegate_.frames()[0].payload.size(),
            delegate_.frames()[20].payload.size());

  // The first frame after the pause should have a much later timestamp.
  // Note that the previous frame (19) happens at the *start* of the 20th
  // interval.  Frame 20 would normally happen one interval later, but we've
  // delayed it by an extra 20 intervals (for a total of 21 intervals later).
  EXPECT_EQ(delegate_.frames()[20].send_time - delegate_.frames()[19].send_time,
            21 * config.frame_interval);
}

TEST_F(QuartcDataSourceTest, EnablingTwiceDoesNotChangeSchedule) {
  QuartcDataSource::Config config;
  config.frame_interval = QuicTime::Delta::FromMilliseconds(20);

  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(1000, config.frame_interval));

  // The first frame is produced immediately (but asynchronously) upon enabling
  // the source.
  source_->SetEnabled(true);
  simulator_.RunFor(QuicTime::Delta::FromMicroseconds(1));
  EXPECT_EQ(delegate_.frames().size(), 1u);

  // Enabling the source again does not re-schedule the alarm.
  source_->SetEnabled(true);
  simulator_.RunFor(QuicTime::Delta::FromMicroseconds(1));
  EXPECT_EQ(delegate_.frames().size(), 1u);

  // The second frame is sent at the expected interval after the first.
  ASSERT_TRUE(
      simulator_.RunUntil([this] { return delegate_.frames().size() == 2; }));

  EXPECT_EQ(delegate_.frames()[1].send_time - delegate_.frames()[0].send_time,
            config.frame_interval);
}

TEST_F(QuartcDataSourceTest, ProducesFramesWithConfiguredSourceId) {
  QuartcDataSource::Config config;
  config.id = 7;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(1000, config.frame_interval));
  source_->SetEnabled(true);
  simulator_.RunFor(config.frame_interval);

  ASSERT_EQ(delegate_.frames().size(), 1u);
  EXPECT_EQ(delegate_.frames()[0].source_id, config.id);
}

TEST_F(QuartcDataSourceTest, ProducesFramesAtAllocatedBandwidth) {
  QuartcDataSource::Config config;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);

  constexpr QuicByteCount bytes_per_frame = 1000;
  source_->AllocateBandwidth(QuicBandwidth::FromBytesAndTimeDelta(
      bytes_per_frame, config.frame_interval));
  source_->SetEnabled(true);
  simulator_.RunFor(config.frame_interval);

  ASSERT_EQ(delegate_.frames().size(), 1u);
  EXPECT_EQ(delegate_.frames()[0].payload.size(),
            bytes_per_frame - kDataFrameHeaderSize);
  EXPECT_EQ(delegate_.frames()[0].size, bytes_per_frame);
}

TEST_F(QuartcDataSourceTest, ProducesParseableHeaderWhenNotEnoughBandwidth) {
  QuartcDataSource::Config config;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);

  // Allocate less bandwidth than the source requires for its header.
  source_->AllocateBandwidth(QuicBandwidth::FromBytesAndTimeDelta(
      kDataFrameHeaderSize - 10, config.frame_interval));
  source_->SetEnabled(true);

  QuicTime start_time = simulator_.GetClock()->Now();
  simulator_.RunFor(config.frame_interval);

  ASSERT_EQ(delegate_.frames().size(), 1u);
  EXPECT_EQ(delegate_.frames()[0].payload.size(), 0u);
  EXPECT_EQ(delegate_.frames()[0].size, kDataFrameHeaderSize);

  // Header fields are still present and parseable.
  EXPECT_EQ(delegate_.frames()[0].source_id, 0);
  EXPECT_EQ(delegate_.frames()[0].sequence_number, 0);
  EXPECT_EQ(delegate_.frames()[0].send_time, start_time);
}

TEST_F(QuartcDataSourceTest, ProducesSequenceNumbers) {
  QuartcDataSource::Config config;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(1000, config.frame_interval));
  source_->SetEnabled(true);

  simulator_.RunFor(config.frame_interval * 20);

  ASSERT_EQ(delegate_.frames().size(), 20u);
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(delegate_.frames()[i].sequence_number, i);
  }
}

TEST_F(QuartcDataSourceTest, ProducesSendTimes) {
  QuartcDataSource::Config config;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(1000, config.frame_interval));
  source_->SetEnabled(true);

  simulator_.RunFor(config.frame_interval * 20);

  ASSERT_EQ(delegate_.frames().size(), 20u);
  QuicTime first_send_time = delegate_.frames()[0].send_time;
  for (int i = 1; i < 20; ++i) {
    EXPECT_EQ(delegate_.frames()[i].send_time,
              first_send_time + i * config.frame_interval);
  }
}

TEST_F(QuartcDataSourceTest, AllocateClampsToMin) {
  QuartcDataSource::Config config;
  config.min_bandwidth = QuicBandwidth::FromBitsPerSecond(8000);
  config.frame_interval = QuicTime::Delta::FromMilliseconds(100);
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);

  // When allocating less than the minimum, there is nothing left over.
  EXPECT_EQ(source_->AllocateBandwidth(QuicBandwidth::FromBitsPerSecond(6000)),
            QuicBandwidth::Zero());

  source_->SetEnabled(true);
  simulator_.RunFor(config.frame_interval);

  // The frames produced use min_bandwidth instead of the lower allocation.
  QuicByteCount bytes_per_frame =
      config.min_bandwidth.ToBytesPerPeriod(config.frame_interval);
  ASSERT_EQ(delegate_.frames().size(), 1u);
  EXPECT_EQ(delegate_.frames()[0].payload.size(),
            bytes_per_frame - kDataFrameHeaderSize);
  EXPECT_EQ(delegate_.frames()[0].size, bytes_per_frame);
}

TEST_F(QuartcDataSourceTest, AllocateClampsToMax) {
  QuartcDataSource::Config config;
  config.max_bandwidth = QuicBandwidth::FromBitsPerSecond(8000);
  config.frame_interval = QuicTime::Delta::FromMilliseconds(100);
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);

  // When allocating more than the maximum, the excess is returned.
  EXPECT_EQ(source_->AllocateBandwidth(QuicBandwidth::FromBitsPerSecond(10000)),
            QuicBandwidth::FromBitsPerSecond(2000));

  source_->SetEnabled(true);
  simulator_.RunFor(config.frame_interval);

  // The frames produced use max_bandwidth instead of the higher allocation.
  QuicByteCount bytes_per_frame =
      config.max_bandwidth.ToBytesPerPeriod(config.frame_interval);
  ASSERT_EQ(delegate_.frames().size(), 1u);
  EXPECT_EQ(delegate_.frames()[0].payload.size(),
            bytes_per_frame - kDataFrameHeaderSize);
  EXPECT_EQ(delegate_.frames()[0].size, bytes_per_frame);
}

TEST_F(QuartcDataSourceTest, MaxFrameSize) {
  constexpr QuicByteCount bytes_per_frame = 1000;
  QuartcDataSource::Config config;
  config.max_frame_size = bytes_per_frame;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);

  // Allocate enough bandwidth for more than one frame per interval.
  source_->AllocateBandwidth(QuicBandwidth::FromBytesAndTimeDelta(
      3 * bytes_per_frame, config.frame_interval));
  source_->SetEnabled(true);

  QuicTime start_time = simulator_.GetClock()->Now();
  simulator_.RunFor(config.frame_interval);

  // Since there's enough bandwidth for three frames per interval, that's what
  // the source should generate.
  EXPECT_EQ(delegate_.frames().size(), 3u);
  int i = 0;
  for (const auto& frame : delegate_.frames()) {
    // Each of the frames should start with a header that can be parsed.
    // Each gets the same timestamp, but a different sequence number.
    EXPECT_EQ(frame.source_id, config.id);
    EXPECT_EQ(frame.sequence_number, i++);
    EXPECT_EQ(frame.send_time, start_time);

    // Each of the frames should have the configured maximum size.
    EXPECT_EQ(frame.payload.size(), bytes_per_frame - kDataFrameHeaderSize);
    EXPECT_EQ(frame.size, bytes_per_frame);
  }
}

TEST_F(QuartcDataSourceTest, ProducesParseableHeaderWhenMaxFrameSizeTooSmall) {
  QuartcDataSource::Config config;
  config.max_frame_size = kDataFrameHeaderSize - 1;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);

  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(200, config.frame_interval));
  source_->SetEnabled(true);

  QuicTime start_time = simulator_.GetClock()->Now();
  simulator_.RunFor(config.frame_interval);

  ASSERT_GE(delegate_.frames().size(), 1u);
  EXPECT_EQ(delegate_.frames()[0].payload.size(), 0u);
  EXPECT_EQ(delegate_.frames()[0].size, kDataFrameHeaderSize);

  // Header fields are still present and parseable.
  EXPECT_EQ(delegate_.frames()[0].source_id, 0);
  EXPECT_EQ(delegate_.frames()[0].sequence_number, 0);
  EXPECT_EQ(delegate_.frames()[0].send_time, start_time);
}

TEST_F(QuartcDataSourceTest, ProducesParseableHeaderWhenLeftoverSizeTooSmall) {
  QuartcDataSource::Config config;
  config.max_frame_size = 200;
  source_ = std::make_unique<QuartcDataSource>(
      simulator_.GetClock(), simulator_.GetAlarmFactory(),
      simulator_.GetRandomGenerator(), config, &delegate_);

  // Allocate enough bandwidth to send a 200-byte frame and a 1-byte frame.
  source_->AllocateBandwidth(
      QuicBandwidth::FromBytesAndTimeDelta(201, config.frame_interval));
  source_->SetEnabled(true);

  QuicTime start_time = simulator_.GetClock()->Now();
  simulator_.RunFor(config.frame_interval);

  ASSERT_EQ(delegate_.frames().size(), 2u);
  EXPECT_EQ(delegate_.frames()[0].payload.size(), 200u - kDataFrameHeaderSize);
  EXPECT_EQ(delegate_.frames()[0].size, 200u);

  // The second frame, using the 1 leftover byte from the first, rounds up to
  // the minimum frame size (just the header and no payload).
  EXPECT_EQ(delegate_.frames()[1].payload.size(), 0u);
  EXPECT_EQ(delegate_.frames()[1].size, kDataFrameHeaderSize);

  // Header fields are still present and parseable.
  EXPECT_EQ(delegate_.frames()[1].source_id, 0);
  EXPECT_EQ(delegate_.frames()[1].sequence_number, 1);
  EXPECT_EQ(delegate_.frames()[1].send_time, start_time);
}

}  // namespace
}  // namespace test
}  // namespace quic
