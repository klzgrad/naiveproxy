// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PAYLOAD_DECODER_BASE_TEST_UTIL_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PAYLOAD_DECODER_BASE_TEST_UTIL_H_

// Base class for testing concrete payload decoder classes.

#include <stddef.h>

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_constants_test_util.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts.h"
#include "net/third_party/quiche/src/http2/tools/http2_frame_builder.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {
namespace test {

// Base class for tests of payload decoders. Below this there is a templated
// sub-class that adds a bunch of type specific features.
class PayloadDecoderBaseTest : public RandomDecoderTest {
 protected:
  PayloadDecoderBaseTest();

  // Virtual functions to be implemented by the test classes for the individual
  // payload decoders...

  // Start decoding the payload.
  virtual DecodeStatus StartDecodingPayload(DecodeBuffer* db) = 0;

  // Resume decoding the payload.
  virtual DecodeStatus ResumeDecodingPayload(DecodeBuffer* db) = 0;

  // In support of ensuring that we're really accessing and updating the
  // decoder, prepare the decoder by, for example, overwriting the decoder.
  virtual void PreparePayloadDecoder() = 0;

  // Get the listener to be inserted into the FrameDecoderState, ready for
  // listening (e.g. reset if it is a FramePartsCollector).
  virtual Http2FrameDecoderListener* PrepareListener() = 0;

  // Record a frame header for use on each call to StartDecoding.
  void set_frame_header(const Http2FrameHeader& header) {
    EXPECT_EQ(0, InvalidFlagMaskForFrameType(header.type) & header.flags);
    if (!frame_header_is_set_ || frame_header_ != header) {
      HTTP2_VLOG(2) << "set_frame_header: " << frame_header_;
    }
    frame_header_ = header;
    frame_header_is_set_ = true;
  }

  FrameDecoderState* mutable_state() { return frame_decoder_state_.get(); }

  // Randomize the payload decoder, sets the payload decoder's frame_header_,
  // then start decoding the payload. Called by RandomDecoderTest. This method
  // is final so that we can always perform certain actions when
  // RandomDecoderTest starts the decoding of a payload, such as randomizing the
  // the payload decoder, injecting the frame header and counting fast decoding
  // cases. Sub-classes must implement StartDecodingPayload to perform their
  // initial decoding of a frame's payload.
  DecodeStatus StartDecoding(DecodeBuffer* db) final;

  // Called by RandomDecoderTest. This method is final so that we can always
  // perform certain actions when RandomDecoderTest calls it, such as counting
  // slow decode cases. Sub-classes must implement ResumeDecodingPayload to
  // continue decoding the frame's payload, which must not all be in one buffer.
  DecodeStatus ResumeDecoding(DecodeBuffer* db) final;

  // Given the specified payload (without the common frame header), decode
  // it with several partitionings of the payload.
  ::testing::AssertionResult DecodePayloadAndValidateSeveralWays(
      quiche::QuicheStringPiece payload,
      Validator validator);

  // TODO(jamessynge): Add helper method for verifying these are both non-zero,
  // and call the new method from tests that expect successful decoding.
  void ResetDecodeSpeedCounters() {
    fast_decode_count_ = 0;
    slow_decode_count_ = 0;
  }

  // Count of payloads that are full decoded by StartDecodingPayload, or that
  // an error was detected by StartDecodingPayload.
  size_t fast_decode_count_ = 0;

  // Count of payloads that require calling ResumeDecodingPayload in order to
  // decode them completely (or to detect an error during decoding).
  size_t slow_decode_count_ = 0;

 private:
  bool frame_header_is_set_ = false;
  Http2FrameHeader frame_header_;
  std::unique_ptr<FrameDecoderState> frame_decoder_state_;
};

// Base class for payload decoders of type Decoder, with corresponding test
// peer of type DecoderPeer, and using class Listener as the implementation
// of Http2FrameDecoderListenerInterface to be used during decoding.
// Typically Listener is a sub-class of FramePartsCollector.
// SupportedFrameType is set to false only for UnknownPayloadDecoder.
template <class Decoder,
          class DecoderPeer,
          class Listener,
          bool SupportedFrameType = true>
class AbstractPayloadDecoderTest : public PayloadDecoderBaseTest {
 protected:
  // An ApproveSize function returns true to approve decoding the specified
  // size of payload, else false to skip that size. Typically used for negative
  // tests; for example, decoding a SETTINGS frame at all sizes except for
  // multiples of 6.
  typedef std::function<bool(size_t size)> ApproveSize;

  AbstractPayloadDecoderTest() {}

