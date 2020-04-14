// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TOOLS_RANDOM_DECODER_TEST_H_
#define QUICHE_HTTP2_TOOLS_RANDOM_DECODER_TEST_H_

// RandomDecoderTest is a base class for tests of decoding various kinds
// of HTTP/2 and HPACK encodings.

// TODO(jamessynge): Move more methods into .cc file.

#include <stddef.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {
namespace test {

// Some helpers.

template <typename T, size_t N>
quiche::QuicheStringPiece ToStringPiece(T (&data)[N]) {
  return quiche::QuicheStringPiece(reinterpret_cast<const char*>(data),
                                   N * sizeof(T));
}

// Overwrite the enum with some random value, probably not a valid value for
// the enum type, but which fits into its storage.
template <typename T,
          typename E = typename std::enable_if<std::is_enum<T>::value>::type>
void CorruptEnum(T* out, Http2Random* rng) {
  // Per cppreference.com, if the destination type of a static_cast is
  // smaller than the source type (i.e. type of r and uint32 below), the
  // resulting value is the smallest unsigned value equal to the source value
  // modulo 2^n, where n is the number of bits used to represent the
  // destination type unsigned U.
  using underlying_type_T = typename std::underlying_type<T>::type;
  using unsigned_underlying_type_T =
      typename std::make_unsigned<underlying_type_T>::type;
  auto r = static_cast<unsigned_underlying_type_T>(rng->Rand32());
  *out = static_cast<T>(r);
}

// Base class for tests of the ability to decode a sequence of bytes with
// various boundaries between the DecodeBuffers provided to the decoder.
class RandomDecoderTest : public ::testing::Test {
 public:
  // SelectSize returns the size of the next DecodeBuffer to be passed to the
  // decoder. Note that RandomDecoderTest allows that size to be zero, though
  // some decoders can't deal with that on the first byte, hence the |first|
  // parameter.
  typedef std::function<size_t(bool first, size_t offset, size_t remaining)>
      SelectSize;

  // Validator returns an AssertionResult so test can do:
  // EXPECT_THAT(DecodeAndValidate(..., validator));
  typedef ::testing::AssertionResult AssertionResult;
  typedef std::function<AssertionResult(const DecodeBuffer& input,
                                        DecodeStatus status)>
      Validator;
  typedef std::function<AssertionResult()> NoArgValidator;

  RandomDecoderTest();

 protected:
  // TODO(jamessynge): Modify StartDecoding, etc. to (somehow) return
  // AssertionResult so that the VERIFY_* methods exported from
  // gunit_helpers.h can be widely used.

  // Start decoding; call allows sub-class to Reset the decoder, or deal with
  // the first byte if that is done in a unique fashion.  Might be called with
  // a zero byte buffer.
  virtual DecodeStatus StartDecoding(DecodeBuffer* db) = 0;

  // Resume decoding of the input after a prior call to StartDecoding, and
  // possibly many calls to ResumeDecoding.
  virtual DecodeStatus ResumeDecoding(DecodeBuffer* db) = 0;

  // Return true if a decode status of kDecodeDone indicates that
  // decoding should stop.
  virtual bool StopDecodeOnDone();

  // Decode buffer |original| until we run out of input, or kDecodeDone is
  // returned by the decoder AND StopDecodeOnDone() returns true. Segments
  // (i.e. cuts up) the original DecodeBuffer into (potentially) smaller buffers
  // by calling |select_size| to decide how large each buffer should be.
  // We do this to test the ability to deal with arbitrary boundaries, as might
  // happen in transport.
  // Returns the final DecodeStatus.
  DecodeStatus DecodeSegments(DecodeBuffer* original,
                              const SelectSize& select_size);

  // Decode buffer |original| until we run out of input, or kDecodeDone is
  // returned by the decoder AND StopDecodeOnDone() returns true. Segments
  // (i.e. cuts up) the original DecodeBuffer into (potentially) smaller buffers
  // by calling |select_size| to decide how large each buffer should be.
  // We do this to test the ability to deal with arbitrary boundaries, as might
  // happen in transport.
  // Invokes |validator| with the final decode status and the original decode
  // buffer, with the cursor advanced as far as has been consumed by the decoder
  // and returns validator's result.
  ::testing::AssertionResult DecodeSegmentsAndValidate(
      DecodeBuffer* original,
      const SelectSize& select_size,
      const Validator& validator) {
    DecodeStatus status = DecodeSegments(original, select_size);
    VERIFY_AND_RETURN_SUCCESS(validator(*original, status));
  }

  // Returns a SelectSize function for fast decoding, i.e. passing all that
  // is available to the decoder.
  static SelectSize SelectRemaining() {
    return [](bool first, size_t offset, size_t remaining) -> size_t {
      return remaining;
    };
  }

