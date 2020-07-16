// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder.h"

// Tests of Http2FrameDecoder.

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts.h"
#include "net/third_party/quiche/src/http2/test_tools/frame_parts_collector_listener.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

namespace http2 {
namespace test {
class Http2FrameDecoderPeer {
 public:
  static size_t remaining_total_payload(Http2FrameDecoder* decoder) {
    return decoder->frame_decoder_state_.remaining_total_payload();
  }
};

namespace {

class Http2FrameDecoderTest : public RandomDecoderTest {
 protected:
  void SetUp() override {
    // On any one run of this suite, we'll always choose the same value for
    // use_default_constructor_ because the random seed is the same for each
    // test case, but across runs the random seed changes.
    use_default_constructor_ = Random().OneIn(2);
  }

  DecodeStatus StartDecoding(DecodeBuffer* db) override {
    HTTP2_DVLOG(2) << "StartDecoding, db->Remaining=" << db->Remaining();
    collector_.Reset();
    PrepareDecoder();

    DecodeStatus status = decoder_->DecodeFrame(db);
    if (status != DecodeStatus::kDecodeInProgress) {
      // Keep track of this so that a concrete test can verify that both fast
      // and slow decoding paths have been tested.
      ++fast_decode_count_;
      if (status == DecodeStatus::kDecodeError) {
        ConfirmDiscardsRemainingPayload();
      }
    }
    return status;
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* db) override {
    HTTP2_DVLOG(2) << "ResumeDecoding, db->Remaining=" << db->Remaining();
    DecodeStatus status = decoder_->DecodeFrame(db);
    if (status != DecodeStatus::kDecodeInProgress) {
      // Keep track of this so that a concrete test can verify that both fast
      // and slow decoding paths have been tested.
      ++slow_decode_count_;
      if (status == DecodeStatus::kDecodeError) {
        ConfirmDiscardsRemainingPayload();
      }
    }
    return status;
  }

  // When an error is returned, the decoder is in state kDiscardPayload, and
  // stays there until the remaining bytes of the frame's payload have been
  // skipped over. There are no callbacks for this situation.
  void ConfirmDiscardsRemainingPayload() {
    ASSERT_TRUE(decoder_->IsDiscardingPayload());
    size_t remaining =
        Http2FrameDecoderPeer::remaining_total_payload(decoder_.get());
    // The decoder will discard the remaining bytes, but not go beyond that,
    // which these conditions verify.
    size_t extra = 10;
    std::string junk(remaining + extra, '0');
    DecodeBuffer tmp(junk);
    EXPECT_EQ(DecodeStatus::kDecodeDone, decoder_->DecodeFrame(&tmp));
    EXPECT_EQ(remaining, tmp.Offset());
    EXPECT_EQ(extra, tmp.Remaining());
    EXPECT_FALSE(decoder_->IsDiscardingPayload());
  }

  void PrepareDecoder() {
    // Alternate which constructor is used.
    if (use_default_constructor_) {
      decoder_ = std::make_unique<Http2FrameDecoder>();
      decoder_->set_listener(&collector_);
    } else {
      decoder_ = std::make_unique<Http2FrameDecoder>(&collector_);
    }
    decoder_->set_maximum_payload_size(maximum_payload_size_);

    use_default_constructor_ = !use_default_constructor_;
  }

  void ResetDecodeSpeedCounters() {
    fast_decode_count_ = 0;
    slow_decode_count_ = 0;
  }

  AssertionResult VerifyCollected(const FrameParts& expected) {
    VERIFY_FALSE(collector_.IsInProgress());
    VERIFY_EQ(1u, collector_.size());
    VERIFY_AND_RETURN_SUCCESS(expected.VerifyEquals(*collector_.frame(0)));
  }

  AssertionResult DecodePayloadAndValidateSeveralWays(
      quiche::QuicheStringPiece payload,
      Validator validator) {
    DecodeBuffer db(payload);
    bool start_decoding_requires_non_empty = false;
    return DecodeAndValidateSeveralWays(&db, start_decoding_requires_non_empty,
                                        validator);
  }

