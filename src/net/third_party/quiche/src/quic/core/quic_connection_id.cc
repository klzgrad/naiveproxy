// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>

#include "third_party/boringssl/src/include/openssl/siphash.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

namespace {

// QuicConnectionIdHasher can be used to generate a stable connection ID hash
// function that will return the same value for two equal connection IDs for
// the duration of process lifetime. It is meant to be used as input to data
// structures that do not outlast process lifetime. A new key is generated once
// per process to prevent attackers from crafting connection IDs in such a way
// that they always land in the same hash bucket.
class QuicConnectionIdHasher {
 public:
  inline QuicConnectionIdHasher()
      : QuicConnectionIdHasher(QuicRandom::GetInstance()) {}

  explicit inline QuicConnectionIdHasher(QuicRandom* random) {
    random->RandBytes(&sip_hash_key_, sizeof(sip_hash_key_));
  }

  inline size_t Hash(const char* input, size_t input_len) const {
    return static_cast<size_t>(SIPHASH_24(
        sip_hash_key_, reinterpret_cast<const uint8_t*>(input), input_len));
  }

 private:
  uint64_t sip_hash_key_[2];
};

}  // namespace

QuicConnectionId::QuicConnectionId() : QuicConnectionId(nullptr, 0) {}

QuicConnectionId::QuicConnectionId(const char* data, uint8_t length) {
  static_assert(kQuicMaxConnectionIdAllVersionsLength <=
                    std::numeric_limits<uint8_t>::max(),
                "kQuicMaxConnectionIdAllVersionsLength too high");
  if (length > kQuicMaxConnectionIdAllVersionsLength) {
    QUIC_BUG << "Attempted to create connection ID of length "
             << static_cast<int>(length);
    length = kQuicMaxConnectionIdAllVersionsLength;
  }
  length_ = length;
  if (length_ == 0) {
    return;
  }
  if (length_ <= sizeof(data_short_)) {
    memcpy(data_short_, data, length_);
    return;
  }
  data_long_ = reinterpret_cast<char*>(malloc(length_));
  CHECK_NE(nullptr, data_long_);
  memcpy(data_long_, data, length_);
}

QuicConnectionId::~QuicConnectionId() {
  if (length_ > sizeof(data_short_)) {
    free(data_long_);
    data_long_ = nullptr;
  }
}

QuicConnectionId::QuicConnectionId(const QuicConnectionId& other)
    : QuicConnectionId(other.data(), other.length()) {}

QuicConnectionId& QuicConnectionId::operator=(const QuicConnectionId& other) {
  set_length(other.length());
  memcpy(mutable_data(), other.data(), length_);
  return *this;
}

const char* QuicConnectionId::data() const {
  if (length_ <= sizeof(data_short_)) {
    return data_short_;
  }
  return data_long_;
}

char* QuicConnectionId::mutable_data() {
  if (length_ <= sizeof(data_short_)) {
    return data_short_;
  }
  return data_long_;
}

uint8_t QuicConnectionId::length() const {
  return length_;
}

void QuicConnectionId::set_length(uint8_t length) {
  if (length > kQuicMaxConnectionIdAllVersionsLength) {
    QUIC_BUG << "Attempted to set connection ID length to "
             << static_cast<int>(length);
    length = kQuicMaxConnectionIdAllVersionsLength;
  }
  char temporary_data[sizeof(data_short_)];
  if (length > sizeof(data_short_)) {
    if (length_ <= sizeof(data_short_)) {
      // Copy data from data_short_ to data_long_.
      memcpy(temporary_data, data_short_, length_);
      data_long_ = reinterpret_cast<char*>(malloc(length));
      CHECK_NE(nullptr, data_long_);
      memcpy(data_long_, temporary_data, length_);
    } else {
      // Resize data_long_.
      char* realloc_result =
          reinterpret_cast<char*>(realloc(data_long_, length));
      CHECK_NE(nullptr, realloc_result);
      data_long_ = realloc_result;
    }
  } else if (length_ > sizeof(data_short_)) {
    // Copy data from data_long_ to data_short_.
    memcpy(temporary_data, data_long_, length);
    free(data_long_);
    data_long_ = nullptr;
    memcpy(data_short_, temporary_data, length);
  }
  length_ = length;
}

bool QuicConnectionId::IsEmpty() const {
  return length_ == 0;
}

size_t QuicConnectionId::Hash() const {
  static const QuicConnectionIdHasher hasher = QuicConnectionIdHasher();
  return hasher.Hash(data(), length_);
}

std::string QuicConnectionId::ToString() const {
  if (IsEmpty()) {
    return std::string("0");
  }
  return QuicTextUtils::HexEncode(data(), length_);
}

std::ostream& operator<<(std::ostream& os, const QuicConnectionId& v) {
  os << v.ToString();
  return os;
}

bool QuicConnectionId::operator==(const QuicConnectionId& v) const {
  return length_ == v.length_ && memcmp(data(), v.data(), length_) == 0;
}

bool QuicConnectionId::operator!=(const QuicConnectionId& v) const {
  return !(v == *this);
}

bool QuicConnectionId::operator<(const QuicConnectionId& v) const {
  if (length_ < v.length_) {
    return true;
  }
  if (length_ > v.length_) {
    return false;
  }
  return memcmp(data(), v.data(), length_) < 0;
}

QuicConnectionId EmptyQuicConnectionId() {
  return QuicConnectionId();
}

static_assert(kQuicDefaultConnectionIdLength == sizeof(uint64_t),
              "kQuicDefaultConnectionIdLength changed");
static_assert(kQuicDefaultConnectionIdLength == PACKET_8BYTE_CONNECTION_ID,
              "kQuicDefaultConnectionIdLength changed");

}  // namespace quic
