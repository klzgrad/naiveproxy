// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/mock_quic_data.h"

namespace net {
namespace test {

MockQuicData::MockQuicData() : sequence_number_(0) {}

MockQuicData::~MockQuicData() {}

void MockQuicData::AddConnect(IoMode mode, int rv) {
  connect_.reset(new MockConnect(mode, rv));
}

void MockQuicData::AddSynchronousRead(
    std::unique_ptr<QuicEncryptedPacket> packet) {
  reads_.push_back(MockRead(SYNCHRONOUS, packet->data(), packet->length(),
                            sequence_number_++));
  packets_.push_back(std::move(packet));
}

void MockQuicData::AddRead(std::unique_ptr<QuicEncryptedPacket> packet) {
  reads_.push_back(
      MockRead(ASYNC, packet->data(), packet->length(), sequence_number_++));
  packets_.push_back(std::move(packet));
}

void MockQuicData::AddRead(IoMode mode, int rv) {
  reads_.push_back(MockRead(mode, rv, sequence_number_++));
}

void MockQuicData::AddWrite(std::unique_ptr<QuicEncryptedPacket> packet) {
  writes_.push_back(MockWrite(SYNCHRONOUS, packet->data(), packet->length(),
                              sequence_number_++));
  packets_.push_back(std::move(packet));
}

void MockQuicData::AddAsyncWrite(std::unique_ptr<QuicEncryptedPacket> packet) {
  writes_.push_back(
      MockWrite(ASYNC, packet->data(), packet->length(), sequence_number_++));
  packets_.push_back(std::move(packet));
}

void MockQuicData::AddWrite(IoMode mode, int rv) {
  writes_.push_back(MockWrite(mode, rv, sequence_number_++));
}

void MockQuicData::AddSocketDataToFactory(MockClientSocketFactory* factory) {
  factory->AddSocketDataProvider(InitializeAndGetSequencedSocketData());
}

bool MockQuicData::AllReadDataConsumed() {
  return socket_data_->AllReadDataConsumed();
}

bool MockQuicData::AllWriteDataConsumed() {
  return socket_data_->AllWriteDataConsumed();
}

void MockQuicData::Resume() {
  socket_data_->Resume();
}

SequencedSocketData* MockQuicData::InitializeAndGetSequencedSocketData() {
  MockRead* reads = reads_.empty() ? nullptr : &reads_[0];
  MockWrite* writes = writes_.empty() ? nullptr : &writes_[0];
  socket_data_.reset(
      new SequencedSocketData(reads, reads_.size(), writes, writes_.size()));
  if (connect_ != nullptr)
    socket_data_->set_connect_data(*connect_);

  return socket_data_.get();
}

SequencedSocketData* MockQuicData::GetSequencedSocketData() {
  return socket_data_.get();
}

}  // namespace test
}  // namespace net