  // Decode one frame's payload and confirm that the listener recorded the
  // expected FrameParts instance, and only one FrameParts instance. The
  // payload will be decoded several times with different partitionings
  // of the payload, and after each the validator will be called.
  AssertionResult DecodePayloadAndValidateSeveralWays(
      quiche::QuicheStringPiece payload,
      const FrameParts& expected) {
    auto validator = [&expected, this](const DecodeBuffer& /*input*/,
                                       DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(status, DecodeStatus::kDecodeDone);
      VERIFY_AND_RETURN_SUCCESS(VerifyCollected(expected));
    };
    ResetDecodeSpeedCounters();
    VERIFY_SUCCESS(DecodePayloadAndValidateSeveralWays(
        payload, ValidateDoneAndEmpty(validator)));
    VERIFY_GT(fast_decode_count_, 0u);
    VERIFY_GT(slow_decode_count_, 0u);

    // Repeat with more input; it should stop without reading that input.
    std::string next_frame = Random().RandString(10);
    std::string input(payload.data(), payload.size());
    input += next_frame;

    ResetDecodeSpeedCounters();
    VERIFY_SUCCESS(DecodePayloadAndValidateSeveralWays(
        payload, ValidateDoneAndOffset(payload.size(), validator)));
    VERIFY_GT(fast_decode_count_, 0u);
    VERIFY_GT(slow_decode_count_, 0u);

    return AssertionSuccess();
  }

  template <size_t N>
  AssertionResult DecodePayloadAndValidateSeveralWays(
      const char (&buf)[N],
      const FrameParts& expected) {
    return DecodePayloadAndValidateSeveralWays(
        quiche::QuicheStringPiece(buf, N), expected);
  }

  template <size_t N>
  AssertionResult DecodePayloadAndValidateSeveralWays(
      const char (&buf)[N],
      const Http2FrameHeader& header) {
    return DecodePayloadAndValidateSeveralWays(
        quiche::QuicheStringPiece(buf, N), FrameParts(header));
  }

  template <size_t N>
  AssertionResult DecodePayloadExpectingError(const char (&buf)[N],
                                              const FrameParts& expected) {
    auto validator = [&expected, this](const DecodeBuffer& /*input*/,
                                       DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(status, DecodeStatus::kDecodeError);
      VERIFY_AND_RETURN_SUCCESS(VerifyCollected(expected));
    };
    ResetDecodeSpeedCounters();
    EXPECT_TRUE(
        DecodePayloadAndValidateSeveralWays(ToStringPiece(buf), validator));
    EXPECT_GT(fast_decode_count_, 0u);
    EXPECT_GT(slow_decode_count_, 0u);
    return AssertionSuccess();
  }

  template <size_t N>
  AssertionResult DecodePayloadExpectingFrameSizeError(const char (&buf)[N],
                                                       FrameParts expected) {
    expected.SetHasFrameSizeError(true);
    VERIFY_AND_RETURN_SUCCESS(DecodePayloadExpectingError(buf, expected));
  }

  template <size_t N>
  AssertionResult DecodePayloadExpectingFrameSizeError(
      const char (&buf)[N],
      const Http2FrameHeader& header) {
    return DecodePayloadExpectingFrameSizeError(buf, FrameParts(header));
  }

  // Count of payloads that are fully decoded by StartDecodingPayload or for
  // which an error was detected by StartDecodingPayload.
  size_t fast_decode_count_ = 0;

  // Count of payloads that required calling ResumeDecodingPayload in order to
  // decode completely, or for which an error was detected by
  // ResumeDecodingPayload.
  size_t slow_decode_count_ = 0;

