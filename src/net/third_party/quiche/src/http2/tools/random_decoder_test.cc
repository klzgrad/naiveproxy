// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"

#include <stddef.h>

#include <algorithm>
#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

namespace http2 {
namespace test {

RandomDecoderTest::RandomDecoderTest() = default;

bool RandomDecoderTest::StopDecodeOnDone() {
  return stop_decode_on_done_;
}

DecodeStatus RandomDecoderTest::DecodeSegments(DecodeBuffer* original,
                                               const SelectSize& select_size) {
  DecodeStatus status = DecodeStatus::kDecodeInProgress;
  bool first = true;
  HTTP2_VLOG(2) << "DecodeSegments: input size=" << original->Remaining();
  while (first || original->HasData()) {
    size_t remaining = original->Remaining();
    size_t size =
        std::min(remaining, select_size(first, original->Offset(), remaining));
    DecodeBuffer db(original->cursor(), size);
    HTTP2_VLOG(2) << "Decoding " << size << " bytes of " << remaining
                  << " remaining";
    if (first) {
      first = false;
      status = StartDecoding(&db);
    } else {
      status = ResumeDecoding(&db);
    }
    // A decoder MUST consume some input (if any is available), else we could
    // get stuck in infinite loops.
    if (db.Offset() == 0 && db.HasData() &&
        status != DecodeStatus::kDecodeError) {
      ADD_FAILURE() << "Decoder didn't make any progress; db.FullSize="
                    << db.FullSize()
                    << "   original.Offset=" << original->Offset();
      return DecodeStatus::kDecodeError;
    }
    original->AdvanceCursor(db.Offset());
    switch (status) {
      case DecodeStatus::kDecodeDone:
        if (original->Empty() || StopDecodeOnDone()) {
          return DecodeStatus::kDecodeDone;
        }
        continue;
      case DecodeStatus::kDecodeInProgress:
        continue;
      case DecodeStatus::kDecodeError:
        return DecodeStatus::kDecodeError;
    }
  }
  return status;
}

// Decode |original| multiple times, with different segmentations, validating
// after each decode, returning on the first failure.
AssertionResult RandomDecoderTest::DecodeAndValidateSeveralWays(
    DecodeBuffer* original,
    bool return_non_zero_on_first,
    const Validator& validator) {
  const uint32_t original_remaining = original->Remaining();
  HTTP2_VLOG(1) << "DecodeAndValidateSeveralWays - Start, remaining = "
                << original_remaining;
  uint32_t first_consumed;
  {
    // Fast decode (no stopping unless decoder does so).
    DecodeBuffer input(original->cursor(), original_remaining);
    HTTP2_VLOG(2) << "DecodeSegmentsAndValidate with SelectRemaining";
    VERIFY_SUCCESS(
        DecodeSegmentsAndValidate(&input, SelectRemaining(), validator))
        << "\nFailed with SelectRemaining; input.Offset=" << input.Offset()
        << "; input.Remaining=" << input.Remaining();
    first_consumed = input.Offset();
  }
  if (original_remaining <= 30) {
    // Decode again, one byte at a time.
    DecodeBuffer input(original->cursor(), original_remaining);
    HTTP2_VLOG(2) << "DecodeSegmentsAndValidate with SelectOne";
    VERIFY_SUCCESS(DecodeSegmentsAndValidate(&input, SelectOne(), validator))
        << "\nFailed with SelectOne; input.Offset=" << input.Offset()
        << "; input.Remaining=" << input.Remaining();
    VERIFY_EQ(first_consumed, input.Offset()) << "\nFailed with SelectOne";
  }
  if (original_remaining <= 20) {
    // Decode again, one or zero bytes at a time.
    DecodeBuffer input(original->cursor(), original_remaining);
    HTTP2_VLOG(2) << "DecodeSegmentsAndValidate with SelectZeroAndOne";
    VERIFY_SUCCESS(DecodeSegmentsAndValidate(
        &input, SelectZeroAndOne(return_non_zero_on_first), validator))
        << "\nFailed with SelectZeroAndOne";
    VERIFY_EQ(first_consumed, input.Offset())
        << "\nFailed with SelectZeroAndOne; input.Offset=" << input.Offset()
        << "; input.Remaining=" << input.Remaining();
  }
  {
    // Decode again, with randomly selected segment sizes.
    DecodeBuffer input(original->cursor(), original_remaining);
    HTTP2_VLOG(2) << "DecodeSegmentsAndValidate with SelectRandom";
    VERIFY_SUCCESS(DecodeSegmentsAndValidate(
        &input, SelectRandom(return_non_zero_on_first), validator))
        << "\nFailed with SelectRandom; input.Offset=" << input.Offset()
        << "; input.Remaining=" << input.Remaining();
    VERIFY_EQ(first_consumed, input.Offset()) << "\nFailed with SelectRandom";
  }
  VERIFY_EQ(original_remaining, original->Remaining());
  original->AdvanceCursor(first_consumed);
  HTTP2_VLOG(1) << "DecodeAndValidateSeveralWays - SUCCESS";
  return ::testing::AssertionSuccess();
}

// static
RandomDecoderTest::SelectSize RandomDecoderTest::SelectZeroAndOne(
    bool return_non_zero_on_first) {
  std::shared_ptr<bool> zero_next(new bool);
  *zero_next = !return_non_zero_on_first;
  return [zero_next](bool first, size_t offset, size_t remaining) -> size_t {
    if (*zero_next) {
      *zero_next = false;
      return 0;
    } else {
      *zero_next = true;
      return 1;
    }
  };
}

RandomDecoderTest::SelectSize RandomDecoderTest::SelectRandom(
    bool return_non_zero_on_first) {
  return [this, return_non_zero_on_first](bool first, size_t offset,
                                          size_t remaining) -> size_t {
    uint32_t r = random_.Rand32();
    if (first && return_non_zero_on_first) {
      CHECK_LT(0u, remaining);
      if (remaining == 1) {
        return 1;
      }
      return 1 + (r % remaining);  // size in range [1, remaining).
    }
    return r % (remaining + 1);  // size in range [0, remaining].
  };
}

uint32_t RandomDecoderTest::RandStreamId() {
  return random_.Rand32() & StreamIdMask();
}

}  // namespace test
}  // namespace http2