  // Returns a SelectSize function for decoding a single byte at a time.
  static SelectSize SelectOne() {
    return
        [](bool first, size_t offset, size_t remaining) -> size_t { return 1; };
  }

  // Returns a SelectSize function for decoding a single byte at a time, where
  // zero byte buffers are also allowed. Alternates between zero and one.
  static SelectSize SelectZeroAndOne(bool return_non_zero_on_first);

  // Returns a SelectSize function for decoding random sized segments.
  SelectSize SelectRandom(bool return_non_zero_on_first);

  // Decode |original| multiple times, with different segmentations of the
  // decode buffer, validating after each decode, and confirming that they
  // each decode the same amount. Returns on the first failure, else returns
  // success.
  AssertionResult DecodeAndValidateSeveralWays(DecodeBuffer* original,
                                               bool return_non_zero_on_first,
                                               const Validator& validator);

  static Validator ToValidator(std::nullptr_t) {
    return [](const DecodeBuffer& input, DecodeStatus status) {
      return ::testing::AssertionSuccess();
    };
  }

  static Validator ToValidator(const Validator& validator) {
    if (validator == nullptr) {
      return ToValidator(nullptr);
    }
    return validator;
  }

  static Validator ToValidator(const NoArgValidator& validator) {
    if (validator == nullptr) {
      return ToValidator(nullptr);
    }
    return [validator](const DecodeBuffer& input, DecodeStatus status) {
      return validator();
    };
  }

  // Wraps a validator with another validator
  // that first checks that the DecodeStatus is kDecodeDone and
  // that the DecodeBuffer is empty.
  // TODO(jamessynge): Replace this overload with the next, as using this method
  // usually means that the wrapped function doesn't need to be passed the
  // DecodeBuffer nor the DecodeStatus.
  static Validator ValidateDoneAndEmpty(const Validator& wrapped) {
    return [wrapped](const DecodeBuffer& input,
                     DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(status, DecodeStatus::kDecodeDone);
      VERIFY_EQ(0u, input.Remaining()) << "\nOffset=" << input.Offset();
      if (wrapped) {
        return wrapped(input, status);
      }
      return ::testing::AssertionSuccess();
    };
  }
  static Validator ValidateDoneAndEmpty(NoArgValidator wrapped) {
    return [wrapped](const DecodeBuffer& input,
                     DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(status, DecodeStatus::kDecodeDone);
      VERIFY_EQ(0u, input.Remaining()) << "\nOffset=" << input.Offset();
      if (wrapped) {
        return wrapped();
      }
      return ::testing::AssertionSuccess();
    };
  }
  static Validator ValidateDoneAndEmpty() {
    NoArgValidator validator;
    return ValidateDoneAndEmpty(validator);
  }

  // Wraps a validator with another validator
  // that first checks that the DecodeStatus is kDecodeDone and
  // that the DecodeBuffer has the expected offset.
  // TODO(jamessynge): Replace this overload with the next, as using this method
  // usually means that the wrapped function doesn't need to be passed the
  // DecodeBuffer nor the DecodeStatus.
  static Validator ValidateDoneAndOffset(uint32_t offset,
                                         const Validator& wrapped) {
    return [wrapped, offset](const DecodeBuffer& input,
                             DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(status, DecodeStatus::kDecodeDone);
      VERIFY_EQ(offset, input.Offset()) << "\nRemaining=" << input.Remaining();
      if (wrapped) {
        return wrapped(input, status);
      }
      return ::testing::AssertionSuccess();
    };
  }
  static Validator ValidateDoneAndOffset(uint32_t offset,
                                         NoArgValidator wrapped) {
    return [wrapped, offset](const DecodeBuffer& input,
                             DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(status, DecodeStatus::kDecodeDone);
      VERIFY_EQ(offset, input.Offset()) << "\nRemaining=" << input.Remaining();
      if (wrapped) {
        return wrapped();
      }
      return ::testing::AssertionSuccess();
    };
  }
  static Validator ValidateDoneAndOffset(uint32_t offset) {
    NoArgValidator validator;
    return ValidateDoneAndOffset(offset, validator);
  }

  // Expose |random_| as Http2Random so callers don't have to care about which
  // sub-class of Http2Random is used, nor can they rely on the specific
  // sub-class that RandomDecoderTest uses.
  Http2Random& Random() { return random_; }
  Http2Random* RandomPtr() { return &random_; }

  uint32_t RandStreamId();

  bool stop_decode_on_done_ = true;

 private:
  Http2Random random_;
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TOOLS_RANDOM_DECODER_TEST_H_