  uint32_t maximum_payload_size_ = Http2SettingsInfo::DefaultMaxFrameSize();
  FramePartsCollectorListener collector_;
  std::unique_ptr<Http2FrameDecoder> decoder_;
  bool use_default_constructor_;
};

////////////////////////////////////////////////////////////////////////////////
// Tests that pass the minimum allowed size for the frame type, which is often
// empty. The tests are in order by frame type value (i.e. 0 for DATA frames).

TEST_F(Http2FrameDecoderTest, DataEmpty) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',  // Payload length: 0
      '\x00',                  // DATA
      '\x00',                  // Flags: none
      '\x00', '\x00', '\x00',
      '\x00',  // Stream ID: 0 (invalid but unchecked here)
  };
  Http2FrameHeader header(0, Http2FrameType::DATA, 0, 0);
  FrameParts expected(header, "");
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, HeadersEmpty) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',          // Payload length: 0
      '\x01',                          // HEADERS
      '\x00',                          // Flags: none
      '\x00', '\x00', '\x00', '\x01',  // Stream ID: 0  (REQUIRES ID)
  };
  Http2FrameHeader header(0, Http2FrameType::HEADERS, 0, 1);
  FrameParts expected(header, "");
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, Priority) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x05',          // Length: 5
      '\x02',                          //   Type: PRIORITY
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x02',  // Stream: 2
      '\x80', '\x00', '\x00', '\x01',  // Parent: 1 (Exclusive)
      '\x10',                          // Weight: 17
  };
  Http2FrameHeader header(5, Http2FrameType::PRIORITY, 0, 2);
  FrameParts expected(header);
  expected.SetOptPriority(Http2PriorityFields(1, 17, true));
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, RstStream) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x04',          // Length: 4
      '\x03',                          //   Type: RST_STREAM
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1
      '\x00', '\x00', '\x00', '\x01',  //  Error: PROTOCOL_ERROR
  };
  Http2FrameHeader header(4, Http2FrameType::RST_STREAM, 0, 1);
  FrameParts expected(header);
  expected.SetOptRstStreamErrorCode(Http2ErrorCode::PROTOCOL_ERROR);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, SettingsEmpty) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',          // Length: 0
      '\x04',                          //   Type: SETTINGS
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1 (invalid but unchecked here)
  };
  Http2FrameHeader header(0, Http2FrameType::SETTINGS, 0, 1);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, SettingsAck) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',          //   Length: 6
      '\x04',                          //     Type: SETTINGS
      '\x01',                          //    Flags: ACK
      '\x00', '\x00', '\x00', '\x00',  //   Stream: 0
  };
  Http2FrameHeader header(0, Http2FrameType::SETTINGS, Http2FrameFlag::ACK, 0);
  FrameParts expected(header);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, PushPromiseMinimal) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x04',  // Payload length: 4
      '\x05',                  // PUSH_PROMISE
      '\x04',                  // Flags: END_HEADERS
      '\x00', '\x00', '\x00',
      '\x02',  //   Stream: 2 (invalid but unchecked here)
      '\x00', '\x00', '\x00',
      '\x01',  // Promised: 1 (invalid but unchecked here)
  };
  Http2FrameHeader header(4, Http2FrameType::PUSH_PROMISE,
                          Http2FrameFlag::END_HEADERS, 2);
  FrameParts expected(header, "");
  expected.SetOptPushPromise(Http2PushPromiseFields{1});
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, Ping) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x08',          //   Length: 8
      '\x06',                          //     Type: PING
      '\xfe',                          //    Flags: no valid flags
      '\x00', '\x00', '\x00', '\x00',  //   Stream: 0
      's',    'o',    'm',    'e',     // "some"
      'd',    'a',    't',    'a',     // "data"
  };
  Http2FrameHeader header(8, Http2FrameType::PING, 0, 0);
  FrameParts expected(header);
  expected.SetOptPing(
      Http2PingFields{{'s', 'o', 'm', 'e', 'd', 'a', 't', 'a'}});
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, PingAck) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x08',          //   Length: 8
      '\x06',                          //     Type: PING
      '\xff',                          //    Flags: ACK (plus all invalid flags)
      '\x00', '\x00', '\x00', '\x00',  //   Stream: 0
      's',    'o',    'm',    'e',     // "some"
      'd',    'a',    't',    'a',     // "data"
  };
  Http2FrameHeader header(8, Http2FrameType::PING, Http2FrameFlag::ACK, 0);
  FrameParts expected(header);
  expected.SetOptPing(
      Http2PingFields{{'s', 'o', 'm', 'e', 'd', 'a', 't', 'a'}});
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, GoAwayMinimal) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x08',          // Length: 8 (no opaque data)
      '\x07',                          //   Type: GOAWAY
      '\xff',                          //  Flags: 0xff (no valid flags)
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1 (invalid but unchecked here)
      '\x80', '\x00', '\x00', '\xff',  //   Last: 255 (plus R bit)
      '\x00', '\x00', '\x00', '\x09',  //  Error: COMPRESSION_ERROR
  };
  Http2FrameHeader header(8, Http2FrameType::GOAWAY, 0, 1);
  FrameParts expected(header);
  expected.SetOptGoaway(
      Http2GoAwayFields(255, Http2ErrorCode::COMPRESSION_ERROR));
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, WindowUpdate) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x04',          // Length: 4
      '\x08',                          //   Type: WINDOW_UPDATE
      '\x0f',                          //  Flags: 0xff (no valid flags)
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1
      '\x80', '\x00', '\x04', '\x00',  //   Incr: 1024 (plus R bit)
  };
  Http2FrameHeader header(4, Http2FrameType::WINDOW_UPDATE, 0, 1);
  FrameParts expected(header);
  expected.SetOptWindowUpdateIncrement(1024);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, ContinuationEmpty) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',  // Payload length: 0
      '\x09',                  // CONTINUATION
      '\x00',                  // Flags: none
      '\x00', '\x00', '\x00',
      '\x00',  // Stream ID: 0 (invalid but unchecked here)
  };
  Http2FrameHeader header(0, Http2FrameType::CONTINUATION, 0, 0);
  FrameParts expected(header);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, AltSvcMinimal) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x02',  // Payload length: 2
      '\x0a',                  // ALTSVC
      '\xff',                  // Flags: none (plus 0xff)
      '\x00', '\x00', '\x00',
      '\x00',          // Stream ID: 0 (invalid but unchecked here)
      '\x00', '\x00',  // Origin Length: 0
  };
  Http2FrameHeader header(2, Http2FrameType::ALTSVC, 0, 0);
  FrameParts expected(header);
  expected.SetOptAltsvcOriginLength(0);
  expected.SetOptAltsvcValueLength(0);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, UnknownEmpty) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',          // Payload length: 0
      '\x20',                          // 32 (unknown)
      '\xff',                          // Flags: all
      '\x00', '\x00', '\x00', '\x00',  // Stream ID: 0
  };
  Http2FrameHeader header(0, static_cast<Http2FrameType>(32), 0xff, 0);
  FrameParts expected(header);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

