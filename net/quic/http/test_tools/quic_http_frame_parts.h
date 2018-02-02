// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_TEST_TOOLS_QUIC_HTTP_FRAME_PARTS_H_
#define NET_QUIC_HTTP_TEST_TOOLS_QUIC_HTTP_FRAME_PARTS_H_

// QuicHttpFrameParts implements QuicHttpFrameDecoderListener, recording the
// callbacks during the decoding of a single frame. It is also used for
// comparing the info that a test expects to be recorded during the decoding of
// a frame with the actual recorded value (i.e. by providing a comparator).

// TODO(jamessynge): Convert QuicHttpFrameParts to a class, hide the members,
// add getters/setters.

#include <stddef.h>

#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_optional.h"
#include "net/quic/platform/api/quic_string.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

// Forward declarations.
struct QuicHttpFrameParts;
std::ostream& operator<<(std::ostream& out, const QuicHttpFrameParts& v);

struct QuicHttpFrameParts : public QuicHttpFrameDecoderListener {
  // The first callback for every type of frame includes the frame header; this
  // is the only constructor used during decoding of a frame.
  explicit QuicHttpFrameParts(const QuicHttpFrameHeader& header);

  // For use in tests where the expected frame has a variable size payload.
  QuicHttpFrameParts(const QuicHttpFrameHeader& header,
                     QuicStringPiece payload);

  // For use in tests where the expected frame has a variable size payload
  // and may be padded.
  QuicHttpFrameParts(const QuicHttpFrameHeader& header,
                     QuicStringPiece payload,
                     size_t total_pad_length);

  // Copy constructor.
  QuicHttpFrameParts(const QuicHttpFrameParts& header);

  ~QuicHttpFrameParts() override;

  // Returns AssertionSuccess() if they're equal, else AssertionFailure()
  // with info about the difference.
  ::testing::AssertionResult VerifyEquals(const QuicHttpFrameParts& that) const;

  // Format this QuicHttpFrameParts object.
  void OutputTo(std::ostream& out) const;

  // Set the total padding length (0 to 256).
  void SetTotalPadLength(size_t total_pad_length);

  // Set the origin and value expected in an ALTSVC frame.
  void SetAltSvcExpected(QuicStringPiece origin, QuicStringPiece value);

  // QuicHttpFrameDecoderListener methods:
  bool OnFrameHeader(const QuicHttpFrameHeader& header) override;
  void OnDataStart(const QuicHttpFrameHeader& header) override;
  void OnDataPayload(const char* data, size_t len) override;
  void OnDataEnd() override;
  void OnHeadersStart(const QuicHttpFrameHeader& header) override;
  void OnHeadersPriority(const QuicHttpPriorityFields& priority) override;
  void OnHpackFragment(const char* data, size_t len) override;
  void OnHeadersEnd() override;
  void OnPriorityFrame(const QuicHttpFrameHeader& header,
                       const QuicHttpPriorityFields& priority) override;
  void OnContinuationStart(const QuicHttpFrameHeader& header) override;
  void OnContinuationEnd() override;
  void OnPadLength(size_t trailing_length) override;
  void OnPadding(const char* pad, size_t skipped_length) override;
  void OnRstStream(const QuicHttpFrameHeader& header,
                   QuicHttpErrorCode error_code) override;
  void OnSettingsStart(const QuicHttpFrameHeader& header) override;
  void OnSetting(const QuicHttpSettingFields& setting_fields) override;
  void OnSettingsEnd() override;
  void OnSettingsAck(const QuicHttpFrameHeader& header) override;
  void OnPushPromiseStart(const QuicHttpFrameHeader& header,
                          const QuicHttpPushPromiseFields& promise,
                          size_t total_padding_length) override;
  void OnPushPromiseEnd() override;
  void OnPing(const QuicHttpFrameHeader& header,
              const QuicHttpPingFields& ping) override;
  void OnPingAck(const QuicHttpFrameHeader& header,
                 const QuicHttpPingFields& ping) override;
  void OnGoAwayStart(const QuicHttpFrameHeader& header,
                     const QuicHttpGoAwayFields& goaway) override;
  void OnGoAwayOpaqueData(const char* data, size_t len) override;
  void OnGoAwayEnd() override;
  void OnWindowUpdate(const QuicHttpFrameHeader& header,
                      uint32_t increment) override;
  void OnAltSvcStart(const QuicHttpFrameHeader& header,
                     size_t origin_length,
                     size_t value_length) override;
  void OnAltSvcOriginData(const char* data, size_t len) override;
  void OnAltSvcValueData(const char* data, size_t len) override;
  void OnAltSvcEnd() override;
  void OnUnknownStart(const QuicHttpFrameHeader& header) override;
  void OnUnknownPayload(const char* data, size_t len) override;
  void OnUnknownEnd() override;
  void OnPaddingTooLong(const QuicHttpFrameHeader& header,
                        size_t missing_length) override;
  void OnFrameSizeError(const QuicHttpFrameHeader& header) override;

  // The fields are public for access by tests.

  const QuicHttpFrameHeader frame_header;

  QuicString payload;
  QuicString padding;
  QuicString altsvc_origin;
  QuicString altsvc_value;

  QuicOptional<QuicHttpPriorityFields> opt_priority;
  QuicOptional<QuicHttpErrorCode> opt_rst_stream_error_code;
  QuicOptional<QuicHttpPushPromiseFields> opt_push_promise;
  QuicOptional<QuicHttpPingFields> opt_ping;
  QuicOptional<QuicHttpGoAwayFields> opt_goaway;

  QuicOptional<size_t> opt_pad_length;
  QuicOptional<size_t> opt_payload_length;
  QuicOptional<size_t> opt_missing_length;
  QuicOptional<size_t> opt_altsvc_origin_length;
  QuicOptional<size_t> opt_altsvc_value_length;

  QuicOptional<size_t> opt_window_update_increment;

  bool has_frame_size_error = false;

  std::vector<QuicHttpSettingFields> settings;

  // These booleans are not checked by CompareCollectedFrames.
  bool got_start_callback = false;
  bool got_end_callback = false;

 private:
  // ASSERT during an On* method that we're handling a frame of type
  // expected_frame_type, and have not already received other On* methods
  // (i.e. got_start_callback is false).
  ::testing::AssertionResult StartFrameOfType(
      const QuicHttpFrameHeader& header,
      QuicHttpFrameType expected_frame_type);

  // ASSERT that StartFrameOfType has already been called with
  // expected_frame_type (i.e. got_start_callback has been called), and that
  // EndFrameOfType has not yet been called (i.e. got_end_callback is false).
  ::testing::AssertionResult InFrameOfType(
      QuicHttpFrameType expected_frame_type);

  // ASSERT that we're InFrameOfType, and then sets got_end_callback=true.
  ::testing::AssertionResult EndFrameOfType(
      QuicHttpFrameType expected_frame_type);

  // ASSERT that we're in the middle of processing a frame that is padded.
  ::testing::AssertionResult InPaddedFrame();

  // Append source to target. If opt_length is not nullptr, then verifies that
  // the optional has a value (i.e. that the necessary On*Start method has been
  // called), and that target is not longer than opt_length->value().
  ::testing::AssertionResult AppendString(QuicStringPiece source,
                                          QuicString* target,
                                          QuicOptional<size_t>* opt_length);
};

std::ostream& operator<<(std::ostream& out, const QuicHttpFrameParts& v);

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_HTTP_TEST_TOOLS_QUIC_HTTP_FRAME_PARTS_H_