  // These tests are in setup rather than the constructor for two reasons:
  // 1) Constructors are not allowed to fail, so gUnit documents that EXPECT_*
  //    and ASSERT_* are not allowed in constructors, and should instead be in
  //    SetUp if they are needed before the body of the test is executed.
  // 2) To allow the sub-class constructor to make any desired modifications to
  //    the DecoderPeer before these tests are executed; in particular,
  //    UnknownPayloadDecoderPeer has not got a fixed frame type, but it is
  //    instead set during the test's constructor.
  void SetUp() override {
    PayloadDecoderBaseTest::SetUp();

    // Confirm that DecoderPeer et al returns sensible values. Using auto as the
    // variable type so that no (narrowing) conversions take place that hide
    // problems; i.e. if someone changes KnownFlagsMaskForFrameType so that it
    // doesn't return a uint8, and has bits above the low-order 8 bits set, this
    // bit of paranoia should detect the problem before we get too far.
    auto frame_type = DecoderPeer::FrameType();
    if (SupportedFrameType) {
      EXPECT_TRUE(IsSupportedHttp2FrameType(frame_type)) << frame_type;
    } else {
      EXPECT_FALSE(IsSupportedHttp2FrameType(frame_type)) << frame_type;
    }

    auto known_flags = KnownFlagsMaskForFrameType(frame_type);
    EXPECT_EQ(known_flags, known_flags & 0xff);

    auto flags_to_avoid = DecoderPeer::FlagsAffectingPayloadDecoding();
    EXPECT_EQ(flags_to_avoid, flags_to_avoid & known_flags);
  }

  void PreparePayloadDecoder() override {
    payload_decoder_ = std::make_unique<Decoder>();
  }

  Http2FrameDecoderListener* PrepareListener() override {
    listener_.Reset();
    return &listener_;
  }

  // Returns random flags, but only those valid for the frame type, yet not
  // those that the DecoderPeer says will affect the decoding of the payload
  // (e.g. the PRIORTY flag on a HEADERS frame or PADDED on DATA frames).
  uint8_t RandFlags() {
    return Random().Rand8() &
           KnownFlagsMaskForFrameType(DecoderPeer::FrameType()) &
           ~DecoderPeer::FlagsAffectingPayloadDecoding();
  }

  // Start decoding the payload.
  DecodeStatus StartDecodingPayload(DecodeBuffer* db) override {
    HTTP2_DVLOG(2) << "StartDecodingPayload, db->Remaining=" << db->Remaining();
    return payload_decoder_->StartDecodingPayload(mutable_state(), db);
  }

  // Resume decoding the payload.
  DecodeStatus ResumeDecodingPayload(DecodeBuffer* db) override {
    HTTP2_DVLOG(2) << "ResumeDecodingPayload, db->Remaining="
                   << db->Remaining();
    return payload_decoder_->ResumeDecodingPayload(mutable_state(), db);
  }

  // Decode one frame's payload and confirm that the listener recorded the
  // expected FrameParts instance, and only FrameParts instance. The payload
  // will be decoded several times with different partitionings of the payload,
  // and after each the validator will be called.
  AssertionResult DecodePayloadAndValidateSeveralWays(
      quiche::QuicheStringPiece payload,
      const FrameParts& expected) {
    auto validator = [&expected, this]() -> AssertionResult {
      VERIFY_FALSE(listener_.IsInProgress());
      VERIFY_EQ(1u, listener_.size());
      VERIFY_AND_RETURN_SUCCESS(expected.VerifyEquals(*listener_.frame(0)));
    };
    return PayloadDecoderBaseTest::DecodePayloadAndValidateSeveralWays(
        payload, ValidateDoneAndEmpty(validator));
  }

  // Decode one frame's payload, expecting that the final status will be
  // kDecodeError, and that OnFrameSizeError will have been called on the
  // listener. The payload will be decoded several times with different
  // partitionings of the payload. The type WrappedValidator is either
  // RandomDecoderTest::Validator, RandomDecoderTest::NoArgValidator or
  // std::nullptr_t (not extra validation).
  template <typename WrappedValidator>
  ::testing::AssertionResult VerifyDetectsFrameSizeError(
      quiche::QuicheStringPiece payload,
      const Http2FrameHeader& header,
      WrappedValidator wrapped_validator) {
    set_frame_header(header);
    // If wrapped_validator is not a RandomDecoderTest::Validator, make it so.
    Validator validator = ToValidator(wrapped_validator);
    // And wrap that validator in another which will check that we've reached
    // the expected state of kDecodeError with OnFrameSizeError having been
    // called by the payload decoder.
    validator = [header, validator, this](
                    const DecodeBuffer& input,
                    DecodeStatus status) -> ::testing::AssertionResult {
      HTTP2_DVLOG(2) << "VerifyDetectsFrameSizeError validator; status="
                     << status << "; input.Remaining=" << input.Remaining();
      VERIFY_EQ(DecodeStatus::kDecodeError, status);
      VERIFY_FALSE(listener_.IsInProgress());
      VERIFY_EQ(1u, listener_.size());
      const FrameParts* frame = listener_.frame(0);
      VERIFY_EQ(header, frame->GetFrameHeader());
      VERIFY_TRUE(frame->GetHasFrameSizeError());
      // Verify did not get OnPaddingTooLong, as we should only ever produce
      // one of these two errors for a single frame.
      VERIFY_FALSE(frame->GetOptMissingLength());
      return validator(input, status);
    };
    VERIFY_AND_RETURN_SUCCESS(
        PayloadDecoderBaseTest::DecodePayloadAndValidateSeveralWays(payload,
                                                                    validator));
  }