////////////////////////////////////////////////////////////////////////////////
// Tests of longer payloads, for those frame types that allow longer payloads.

TEST_F(Http2FrameDecoderTest, DataPayload) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x03',          // Payload length: 7
      '\x00',                          // DATA
      '\x80',                          // Flags: 0x80
      '\x00', '\x00', '\x02', '\x02',  // Stream ID: 514
      'a',    'b',    'c',             // Data
  };
  Http2FrameHeader header(3, Http2FrameType::DATA, 0, 514);
  FrameParts expected(header, "abc");
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, HeadersPayload) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x03',          // Payload length: 3
      '\x01',                          // HEADERS
      '\x05',                          // Flags: END_STREAM | END_HEADERS
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 0  (REQUIRES ID)
      'a',    'b',    'c',  // HPACK fragment (doesn't have to be valid)
  };
  Http2FrameHeader header(
      3, Http2FrameType::HEADERS,
      Http2FrameFlag::END_STREAM | Http2FrameFlag::END_HEADERS, 2);
  FrameParts expected(header, "abc");
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, HeadersPriority) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x05',          // Payload length: 5
      '\x01',                          // HEADERS
      '\x20',                          // Flags: PRIORITY
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 0  (REQUIRES ID)
      '\x00', '\x00', '\x00', '\x01',  // Parent: 1 (Not Exclusive)
      '\xff',                          // Weight: 256
  };
  Http2FrameHeader header(5, Http2FrameType::HEADERS, Http2FrameFlag::PRIORITY,
                          2);
  FrameParts expected(header);
  expected.SetOptPriority(Http2PriorityFields(1, 256, false));
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, Settings) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x0c',          // Length: 12
      '\x04',                          //   Type: SETTINGS
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x00',  // Stream: 0
      '\x00', '\x04',                  //  Param: INITIAL_WINDOW_SIZE
      '\x0a', '\x0b', '\x0c', '\x0d',  //  Value: 168496141
      '\x00', '\x02',                  //  Param: ENABLE_PUSH
      '\x00', '\x00', '\x00', '\x03',  //  Value: 3 (invalid but unchecked here)
  };
  Http2FrameHeader header(12, Http2FrameType::SETTINGS, 0, 0);
  FrameParts expected(header);
  expected.AppendSetting(Http2SettingFields(
      Http2SettingsParameter::INITIAL_WINDOW_SIZE, 168496141));
  expected.AppendSetting(
      Http2SettingFields(Http2SettingsParameter::ENABLE_PUSH, 3));
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, PushPromisePayload) {
  const char kFrameData[] = {
      '\x00', '\x00', 7,               // Payload length: 7
      '\x05',                          // PUSH_PROMISE
      '\x04',                          // Flags: END_HEADERS
      '\x00', '\x00', '\x00', '\xff',  // Stream ID: 255
      '\x00', '\x00', '\x01', '\x00',  // Promised: 256
      'a',    'b',    'c',  // HPACK fragment (doesn't have to be valid)
  };
  Http2FrameHeader header(7, Http2FrameType::PUSH_PROMISE,
                          Http2FrameFlag::END_HEADERS, 255);
  FrameParts expected(header, "abc");
  expected.SetOptPushPromise(Http2PushPromiseFields{256});
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, GoAwayOpaqueData) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x0e',          // Length: 14
      '\x07',                          //   Type: GOAWAY
      '\xff',                          //  Flags: 0xff (no valid flags)
      '\x80', '\x00', '\x00', '\x00',  // Stream: 0 (plus R bit)
      '\x00', '\x00', '\x01', '\x00',  //   Last: 256
      '\x00', '\x00', '\x00', '\x03',  //  Error: FLOW_CONTROL_ERROR
      'o',    'p',    'a',    'q',    'u', 'e',
  };
  Http2FrameHeader header(14, Http2FrameType::GOAWAY, 0, 0);
  FrameParts expected(header, "opaque");
  expected.SetOptGoaway(
      Http2GoAwayFields(256, Http2ErrorCode::FLOW_CONTROL_ERROR));
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, ContinuationPayload) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x03',          // Payload length: 3
      '\x09',                          // CONTINUATION
      '\xff',                          // Flags: END_HEADERS | 0xfb
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 2
      'a',    'b',    'c',             // Data
  };
  Http2FrameHeader header(3, Http2FrameType::CONTINUATION,
                          Http2FrameFlag::END_HEADERS, 2);
  FrameParts expected(header, "abc");
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, AltSvcPayload) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x08',          // Payload length: 3
      '\x0a',                          // ALTSVC
      '\x00',                          // Flags: none
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 2
      '\x00', '\x03',                  // Origin Length: 0
      'a',    'b',    'c',             // Origin
      'd',    'e',    'f',             // Value
  };
  Http2FrameHeader header(8, Http2FrameType::ALTSVC, 0, 2);
  FrameParts expected(header);
  expected.SetAltSvcExpected("abc", "def");
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, UnknownPayload) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x03',          // Payload length: 3
      '\x30',                          // 48 (unknown)
      '\x00',                          // Flags: none
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 2
      'a',    'b',    'c',             // Payload
  };
  Http2FrameHeader header(3, static_cast<Http2FrameType>(48), 0, 2);
  FrameParts expected(header, "abc");
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

