// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/test_tools/packet_dropping_test_writer.h"

#include <limits>

#include "base/rand_util.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"
#include "net/tools/quic/quic_epoll_connection_helper.h"

namespace net {
namespace test {

// An alarm that is scheduled if a blocked socket is simulated to indicate
// it's writable again.
class WriteUnblockedAlarm : public QuicAlarm::Delegate {
 public:
  explicit WriteUnblockedAlarm(PacketDroppingTestWriter* writer)
      : writer_(writer) {}

  void OnAlarm() override {
    QUIC_DLOG(INFO) << "Unblocking socket.";
    writer_->OnCanWrite();
  }

 private:
  PacketDroppingTestWriter* writer_;
};

// An alarm that is scheduled every time a new packet is to be written at a
// later point.
class DelayAlarm : public QuicAlarm::Delegate {
 public:
  explicit DelayAlarm(PacketDroppingTestWriter* writer) : writer_(writer) {}

  void OnAlarm() override {
    QuicTime new_deadline = writer_->ReleaseOldPackets();
    if (new_deadline.IsInitialized()) {
      writer_->SetDelayAlarm(new_deadline);
    }
  }

 private:
  PacketDroppingTestWriter* writer_;
};

PacketDroppingTestWriter::PacketDroppingTestWriter()
    : clock_(nullptr),
      cur_buffer_size_(0),
      num_calls_to_write_(0),
      config_mutex_(),
      fake_packet_loss_percentage_(0),
      fake_drop_first_n_packets_(0),
      fake_blocked_socket_percentage_(0),
      fake_packet_reorder_percentage_(0),
      fake_packet_delay_(QuicTime::Delta::Zero()),
      fake_bandwidth_(QuicBandwidth::Zero()),
      buffer_size_(0) {
  uint32_t seed = base::RandInt(0, std::numeric_limits<int32_t>::max());
  QUIC_LOG(INFO) << "Seeding packet loss with " << seed;
  simple_random_.set_seed(seed);
}

PacketDroppingTestWriter::~PacketDroppingTestWriter() {}

void PacketDroppingTestWriter::Initialize(QuicConnectionHelperInterface* helper,
                                          QuicAlarmFactory* alarm_factory,
                                          Delegate* on_can_write) {
  clock_ = helper->GetClock();
  write_unblocked_alarm_.reset(
      alarm_factory->CreateAlarm(new WriteUnblockedAlarm(this)));
  delay_alarm_.reset(alarm_factory->CreateAlarm(new DelayAlarm(this)));
  on_can_write_.reset(on_can_write);
}

WriteResult PacketDroppingTestWriter::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  ++num_calls_to_write_;
  ReleaseOldPackets();

  QuicReaderMutexLock lock(&config_mutex_);
  if (fake_drop_first_n_packets_ > 0 &&
      num_calls_to_write_ <=
          static_cast<uint64_t>(fake_drop_first_n_packets_)) {
    QUIC_DVLOG(1) << "Dropping first " << fake_drop_first_n_packets_
                  << " packets (packet number " << num_calls_to_write_ << ")";
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }
  if (fake_packet_loss_percentage_ > 0 &&
      simple_random_.RandUint64() % 100 <
          static_cast<uint64_t>(fake_packet_loss_percentage_)) {
    QUIC_DVLOG(1) << "Dropping packet.";
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }
  if (fake_blocked_socket_percentage_ > 0 &&
      simple_random_.RandUint64() % 100 <
          static_cast<uint64_t>(fake_blocked_socket_percentage_)) {
    CHECK(on_can_write_.get() != nullptr);
    QUIC_DVLOG(1) << "Blocking socket.";
    if (!write_unblocked_alarm_->IsSet()) {
      // Set the alarm to fire immediately.
      write_unblocked_alarm_->Set(clock_->ApproximateNow());
    }
    return WriteResult(WRITE_STATUS_BLOCKED, EAGAIN);
  }