  // Confirm that we get OnFrameSizeError when trying to decode unpadded_payload
  // at all sizes from zero to unpadded_payload.size(), except those sizes not
  // approved by approve_size.
  // If total_pad_length is greater than zero, then that amount of padding
  // is added to the payload (including the Pad Length field).
  // The flags will be required_flags, PADDED if total_pad_length > 0, and some
  // randomly selected flag bits not excluded by FlagsAffectingPayloadDecoding.
  ::testing::AssertionResult VerifyDetectsMultipleFrameSizeErrors(
      uint8_t required_flags,
      quiche::QuicheStringPiece unpadded_payload,
      ApproveSize approve_size,
      int total_pad_length) {
    // required_flags should come from those that are defined for the frame
    // type AND are those that affect the decoding of the payload (otherwise,
    // the flag shouldn't be required).
    Http2FrameType frame_type = DecoderPeer::FrameType();
    VERIFY_EQ(required_flags,
              required_flags & KnownFlagsMaskForFrameType(frame_type));
    VERIFY_EQ(required_flags,
              required_flags & DecoderPeer::FlagsAffectingPayloadDecoding());

    if (0 !=
        (Http2FrameFlag::PADDED & KnownFlagsMaskForFrameType(frame_type))) {
      // Frame type supports padding.
      if (total_pad_length == 0) {
        required_flags &= ~Http2FrameFlag::PADDED;
      } else {
        required_flags |= Http2FrameFlag::PADDED;
      }
    } else {
      VERIFY_EQ(0, total_pad_length);
    }

    bool validated = false;
    for (size_t real_payload_size = 0;
         real_payload_size <= unpadded_payload.size(); ++real_payload_size) {
      if (approve_size != nullptr && !approve_size(real_payload_size)) {
        continue;
      }
      HTTP2_VLOG(1) << "real_payload_size=" << real_payload_size;
      uint8_t flags = required_flags | RandFlags();
      Http2FrameBuilder fb;
      if (total_pad_length > 0) {
        // total_pad_length_ includes the size of the Pad Length field, and thus
        // ranges from 0 (no PADDED flag) to 256 (Pad Length == 255).
        fb.AppendUInt8(total_pad_length - 1);
      }
      // Append a subset of the unpadded_payload, which the decoder should
      // determine is not a valid amount.
      fb.Append(unpadded_payload.substr(0, real_payload_size));
      if (total_pad_length > 0) {
        fb.AppendZeroes(total_pad_length - 1);
      }
      // We choose a random stream id because the payload decoders aren't
      // checking stream ids.
      uint32_t stream_id = RandStreamId();
      Http2FrameHeader header(fb.size(), frame_type, flags, stream_id);
      VERIFY_SUCCESS(VerifyDetectsFrameSizeError(fb.buffer(), header, nullptr));
      validated = true;
    }
    VERIFY_TRUE(validated);
    return ::testing::AssertionSuccess();
  }

  // As above, but for frames without padding.
  ::testing::AssertionResult VerifyDetectsFrameSizeError(
      uint8_t required_flags,
      quiche::QuicheStringPiece unpadded_payload,
      const ApproveSize& approve_size) {
    Http2FrameType frame_type = DecoderPeer::FrameType();
    uint8_t known_flags = KnownFlagsMaskForFrameType(frame_type);
    VERIFY_EQ(0, known_flags & Http2FrameFlag::PADDED);
    VERIFY_EQ(0, required_flags & Http2FrameFlag::PADDED);
    VERIFY_AND_RETURN_SUCCESS(VerifyDetectsMultipleFrameSizeErrors(
        required_flags, unpadded_payload, approve_size, 0));
  }

