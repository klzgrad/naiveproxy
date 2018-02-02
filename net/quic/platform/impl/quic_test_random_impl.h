// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GFE_QUIC_PLATFORM_API_QUIC_TEST_RANDOM_IMPL_H_
#define GFE_QUIC_PLATFORM_API_QUIC_TEST_RANDOM_IMPL_H_

namespace net {
namespace test {

class QuicTestRandomBaseImpl {
 public:
  virtual ~QuicTestRandomBaseImpl() {}
  virtual bool OneIn(int n) = 0;
  virtual int32_t Uniform(int32_t n) = 0;
  virtual uint8_t Rand8() = 0;
  virtual uint16_t Rand16() = 0;
  virtual uint32_t Rand32() = 0;
  virtual uint64_t Rand64() = 0;
  virtual int32_t Next() = 0;
  virtual int32_t Skewed(int max_log) = 0;
  virtual QuicString RandString(int length) = 0;

  // STL UniformRandomNumberGenerator implementation.
  typedef uint32_t result_type;
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() {
    return std::numeric_limits<uint32_t>::max();
  }
  result_type operator()() { return Rand32(); }
};

// QuicTestRandom holds no state: instances use the same base::RandGenerator
// with a global state.
class QuicTestRandomImpl : public QuicTestRandomBaseImpl {
 public:
  ~QuicTestRandomImpl() override {}
  bool OneIn(int n) override;
  int32_t Uniform(int32_t n) override;
  uint8_t Rand8() override;
  uint16_t Rand16() override;
  uint32_t Rand32() override;
  uint64_t Rand64() override;
  int32_t Next() override;
  int32_t Skewed(int max_log) override;
  QuicString RandString(int length) override;
};

}  // namespace test
}  // namespace net

#endif  // GFE_QUIC_PLATFORM_API_QUIC_TEST_RANDOM_IMPL_H_
