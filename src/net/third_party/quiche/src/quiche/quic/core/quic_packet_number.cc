// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_packet_number.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <string>

#include "absl/strings/str_cat.h"

namespace quic {

void QuicPacketNumber::Clear() { packet_number_ = UninitializedPacketNumber(); }

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
  QUICHE_DCHECK(IsInitialized());
  return packet_number_;
}

uint64_t QuicPacketNumber::ToUint64() const {
  QUICHE_DCHECK(IsInitialized());
  return packet_number_;
}

bool QuicPacketNumber::IsInitialized() const {
  return packet_number_ != UninitializedPacketNumber();
}

QuicPacketNumber& QuicPacketNumber::operator++() {
#ifndef NDEBUG
  QUICHE_DCHECK(IsInitialized());
  QUICHE_DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max() - 1);
#endif
  packet_number_++;
  return *this;
}

QuicPacketNumber QuicPacketNumber::operator++(int) {
#ifndef NDEBUG
  QUICHE_DCHECK(IsInitialized());
  QUICHE_DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max() - 1);
#endif
  QuicPacketNumber previous(*this);
  packet_number_++;
  return previous;
}

QuicPacketNumber& QuicPacketNumber::operator--() {
#ifndef NDEBUG
  QUICHE_DCHECK(IsInitialized());
  QUICHE_DCHECK_GE(ToUint64(), 1UL);
#endif
  packet_number_--;
  return *this;
}

QuicPacketNumber QuicPacketNumber::operator--(int) {
#ifndef NDEBUG
  QUICHE_DCHECK(IsInitialized());
  QUICHE_DCHECK_GE(ToUint64(), 1UL);
#endif
  QuicPacketNumber previous(*this);
  packet_number_--;
  return previous;
}

QuicPacketNumber& QuicPacketNumber::operator+=(uint64_t delta) {
#ifndef NDEBUG
  QUICHE_DCHECK(IsInitialized());
  QUICHE_DCHECK_GT(std::numeric_limits<uint64_t>::max() - ToUint64(), delta);
#endif
  packet_number_ += delta;
  return *this;
}

QuicPacketNumber& QuicPacketNumber::operator-=(uint64_t delta) {
#ifndef NDEBUG
  QUICHE_DCHECK(IsInitialized());
  QUICHE_DCHECK_GE(ToUint64(), delta);
#endif
  packet_number_ -= delta;
  return *this;
}

std::string QuicPacketNumber::ToString() const {
  if (!IsInitialized()) {
    return "uninitialized";
  }
  return absl::StrCat(ToUint64());
}

std::ostream& operator<<(std::ostream& os, const QuicPacketNumber& p) {
  os << p.ToString();
  return os;
}

}  // namespace quic
