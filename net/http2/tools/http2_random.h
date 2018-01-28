// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_TOOLS_HTTP2_RANDOM_H_
#define NET_HTTP2_TOOLS_HTTP2_RANDOM_H_

#include <stdint.h>

#include <limits>

#include "net/http2/platform/api/http2_string.h"

namespace net {
namespace test {

class RandomBase {
 public:
  virtual ~RandomBase() {}
  virtual bool OneIn(int n) = 0;
  virtual int32_t Uniform(int32_t n) = 0;
  virtual uint8_t Rand8() = 0;
  virtual uint16_t Rand16() = 0;
  virtual uint32_t Rand32() = 0;
  virtual uint64_t Rand64() = 0;
  virtual int32_t Next() = 0;
  virtual int32_t Skewed(int max_log) = 0;
  virtual Http2String RandString(int length) = 0;

  // STL UniformRandomNumberGenerator implementation.
  typedef uint32_t result_type;
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() {
    return std::numeric_limits<uint32_t>::max();
  }
  result_type operator()() { return Rand32(); }
};

// Http2Random holds no state: instances use the same base::RandGenerator
// with a global state.
class Http2Random : public RandomBase {
 public:
  ~Http2Random() override {}
  bool OneIn(int n) override;
  int32_t Uniform(int32_t n) override;
  uint8_t Rand8() override;
  uint16_t Rand16() override;
  uint32_t Rand32() override;
  uint64_t Rand64() override;
  int32_t Next() override;
  int32_t Skewed(int max_log) override;
  Http2String RandString(int length) override;
};

}  // namespace test
}  // namespace net

#endif  // NET_HTTP2_TOOLS_HTTP2_RANDOM_H_
