// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/tools/random_util.h"

#include <cmath>

#include "base/rand_util.h"
#include "net/http2/tools/http2_random.h"

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

Http2String RandomString(RandomBase* rng, int len, Http2StringPiece alphabet) {
  Http2String random_string;
  random_string.reserve(len);
  for (int i = 0; i < len; ++i)
    random_string.push_back(alphabet[rng->Uniform(alphabet.size())]);
  return random_string;
}

size_t GenerateUniformInRange(size_t lo, size_t hi, RandomBase* rng) {
  if (lo + 1 >= hi) {
    return lo;
  }
  return lo + rng->Rand64() % (hi - lo);
}

// Here "word" means something that starts with a lower-case letter, and has
// zero or more additional characters that are numbers or lower-case letters.
Http2String GenerateHttp2HeaderName(size_t len, RandomBase* rng) {
  Http2StringPiece alpha_lc = "abcdefghijklmnopqrstuvwxyz";
  // If the name is short, just make it one word.
  if (len < 8) {
    return RandomString(rng, len, alpha_lc);
  }
  // If the name is longer, ensure it starts with a word, and after that may
  // have any character in alphanumdash_lc. 4 is arbitrary, could be as low
  // as 1.
  Http2StringPiece alphanumdash_lc = "abcdefghijklmnopqrstuvwxyz0123456789-";
  return RandomString(rng, 4, alpha_lc) +
         RandomString(rng, len - 4, alphanumdash_lc);
}

Http2String GenerateWebSafeString(size_t len, RandomBase* rng) {
  return RandomString(rng, len, kWebsafe64);
}

Http2String GenerateWebSafeString(size_t lo, size_t hi, RandomBase* rng) {
  return GenerateWebSafeString(GenerateUniformInRange(lo, hi, rng), rng);
}

size_t GenerateRandomSizeSkewedLow(size_t max, RandomBase* rng) {
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
