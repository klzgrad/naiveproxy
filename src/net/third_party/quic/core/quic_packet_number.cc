// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_packet_number.h"

namespace quic {

QuicPacketNumber::QuicPacketNumber()
    : packet_number_(UninitializedPacketNumber()) {}

QuicPacketNumber::QuicPacketNumber(uint64_t packet_number)
    : packet_number_(packet_number) {
  DCHECK_NE(UninitializedPacketNumber(), packet_number)
      << "Use default constructor for uninitialized packet number";
}

void QuicPacketNumber::Clear() {
  packet_number_ = UninitializedPacketNumber();
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
  if (GetQuicRestartFlag(quic_uint64max_uninitialized_pn)) {
    DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max() - 1);
  } else {
    DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max());
  }
#endif
  packet_number_++;
  return *this;
}

QuicPacketNumber QuicPacketNumber::operator++(int) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  if (GetQuicRestartFlag(quic_uint64max_uninitialized_pn)) {
    DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max() - 1);
  } else {
    DCHECK_LT(ToUint64(), std::numeric_limits<uint64_t>::max());
  }
#endif
  QuicPacketNumber previous(*this);
  packet_number_++;
  return previous;
}

QuicPacketNumber& QuicPacketNumber::operator--() {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  if (GetQuicRestartFlag(quic_uint64max_uninitialized_pn)) {
    DCHECK_GE(ToUint64(), 1UL);
  } else {
    DCHECK_GT(ToUint64(), 1UL);
  }
#endif
  packet_number_--;
  return *this;
}

QuicPacketNumber QuicPacketNumber::operator--(int) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  if (GetQuicRestartFlag(quic_uint64max_uninitialized_pn)) {
    DCHECK_GE(ToUint64(), 1UL);
  } else {
    DCHECK_GT(ToUint64(), 1UL);
  }
#endif
  QuicPacketNumber previous(*this);
  packet_number_--;
  return previous;
}

QuicPacketNumber& QuicPacketNumber::operator+=(uint64_t delta) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  if (GetQuicRestartFlag(quic_uint64max_uninitialized_pn)) {
    DCHECK_GT(std::numeric_limits<uint64_t>::max() - ToUint64(), delta);
  } else {
    DCHECK_GE(std::numeric_limits<uint64_t>::max() - ToUint64(), delta);
  }
#endif
  packet_number_ += delta;
  return *this;
}

QuicPacketNumber& QuicPacketNumber::operator-=(uint64_t delta) {
#ifndef NDEBUG
  DCHECK(IsInitialized());
  if (GetQuicRestartFlag(quic_uint64max_uninitialized_pn)) {
    DCHECK_GE(ToUint64(), delta);
  } else {
    DCHECK_GT(ToUint64(), delta);
  }
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

// static
uint64_t QuicPacketNumber::UninitializedPacketNumber() {
  return GetQuicRestartFlag(quic_uint64max_uninitialized_pn)
             ? std::numeric_limits<uint64_t>::max()
             : 0;
}

}  // namespace quic
