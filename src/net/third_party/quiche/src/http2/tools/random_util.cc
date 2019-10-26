// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/tools/random_util.h"

#include <cmath>

namespace http2 {
namespace test {

// Here "word" means something that starts with a lower-case letter, and has
// zero or more additional characters that are numbers or lower-case letters.
std::string GenerateHttp2HeaderName(size_t len, Http2Random* rng) {
  Http2StringPiece alpha_lc = "abcdefghijklmnopqrstuvwxyz";
  // If the name is short, just make it one word.
  if (len < 8) {
    return rng->RandStringWithAlphabet(len, alpha_lc);
  }
  // If the name is longer, ensure it starts with a word, and after that may
  // have any character in alphanumdash_lc. 4 is arbitrary, could be as low
  // as 1.
  Http2StringPiece alphanumdash_lc = "abcdefghijklmnopqrstuvwxyz0123456789-";
  return rng->RandStringWithAlphabet(4, alpha_lc) +
         rng->RandStringWithAlphabet(len - 4, alphanumdash_lc);
}

std::string GenerateWebSafeString(size_t len, Http2Random* rng) {
  static const char* kWebsafe64 =
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
  return rng->RandStringWithAlphabet(len, kWebsafe64);
}

std::string GenerateWebSafeString(size_t lo, size_t hi, Http2Random* rng) {
  return GenerateWebSafeString(rng->UniformInRange(lo, hi), rng);
}

}  // namespace test
}  // namespace http2
