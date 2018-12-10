// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_FUZZED_DATA_PROVIDER_H_
#define BASE_TEST_FUZZED_DATA_PROVIDER_H_

#include <stdint.h>

#include <string>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/strings/string_piece.h"

namespace base {

// Utility class to break up fuzzer input for multiple consumers. Whenever run
// on the same input, provides the same output, as long as its methods are
// called in the same order, with the same arguments.
class FuzzedDataProvider {
 public:
  // |data| is an array of length |size| that the FuzzedDataProvider wraps to
  // provide more granular access. |data| must outlive the FuzzedDataProvider.
  FuzzedDataProvider(const uint8_t* data, size_t size);
  ~FuzzedDataProvider();

  // Returns a std::string containing |num_bytes| of input data. If fewer than
  // |num_bytes| of data remain, returns a shorter std::string containing all
  // of the data that's left.
  std::string ConsumeBytes(size_t num_bytes);

  // Returns a std::string containing all remaining bytes of the input data.
  std::string ConsumeRemainingBytes();

  // Returns a std::string of length from 0 to |max_length|. When it runs out of
  // input data, returns what remains of the input. Designed to be more stable
  // with respect to a fuzzer inserting characters than just picking a random
  // length and then consuming that many bytes with ConsumeBytes().
  std::string ConsumeRandomLengthString(size_t max_length);

  // Returns a number in the range [min, max] by consuming bytes from the input
  // data. The value might not be uniformly distributed in the given range. If
  // there's no input data left, always returns |min|. |min| must be less than
  // or equal to |max|.
  uint32_t ConsumeUint32InRange(uint32_t min, uint32_t max);
  int ConsumeInt32InRange(int min, int max);

  // Returns a bool, or false when no data remains.
  bool ConsumeBool();

  // Returns a uint8_t from the input or 0 if nothing remains. This is
  // equivalent to ConsumeUint32InRange(0, 0xFF).
  uint8_t ConsumeUint8();

  // Returns a uint16_t from the input. If fewer than 2 bytes of data remain
  // will fill the most significant bytes with 0. This is equivalent to
  // ConsumeUint32InRange(0, 0xFFFF).
  uint16_t ConsumeUint16();

  // Returns a value from |array|, consuming as many bytes as needed to do so.
  // |array| must be a fixed-size array. Equivalent to
  // array[ConsumeUint32InRange(sizeof(array)-1)];
  template <typename Type, size_t size>
  Type PickValueInArray(Type (&array)[size]) {
    return array[ConsumeUint32InRange(0, size - 1)];
  }

  // Reports the remaining bytes available for fuzzed input.
  size_t remaining_bytes() { return remaining_data_.length(); }

 private:
  StringPiece remaining_data_;

  DISALLOW_COPY_AND_ASSIGN(FuzzedDataProvider);
};

}  // namespace base

#endif  // BASE_TEST_FUZZED_DATA_PROVIDER_H_