////////////////////////////////////////////////////////////////////////////////
// Tests of padded payloads, for those frame types that allow padding.

TEST_F(Http2FrameDecoderTest, DataPayloadAndPadding) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x07',          // Payload length: 7
      '\x00',                          // DATA
      '\x09',                          // Flags: END_STREAM | PADDED
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 0  (REQUIRES ID)
      '\x03',                          // Pad Len
      'a',    'b',    'c',             // Data
      '\x00', '\x00', '\x00',          // Padding
  };
  Http2FrameHeader header(7, Http2FrameType::DATA,
                          Http2FrameFlag::END_STREAM | Http2FrameFlag::PADDED,
                          2);
  size_t total_pad_length = 4;  // Including the Pad Length field.
  FrameParts expected(header, "abc", total_pad_length);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, HeadersPayloadAndPadding) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x07',          // Payload length: 7
      '\x01',                          // HEADERS
      '\x08',                          // Flags: PADDED
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 0  (REQUIRES ID)
      '\x03',                          // Pad Len
      'a',    'b',    'c',     // HPACK fragment (doesn't have to be valid)
      '\x00', '\x00', '\x00',  // Padding
  };
  Http2FrameHeader header(7, Http2FrameType::HEADERS, Http2FrameFlag::PADDED,
                          2);
  size_t total_pad_length = 4;  // Including the Pad Length field.
  FrameParts expected(header, "abc", total_pad_length);
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, HeadersPayloadPriorityAndPadding) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x0c',          // Payload length: 12
      '\x01',                          // HEADERS
      '\xff',                          // Flags: all, including undefined
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 0  (REQUIRES ID)
      '\x03',                          // Pad Len
      '\x80', '\x00', '\x00', '\x01',  // Parent: 1 (Exclusive)
      '\x10',                          // Weight: 17
      'a',    'b',    'c',     // HPACK fragment (doesn't have to be valid)
      '\x00', '\x00', '\x00',  // Padding
  };
  Http2FrameHeader header(12, Http2FrameType::HEADERS,
                          Http2FrameFlag::END_STREAM |
                              Http2FrameFlag::END_HEADERS |
                              Http2FrameFlag::PADDED | Http2FrameFlag::PRIORITY,
                          2);
  size_t total_pad_length = 4;  // Including the Pad Length field.
  FrameParts expected(header, "abc", total_pad_length);
  expected.SetOptPriority(Http2PriorityFields(1, 17, true));
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, PushPromisePayloadAndPadding) {
  const char kFrameData[] = {
      '\x00', '\x00', 11,              // Payload length: 11
      '\x05',                          // PUSH_PROMISE
      '\xff',                          // Flags: END_HEADERS | PADDED | 0xf3
      '\x00', '\x00', '\x00', '\x01',  // Stream ID: 1
      '\x03',                          // Pad Len
      '\x00', '\x00', '\x00', '\x02',  // Promised: 2
      'a',    'b',    'c',     // HPACK fragment (doesn't have to be valid)
      '\x00', '\x00', '\x00',  // Padding
  };
  Http2FrameHeader header(11, Http2FrameType::PUSH_PROMISE,
                          Http2FrameFlag::END_HEADERS | Http2FrameFlag::PADDED,
                          1);
  size_t total_pad_length = 4;  // Including the Pad Length field.
  FrameParts expected(header, "abc", total_pad_length);
  expected.SetOptPushPromise(Http2PushPromiseFields{2});
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(kFrameData, expected));
}