  Listener listener_;
  std::unique_ptr<Decoder> payload_decoder_;
};

// A base class for tests parameterized by the total number of bytes of
// padding, including the Pad Length field (i.e. a total_pad_length of 0
// means unpadded as there is then no room for the Pad Length field).
// The frame type must support padding.
template <class Decoder, class DecoderPeer, class Listener>
class AbstractPaddablePayloadDecoderTest
    : public AbstractPayloadDecoderTest<Decoder, DecoderPeer, Listener>,
      public ::testing::WithParamInterface<int> {
  typedef AbstractPayloadDecoderTest<Decoder, DecoderPeer, Listener> Base;

 protected:
  using Base::listener_;
  using Base::Random;
  using Base::RandStreamId;
  using Base::set_frame_header;
  typedef typename Base::Validator Validator;

  AbstractPaddablePayloadDecoderTest() : total_pad_length_(GetParam()) {
    HTTP2_LOG(INFO) << "total_pad_length_ = " << total_pad_length_;
  }

  // Note that total_pad_length_ includes the size of the Pad Length field,
  // and thus ranges from 0 (no PADDED flag) to 256 (Pad Length == 255).
  bool IsPadded() const { return total_pad_length_ > 0; }

  // Value of the Pad Length field. Only call if IsPadded.
  size_t pad_length() const {
    EXPECT_TRUE(IsPadded());
    return total_pad_length_ - 1;
  }

  // Clear the frame builder and add the Pad Length field if appropriate.
  void Reset() {
    frame_builder_ = Http2FrameBuilder();
    if (IsPadded()) {
      frame_builder_.AppendUInt8(pad_length());
    }
  }

  void MaybeAppendTrailingPadding() {
    if (IsPadded()) {
      frame_builder_.AppendZeroes(pad_length());
    }
  }

  uint8_t RandFlags() {
    uint8_t flags = Base::RandFlags();
    if (IsPadded()) {
      flags |= Http2FrameFlag::PADDED;
    } else {
      flags &= ~Http2FrameFlag::PADDED;
    }
    return flags;
  }

  // Verify that we get OnPaddingTooLong when decoding payload, and that the
  // amount of missing padding is as specified. header.IsPadded must be true,
  // and the payload must be empty or the PadLength field must be too large.
  ::testing::AssertionResult VerifyDetectsPaddingTooLong(
      quiche::QuicheStringPiece payload,
      const Http2FrameHeader& header,
      size_t expected_missing_length) {
    set_frame_header(header);
    auto& listener = listener_;
    Validator validator =
        [header, expected_missing_length, &listener](
            const DecodeBuffer& input,
            DecodeStatus status) -> ::testing::AssertionResult {
      VERIFY_EQ(DecodeStatus::kDecodeError, status);
      VERIFY_FALSE(listener.IsInProgress());
      VERIFY_EQ(1u, listener.size());
      const FrameParts* frame = listener.frame(0);
      VERIFY_EQ(header, frame->GetFrameHeader());
      VERIFY_TRUE(frame->GetOptMissingLength());
      VERIFY_EQ(expected_missing_length, frame->GetOptMissingLength().value());
      // Verify did not get OnFrameSizeError.
      VERIFY_FALSE(frame->GetHasFrameSizeError());
      return ::testing::AssertionSuccess();
    };
    VERIFY_AND_RETURN_SUCCESS(
        PayloadDecoderBaseTest::DecodePayloadAndValidateSeveralWays(payload,
                                                                    validator));
  }

  // Verifies that we get OnPaddingTooLong for a padded frame payload whose
  // (randomly selected) payload length is less than total_pad_length_.
  // Flags will be selected at random, except PADDED will be set and
  // flags_to_avoid will not be set. The stream id is selected at random.
  ::testing::AssertionResult VerifyDetectsPaddingTooLong() {
    uint8_t flags = RandFlags() | Http2FrameFlag::PADDED;

    // Create an all padding payload for total_pad_length_.
    int payload_length = 0;
    Http2FrameBuilder fb;
    if (IsPadded()) {
      fb.AppendUInt8(pad_length());
      fb.AppendZeroes(pad_length());
      HTTP2_VLOG(1) << "fb.size=" << fb.size();
      // Pick a random length for the payload that is shorter than neccesary.
      payload_length = Random().Uniform(fb.size());
    }

    HTTP2_VLOG(1) << "payload_length=" << payload_length;
    std::string payload = fb.buffer().substr(0, payload_length);

    // The missing length is the amount we cut off the end, unless
    // payload_length is zero, in which case the decoder knows only that 1
    // byte, the Pad Length field, is missing.
    size_t missing_length =
        payload_length == 0 ? 1 : fb.size() - payload_length;
    HTTP2_VLOG(1) << "missing_length=" << missing_length;

    const Http2FrameHeader header(payload_length, DecoderPeer::FrameType(),
                                  flags, RandStreamId());
    VERIFY_AND_RETURN_SUCCESS(
        VerifyDetectsPaddingTooLong(payload, header, missing_length));
  }

  // total_pad_length_ includes the size of the Pad Length field, and thus
  // ranges from 0 (no PADDED flag) to 256 (Pad Length == 255).
  const size_t total_pad_length_;
  Http2FrameBuilder frame_builder_;
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PAYLOAD_DECODER_BASE_TEST_UTIL_H_
