// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/quartc_data_source.h"

#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"

namespace quic {
namespace test {

namespace {

class SendAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit SendAlarmDelegate(QuartcDataSource* source) : source_(source) {}

  void OnAlarm() override { source_->OnSendAlarm(); }

 private:
  QuartcDataSource* source_;
};

}  // namespace

bool ParsedQuartcDataFrame::Parse(QuicStringPiece data,
                                  ParsedQuartcDataFrame* out) {
  QuicDataReader reader(data.data(), data.size());

  uint32_t source_id;
  if (!reader.ReadUInt32(&source_id)) {
    return false;
  }

  uint64_t sequence_number;
  if (!reader.ReadUInt64(&sequence_number)) {
    return false;
  }

  uint64_t time_bits;
  if (!reader.ReadUInt64(&time_bits)) {
    return false;
  }
  QuicStringPiece payload = reader.ReadRemainingPayload();

  out->source_id = source_id;
  out->sequence_number = sequence_number;
  out->send_time =
      QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(time_bits);
  out->size = data.size();
  out->payload = std::string(payload.data(), payload.size());

  return true;
}

QuartcDataSource::QuartcDataSource(const QuicClock* clock,
                                   QuicAlarmFactory* alarm_factory,
                                   QuicRandom* random,
                                   const Config& config,
                                   Delegate* delegate)
    : clock_(clock),
      alarm_factory_(alarm_factory),
      random_(random),
      config_(config),
      delegate_(delegate),
      send_alarm_(alarm_factory_->CreateAlarm(new SendAlarmDelegate(this))),
      sequence_number_(0),
      allocated_bandwidth_(config_.min_bandwidth),
      last_send_time_(QuicTime::Zero()) {}

void QuartcDataSource::OnSendAlarm() {
  QuicTime now = clock_->Now();
  QuicTime::Delta time_since_last_send(QuicTime::Delta::Zero());
  if (last_send_time_.IsInitialized()) {
    // If previous frames have been sent, use the actual time since the last
    // send to compute the frame size.
    time_since_last_send = now - last_send_time_;
  } else {
    // For the first frame, use the configured frame interval.
    time_since_last_send = config_.frame_interval;
  }

  QuicByteCount bytes =
      allocated_bandwidth_.ToBytesPerPeriod(time_since_last_send);
  while (config_.max_frame_size > 0 && bytes > config_.max_frame_size) {
    GenerateFrame(config_.max_frame_size, now);
    bytes -= config_.max_frame_size;
  }
  GenerateFrame(bytes, now);

  // Reset alarm.
  last_send_time_ = now;
  send_alarm_->Set(now + config_.frame_interval);
}

QuicBandwidth QuartcDataSource::AllocateBandwidth(QuicBandwidth bandwidth) {
  allocated_bandwidth_ = std::max(config_.min_bandwidth,
                                  std::min(bandwidth, config_.max_bandwidth));
  return std::max(bandwidth - allocated_bandwidth_, QuicBandwidth::Zero());
}

bool QuartcDataSource::Enabled() const {
  return send_alarm_->IsSet();
}

void QuartcDataSource::SetEnabled(bool value) {
  if (Enabled() == value) {
    return;
  }

  if (!value) {
    send_alarm_->Cancel();

    // Reset the last send time.  When re-enabled, the data source should
    // produce a frame of approximately the right size for its current
    // bandwidth allocation and frame interval, not a huge frame accounting for
    // all the time since it was disabled.
    last_send_time_ = QuicTime::Zero();
    return;
  }

  send_alarm_->Set(clock_->Now());
}

void QuartcDataSource::GenerateFrame(QuicByteCount frame_size, QuicTime now) {
  frame_size = std::max(frame_size, kDataFrameHeaderSize);
  if (buffer_.size() < frame_size) {
    buffer_.resize(frame_size);
  }

  // Generate data.
  QuicDataWriter writer(frame_size, buffer_.data());
  writer.WriteUInt32(config_.id);
  writer.WriteUInt64(sequence_number_++);
  writer.WriteUInt64((now - QuicTime::Zero()).ToMicroseconds());
  writer.WriteRandomBytes(random_, writer.remaining());

  delegate_->OnDataProduced(writer.data(), writer.length());
}

}  // namespace test
}  // namespace quic