  if (!fake_packet_delay_.IsZero() || !fake_bandwidth_.IsZero()) {
    if (buffer_size_ > 0 && buf_len + cur_buffer_size_ > buffer_size_) {
      // Drop packets which do not fit into the buffer.
      QUIC_DVLOG(1) << "Dropping packet because the buffer is full.";
      return WriteResult(WRITE_STATUS_OK, buf_len);
    }

    // Queue it to be sent.
    QuicTime send_time = clock_->ApproximateNow() + fake_packet_delay_;
    if (!fake_bandwidth_.IsZero()) {
      // Calculate a time the bandwidth limit would impose.
      QuicTime::Delta bandwidth_delay = QuicTime::Delta::FromMicroseconds(
          (buf_len * kNumMicrosPerSecond) / fake_bandwidth_.ToBytesPerSecond());
      send_time = delayed_packets_.empty()
                      ? send_time + bandwidth_delay
                      : delayed_packets_.back().send_time + bandwidth_delay;
    }
    std::unique_ptr<PerPacketOptions> delayed_options;
    if (options != nullptr) {
      delayed_options.reset(options->Clone());
    }
    delayed_packets_.push_back(
        DelayedWrite(buffer, buf_len, self_address, peer_address,
                     std::move(delayed_options), send_time));
    cur_buffer_size_ += buf_len;

    // Set the alarm if it's not yet set.
    if (!delay_alarm_->IsSet()) {
      delay_alarm_->Set(send_time);
    }

    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

  return QuicPacketWriterWrapper::WritePacket(buffer, buf_len, self_address,
                                              peer_address, options);
}

bool PacketDroppingTestWriter::IsWriteBlocked() const {
  if (write_unblocked_alarm_.get() != nullptr &&
      write_unblocked_alarm_->IsSet()) {
    return true;
  }
  return QuicPacketWriterWrapper::IsWriteBlocked();
}

void PacketDroppingTestWriter::SetWritable() {
  if (write_unblocked_alarm_.get() != nullptr &&
      write_unblocked_alarm_->IsSet()) {
    write_unblocked_alarm_->Cancel();
  }
  QuicPacketWriterWrapper::SetWritable();
}

QuicTime PacketDroppingTestWriter::ReleaseNextPacket() {
  if (delayed_packets_.empty()) {
    return QuicTime::Zero();
  }
  QuicReaderMutexLock lock(&config_mutex_);
  DelayedPacketList::iterator iter = delayed_packets_.begin();
  // Determine if we should re-order.
  if (delayed_packets_.size() > 1 && fake_packet_reorder_percentage_ > 0 &&
      simple_random_.RandUint64() % 100 <
          static_cast<uint64_t>(fake_packet_reorder_percentage_)) {
    QUIC_DLOG(INFO) << "Reordering packets.";
    ++iter;
    // Swap the send times when re-ordering packets.
    delayed_packets_.begin()->send_time = iter->send_time;
  }

  QUIC_DVLOG(1) << "Releasing packet.  " << (delayed_packets_.size() - 1)
                << " remaining.";
  // Grab the next one off the queue and send it.
  QuicPacketWriterWrapper::WritePacket(
      iter->buffer.data(), iter->buffer.length(), iter->self_address,
      iter->peer_address, iter->options.get());
  DCHECK_GE(cur_buffer_size_, iter->buffer.length());
  cur_buffer_size_ -= iter->buffer.length();
  delayed_packets_.erase(iter);

  // If there are others, find the time for the next to be sent.
  if (delayed_packets_.empty()) {
    return QuicTime::Zero();
  }
  return delayed_packets_.begin()->send_time;
}

QuicTime PacketDroppingTestWriter::ReleaseOldPackets() {
  while (!delayed_packets_.empty()) {
    QuicTime next_send_time = delayed_packets_.front().send_time;
    if (next_send_time > clock_->Now()) {
      return next_send_time;
    }
    ReleaseNextPacket();
  }
  return QuicTime::Zero();
}

void PacketDroppingTestWriter::SetDelayAlarm(QuicTime new_deadline) {
  delay_alarm_->Set(new_deadline);
}

void PacketDroppingTestWriter::OnCanWrite() {
  on_can_write_->OnCanWrite();
}

PacketDroppingTestWriter::DelayedWrite::DelayedWrite(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    std::unique_ptr<PerPacketOptions> options,
    QuicTime send_time)
    : buffer(buffer, buf_len),
      self_address(self_address),
      peer_address(peer_address),
      options(std::move(options)),
      send_time(send_time) {}

// TODO(rtenneti): on windows RValue reference gives errors.
PacketDroppingTestWriter::DelayedWrite::DelayedWrite(
    PacketDroppingTestWriter::DelayedWrite&& other) = default;

// TODO(rtenneti): on windows RValue reference gives errors.
// IPAddress has no move assignment operator.
//
// PacketDroppingTestWriter::DelayedWrite&
// PacketDroppingTestWriter::DelayedWrite::operator=(
//    PacketDroppingTestWriter::DelayedWrite&& other) = default;

PacketDroppingTestWriter::DelayedWrite::~DelayedWrite() {}

}  // namespace test
}  // namespace net
