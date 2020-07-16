// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "net/third_party/quiche/src/quic/core/quic_packet_number.h"

namespace quic {

void QuicPacketNumber::Clear() {
  packet_number_ = UninitializedPacketNumber();
}

void QuicPacketNumber::UpdateMax(QuicPacketNumber new_value) {
  if (!new_value.IsInitialized()) {
    return;
  }
  if (!IsInitialized()) {
    packet_number_ = new_value.ToUint64();
  } else {
    packet_number_ = std::max(packet_number_, new_value.ToUint64());
  }
}

uint64_t QuicPacketNumber::Hash() const {
  DCHECK(IsInitialized());
  return packet_number_;
}

uint64_t QuicPacketNumber::ToUint64() const {
  DCHECK(IsInitialized());
  return packet_number_;
}

bool QuicPacketNumber::IsInitialized() const {
  return packet_number_ != UninitializedPacketNumber();
}

QuicPacketNumber& QuicPacketNumber::operator++() {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max() - 1);
#endif
  packet_number_++;
  return *this;
}

QuicPacketNumber QuicPacketNumber::operator++(int) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max() - 1);
#endif
  QuicPacketNumber previous(*this);
  packet_number_++;
  return previous;
}

QuicPacketNumber& QuicPacketNumber::operator--() {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  DCHECK_GE(ToUint64(), 1UL);
#endif
  packet_number_--;
  return *this;
}

QuicPacketNumber QuicPacketNumber::operator--(int) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  DCHECK_GE(ToUint64(), 1UL);
#endif
  QuicPacketNumber previous(*this);
  packet_number_--;
  return previous;
}

QuicPacketNumber& QuicPacketNumber::operator+=(uint64_t delta) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  DCHECK_GT(std::numeric_limits<uint64_t>::max() - ToUint64(), delta);
#endif
  packet_number_ += delta;
  return *this;
}

QuicPacketNumber& QuicPacketNumber::operator-=(uint64_t delta) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  DCHECK_GE(ToUint64(), delta);
#endif
  packet_number_ -= delta;
  return *this;
}

std::ostream& operator<<(std::ostream& os, const QuicPacketNumber& p) {
  if (p.IsInitialized()) {
    os << p.packet_number_;
  } else {
    os << "uninitialized";
  }
  return os;
}

}  // namespace quic