////////////////////////////////////////////////////////////////////////////////
// Payload too short errors.

TEST_F(Http2FrameDecoderTest, DataMissingPadLengthField) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',          // Payload length: 0
      '\x00',                          // DATA
      '\x08',                          // Flags: PADDED
      '\x00', '\x00', '\x00', '\x01',  // Stream ID: 1
  };
  Http2FrameHeader header(0, Http2FrameType::DATA, Http2FrameFlag::PADDED, 1);
  FrameParts expected(header);
  expected.SetOptMissingLength(1);
  EXPECT_TRUE(DecodePayloadExpectingError(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, HeaderPaddingTooLong) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x02',          // Payload length: 0
      '\x01',                          // HEADERS
      '\x08',                          // Flags: PADDED
      '\x00', '\x01', '\x00', '\x00',  // Stream ID: 65536
      '\xff',                          // Pad Len: 255
      '\x00',                          // Only one byte of padding
  };
  Http2FrameHeader header(2, Http2FrameType::HEADERS, Http2FrameFlag::PADDED,
                          65536);
  FrameParts expected(header);
  expected.SetOptMissingLength(254);
  EXPECT_TRUE(DecodePayloadExpectingError(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, HeaderMissingPriority) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x04',          // Payload length: 0
      '\x01',                          // HEADERS
      '\x20',                          // Flags: PRIORITY
      '\x00', '\x01', '\x00', '\x00',  // Stream ID: 65536
      '\x00', '\x00', '\x00', '\x00',  // Priority (truncated)
  };
  Http2FrameHeader header(4, Http2FrameType::HEADERS, Http2FrameFlag::PRIORITY,
                          65536);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, PriorityTooShort) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x04',          // Length: 5
      '\x02',                          //   Type: PRIORITY
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x02',  // Stream: 2
      '\x80', '\x00', '\x00', '\x01',  // Parent: 1 (Exclusive)
  };
  Http2FrameHeader header(4, Http2FrameType::PRIORITY, 0, 2);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, RstStreamTooShort) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x03',          // Length: 4
      '\x03',                          //   Type: RST_STREAM
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1
      '\x00', '\x00', '\x00',          //  Truncated
  };
  Http2FrameHeader header(3, Http2FrameType::RST_STREAM, 0, 1);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

