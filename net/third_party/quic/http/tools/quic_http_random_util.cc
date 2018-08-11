// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/http/tools/quic_http_random_util.h"

#include <cmath>

#include "base/rand_util.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/api/quic_test_random.h"

namespace net {
namespace test {
namespace {

const char kWebsafe64[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";

// Generate two independent standard normal random variables using the polar
// method.
void GenerateRandomSizeSkewedLowHelper(size_t max, size_t* x, size_t* y) {
  double a, b, s;
  do {
    // Draw uniformly on [-1, 1).
    a = 2 * base::RandDouble() - 1.0;
    b = 2 * base::RandDouble() - 1.0;
    s = a * a + b * b;
  } while (s >= 1.0);
  double t = std::sqrt(-2.0 * std::log(s) / s);
  *x = static_cast<size_t>(a * t * max);
  *y = static_cast<size_t>(b * t * max);
}

}  // anonymous namespace

QuicString RandomString(QuicTestRandomBase* rng,
                        int len,
                        QuicStringPiece alphabet) {
  QuicString random_string;
  random_string.reserve(len);
  for (int i = 0; i < len; ++i)
    random_string.push_back(alphabet[rng->Uniform(alphabet.size())]);
  return random_string;
}

size_t GenerateUniformInRange(size_t lo, size_t hi, QuicTestRandomBase* rng) {
  if (lo + 1 >= hi) {
    return lo;
  }
  return lo + rng->Rand64() % (hi - lo);
}

QuicString GenerateWebSafeString(size_t len, QuicTestRandomBase* rng) {
  return RandomString(rng, len, kWebsafe64);
}

QuicString GenerateWebSafeString(size_t lo,
                                 size_t hi,
                                 QuicTestRandomBase* rng) {
  return GenerateWebSafeString(GenerateUniformInRange(lo, hi, rng), rng);
}

size_t GenerateRandomSizeSkewedLow(size_t max, QuicTestRandomBase* rng) {
  if (max == 0) {
    return 0;
  }
  // Generate a random number with a Gaussian distribution, centered on zero;
  // take the absolute, and then keep in range 0 to max.
  for (int i = 0; i < 5; i++) {
    size_t x, y;
    GenerateRandomSizeSkewedLowHelper(max, &x, &y);
    if (x <= max)
      return x;
    if (y <= max)
      return y;
  }
  return rng->Uniform(max + 1);
}

}  // namespace test
}  // namespace net
