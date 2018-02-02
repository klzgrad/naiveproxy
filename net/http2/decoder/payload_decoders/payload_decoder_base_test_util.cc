// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/decoder/payload_decoders/payload_decoder_base_test_util.h"

#include "net/http2/decoder/frame_decoder_state_test_util.h"
#include "net/http2/http2_structures_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {
PayloadDecoderBaseTest::PayloadDecoderBaseTest() {
  // If the test adds more data after the frame payload,
  // stop as soon as the payload is decoded.
  stop_decode_on_done_ = true;
  frame_header_is_set_ = false;
  Randomize(&frame_header_, RandomPtr());
}

DecodeStatus PayloadDecoderBaseTest::StartDecoding(DecodeBuffer* db) {
  DVLOG(2) << "StartDecoding, db->Remaining=" << db->Remaining();
  // Make sure sub-class has set frame_header_ so that we can inject it
  // into the payload decoder below.
  if (!frame_header_is_set_) {
    ADD_FAILURE() << "frame_header_ is not set";
    return DecodeStatus::kDecodeError;
  }
  // The contract with the payload decoders is that they won't receive a
  // decode buffer that extends beyond the end of the frame.
  if (db->Remaining() > frame_header_.payload_length) {
    ADD_FAILURE() << "DecodeBuffer has too much data: " << db->Remaining()
                  << " > " << frame_header_.payload_length;
    return DecodeStatus::kDecodeError;
  }

  // Prepare the payload decoder.
  PreparePayloadDecoder();

  // Reconstruct the FrameDecoderState, prepare the listener, and add it to
  // the FrameDecoderState.
  Http2DefaultReconstructObject(&frame_decoder_state_, RandomPtr());
  frame_decoder_state_.set_listener(PrepareListener());

  // Make sure that a listener was provided.
  if (frame_decoder_state_.listener() == nullptr) {
    ADD_FAILURE() << "PrepareListener must return a listener.";
    return DecodeStatus::kDecodeError;
  }

  // Now that nothing in the payload decoder should be valid, inject the
  // Http2FrameHeader whose payload we're about to decode. That header is the
  // only state that a payload decoder should expect is valid when its Start
  // method is called.
  FrameDecoderStatePeer::set_frame_header(frame_header_, &frame_decoder_state_);
  DecodeStatus status = StartDecodingPayload(db);
  if (status != DecodeStatus::kDecodeInProgress) {
    // Keep track of this so that a concrete test can verify that both fast
    // and slow decoding paths have been tested.
    ++fast_decode_count_;
  }
  return status;
}

DecodeStatus PayloadDecoderBaseTest::ResumeDecoding(DecodeBuffer* db) {
  DVLOG(2) << "ResumeDecoding, db->Remaining=" << db->Remaining();
  DecodeStatus status = ResumeDecodingPayload(db);
  if (status != DecodeStatus::kDecodeInProgress) {
    // Keep track of this so that a concrete test can verify that both fast
    // and slow decoding paths have been tested.
    ++slow_decode_count_;
  }
  return status;
}

::testing::AssertionResult
PayloadDecoderBaseTest::DecodePayloadAndValidateSeveralWays(
    Http2StringPiece payload,
    Validator validator) {
  VERIFY_TRUE(frame_header_is_set_);
  // Cap the payload to be decoded at the declared payload length. This is
  // required by the decoders' preconditions; they are designed on the
  // assumption that they're never passed more than they're permitted to
  // consume.
  // Note that it is OK if the payload is too short; the validator may be
  // designed to check for that.
  if (payload.size() > frame_header_.payload_length) {
    payload = Http2StringPiece(payload.data(), frame_header_.payload_length);
  }
  DecodeBuffer db(payload);
  ResetDecodeSpeedCounters();
  const bool kMayReturnZeroOnFirst = false;
  return DecodeAndValidateSeveralWays(&db, kMayReturnZeroOnFirst, validator);
}

}  // namespace test
}  // namespace net