// SETTINGS frames must a multiple of 6 bytes long, so an 9 byte payload is
// invalid.
TEST_F(Http2FrameDecoderTest, SettingsWrongSize) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x09',          // Length: 2
      '\x04',                          //   Type: SETTINGS
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x00',  // Stream: 0
      '\x00', '\x02',                  //  Param: ENABLE_PUSH
      '\x00', '\x00', '\x00', '\x03',  //  Value: 1
      '\x00', '\x04',                  //  Param: INITIAL_WINDOW_SIZE
      '\x00',                          //  Value: Truncated
  };
  Http2FrameHeader header(9, Http2FrameType::SETTINGS, 0, 0);
  FrameParts expected(header);
  expected.AppendSetting(
      Http2SettingFields(Http2SettingsParameter::ENABLE_PUSH, 3));
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, expected));
}

TEST_F(Http2FrameDecoderTest, PushPromiseTooShort) {
  const char kFrameData[] = {
      '\x00', '\x00', 3,               // Payload length: 3
      '\x05',                          // PUSH_PROMISE
      '\x00',                          // Flags: none
      '\x00', '\x00', '\x00', '\x01',  // Stream ID: 1
      '\x00', '\x00', '\x00',          // Truncated promise id
  };
  Http2FrameHeader header(3, Http2FrameType::PUSH_PROMISE, 0, 1);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, PushPromisePaddedTruncatedPromise) {
  const char kFrameData[] = {
      '\x00', '\x00', 4,               // Payload length: 4
      '\x05',                          // PUSH_PROMISE
      '\x08',                          // Flags: PADDED
      '\x00', '\x00', '\x00', '\x01',  // Stream ID: 1
      '\x00',                          // Pad Len
      '\x00', '\x00', '\x00',          // Truncated promise id
  };
  Http2FrameHeader header(4, Http2FrameType::PUSH_PROMISE,
                          Http2FrameFlag::PADDED, 1);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, PingTooShort) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x07',          //   Length: 8
      '\x06',                          //     Type: PING
      '\xfe',                          //    Flags: no valid flags
      '\x00', '\x00', '\x00', '\x00',  //   Stream: 0
      's',    'o',    'm',    'e',     // "some"
      'd',    'a',    't',             // Too little
  };
  Http2FrameHeader header(7, Http2FrameType::PING, 0, 0);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, GoAwayTooShort) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x00',          // Length: 0
      '\x07',                          //   Type: GOAWAY
      '\xff',                          //  Flags: 0xff (no valid flags)
      '\x00', '\x00', '\x00', '\x00',  // Stream: 0
  };
  Http2FrameHeader header(0, Http2FrameType::GOAWAY, 0, 0);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, WindowUpdateTooShort) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x03',          // Length: 3
      '\x08',                          //   Type: WINDOW_UPDATE
      '\x0f',                          //  Flags: 0xff (no valid flags)
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1
      '\x80', '\x00', '\x04',          // Truncated
  };
  Http2FrameHeader header(3, Http2FrameType::WINDOW_UPDATE, 0, 1);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, AltSvcTruncatedOriginLength) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x01',          // Payload length: 3
      '\x0a',                          // ALTSVC
      '\x00',                          // Flags: none
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 2
      '\x00',                          // Origin Length: truncated
  };
  Http2FrameHeader header(1, Http2FrameType::ALTSVC, 0, 2);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, AltSvcTruncatedOrigin) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x05',          // Payload length: 3
      '\x0a',                          // ALTSVC
      '\x00',                          // Flags: none
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 2
      '\x00', '\x04',                  // Origin Length: 4 (too long)
      'a',    'b',    'c',             // Origin
  };
  Http2FrameHeader header(5, Http2FrameType::ALTSVC, 0, 2);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

////////////////////////////////////////////////////////////////////////////////
// Payload too long errors.

// The decoder calls the listener's OnFrameSizeError method if the frame's
// payload is longer than the currently configured maximum payload size.
TEST_F(Http2FrameDecoderTest, BeyondMaximum) {
  maximum_payload_size_ = 2;
  const char kFrameData[] = {
      '\x00', '\x00', '\x07',          // Payload length: 7
      '\x00',                          // DATA
      '\x09',                          // Flags: END_STREAM | PADDED
      '\x00', '\x00', '\x00', '\x02',  // Stream ID: 0  (REQUIRES ID)
      '\x03',                          // Pad Len
      'a',    'b',    'c',             // Data
      '\x00', '\x00', '\x00',          // Padding
  };
  Http2FrameHeader header(7, Http2FrameType::DATA,
                          Http2FrameFlag::END_STREAM | Http2FrameFlag::PADDED,
                          2);
  FrameParts expected(header);
  expected.SetHasFrameSizeError(true);
  auto validator = [&expected, this](const DecodeBuffer& input,
                                     DecodeStatus status) -> AssertionResult {
    VERIFY_EQ(status, DecodeStatus::kDecodeError);
    // The decoder detects this error after decoding the header, and without
    // trying to decode the payload.
    VERIFY_EQ(input.Offset(), Http2FrameHeader::EncodedSize());
    VERIFY_AND_RETURN_SUCCESS(VerifyCollected(expected));
  };
  ResetDecodeSpeedCounters();
  EXPECT_TRUE(DecodePayloadAndValidateSeveralWays(ToStringPiece(kFrameData),
                                                  validator));
  EXPECT_GT(fast_decode_count_, 0u);
  EXPECT_GT(slow_decode_count_, 0u);
}

TEST_F(Http2FrameDecoderTest, PriorityTooLong) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x06',          // Length: 5
      '\x02',                          //   Type: PRIORITY
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x02',  // Stream: 2
      '\x80', '\x00', '\x00', '\x01',  // Parent: 1 (Exclusive)
      '\x10',                          // Weight: 17
      '\x00',                          // Too much
  };
  Http2FrameHeader header(6, Http2FrameType::PRIORITY, 0, 2);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, RstStreamTooLong) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x05',          // Length: 4
      '\x03',                          //   Type: RST_STREAM
      '\x00',                          //  Flags: none
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1
      '\x00', '\x00', '\x00', '\x01',  //  Error: PROTOCOL_ERROR
      '\x00',                          // Too much
  };
  Http2FrameHeader header(5, Http2FrameType::RST_STREAM, 0, 1);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, SettingsAckTooLong) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x06',          //   Length: 6
      '\x04',                          //     Type: SETTINGS
      '\x01',                          //    Flags: ACK
      '\x00', '\x00', '\x00', '\x00',  //   Stream: 0
      '\x00', '\x00',                  //   Extra
      '\x00', '\x00', '\x00', '\x00',  //   Extra
  };
  Http2FrameHeader header(6, Http2FrameType::SETTINGS, Http2FrameFlag::ACK, 0);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, PingAckTooLong) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x09',          //   Length: 8
      '\x06',                          //     Type: PING
      '\xff',                          //    Flags: ACK | 0xfe
      '\x00', '\x00', '\x00', '\x00',  //   Stream: 0
      's',    'o',    'm',    'e',     // "some"
      'd',    'a',    't',    'a',     // "data"
      '\x00',                          // Too much
  };
  Http2FrameHeader header(9, Http2FrameType::PING, Http2FrameFlag::ACK, 0);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

TEST_F(Http2FrameDecoderTest, WindowUpdateTooLong) {
  const char kFrameData[] = {
      '\x00', '\x00', '\x05',          // Length: 5
      '\x08',                          //   Type: WINDOW_UPDATE
      '\x0f',                          //  Flags: 0xff (no valid flags)
      '\x00', '\x00', '\x00', '\x01',  // Stream: 1
      '\x80', '\x00', '\x04', '\x00',  //   Incr: 1024 (plus R bit)
      '\x00',                          // Too much
  };
  Http2FrameHeader header(5, Http2FrameType::WINDOW_UPDATE, 0, 1);
  EXPECT_TRUE(DecodePayloadExpectingFrameSizeError(kFrameData, header));
}

}  // namespace
}  // namespace test
}  // namespace http2
