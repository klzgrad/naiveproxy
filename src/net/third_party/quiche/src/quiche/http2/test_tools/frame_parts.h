// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TEST_TOOLS_FRAME_PARTS_H_
#define QUICHE_HTTP2_TEST_TOOLS_FRAME_PARTS_H_

// FrameParts implements Http2FrameDecoderListener, recording the callbacks
// during the decoding of a single frame. It is also used for comparing the
// info that a test expects to be recorded during the decoding of a frame
// with the actual recorded value (i.e. by providing a comparator).

#include <stddef.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/http2_constants.h"
#include "quiche/http2/core/http2_structures.h"
#include "quiche/http2/decoder/http2_frame_decoder_listener.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace http2 {
namespace test {

class QUICHE_NO_EXPORT FrameParts : public Http2FrameDecoderListener {
 public:
  // The first callback for every type of frame includes the frame header; this
  // is the only constructor used during decoding of a frame.
  explicit FrameParts(const Http2FrameHeader& header);

  // For use in tests where the expected frame has a variable size payload.
  FrameParts(const Http2FrameHeader& header, absl::string_view payload);

  // For use in tests where the expected frame has a variable size payload
  // and may be padded.
  FrameParts(const Http2FrameHeader& header, absl::string_view payload,
             size_t total_pad_length);

  // Copy constructor.
  FrameParts(const FrameParts& header);

  ~FrameParts() override;

  // Returns AssertionSuccess() if they're equal, else AssertionFailure()
  // with info about the difference.
  ::testing::AssertionResult VerifyEquals(const FrameParts& other) const;

  // Format this FrameParts object.
  void OutputTo(std::ostream& out) const;

  // Set the total padding length (0 to 256).
  void SetTotalPadLength(size_t total_pad_length);

  // Set the origin and value expected in an ALTSVC frame.
  void SetAltSvcExpected(absl::string_view origin, absl::string_view value);

  // Http2FrameDecoderListener methods:
  bool OnFrameHeader(const Http2FrameHeader& header) override;
  void OnDataStart(const Http2FrameHeader& header) override;
  void OnDataPayload(const char* data, size_t len) override;
  void OnDataEnd() override;
  void OnHeadersStart(const Http2FrameHeader& header) override;
  void OnHeadersPriority(const Http2PriorityFields& priority) override;
  void OnHpackFragment(const char* data, size_t len) override;
  void OnHeadersEnd() override;
  void OnPriorityFrame(const Http2FrameHeader& header,
                       const Http2PriorityFields& priority) override;
  void OnContinuationStart(const Http2FrameHeader& header) override;
  void OnContinuationEnd() override;
  void OnPadLength(size_t trailing_length) override;
  void OnPadding(const char* pad, size_t skipped_length) override;
  void OnRstStream(const Http2FrameHeader& header,
                   Http2ErrorCode error_code) override;
  void OnSettingsStart(const Http2FrameHeader& header) override;
  void OnSetting(const Http2SettingFields& setting_fields) override;
  void OnSettingsEnd() override;
  void OnSettingsAck(const Http2FrameHeader& header) override;
  void OnPushPromiseStart(const Http2FrameHeader& header,
                          const Http2PushPromiseFields& promise,
                          size_t total_padding_length) override;
  void OnPushPromiseEnd() override;
  void OnPing(const Http2FrameHeader& header,
              const Http2PingFields& ping) override;
  void OnPingAck(const Http2FrameHeader& header,
                 const Http2PingFields& ping) override;
  void OnGoAwayStart(const Http2FrameHeader& header,
                     const Http2GoAwayFields& goaway) override;
  void OnGoAwayOpaqueData(const char* data, size_t len) override;
  void OnGoAwayEnd() override;
  void OnWindowUpdate(const Http2FrameHeader& header,
                      uint32_t increment) override;
  void OnAltSvcStart(const Http2FrameHeader& header, size_t origin_length,
                     size_t value_length) override;
  void OnAltSvcOriginData(const char* data, size_t len) override;
  void OnAltSvcValueData(const char* data, size_t len) override;
  void OnAltSvcEnd() override;
  void OnPriorityUpdateStart(
      const Http2FrameHeader& header,
      const Http2PriorityUpdateFields& priority_update) override;
  void OnPriorityUpdatePayload(const char* data, size_t len) override;
  void OnPriorityUpdateEnd() override;
  void OnUnknownStart(const Http2FrameHeader& header) override;
  void OnUnknownPayload(const char* data, size_t len) override;
  void OnUnknownEnd() override;
  void OnPaddingTooLong(const Http2FrameHeader& header,
                        size_t missing_length) override;
  void OnFrameSizeError(const Http2FrameHeader& header) override;

  void AppendSetting(const Http2SettingFields& setting_fields) {
    settings_.push_back(setting_fields);
  }

  const Http2FrameHeader& GetFrameHeader() const { return frame_header_; }

  std::optional<Http2PriorityFields> GetOptPriority() const {
    return opt_priority_;
  }
  std::optional<Http2ErrorCode> GetOptRstStreamErrorCode() const {
    return opt_rst_stream_error_code_;
  }
  std::optional<Http2PushPromiseFields> GetOptPushPromise() const {
    return opt_push_promise_;
  }
  std::optional<Http2PingFields> GetOptPing() const { return opt_ping_; }
  std::optional<Http2GoAwayFields> GetOptGoaway() const { return opt_goaway_; }
  std::optional<size_t> GetOptPadLength() const { return opt_pad_length_; }
  std::optional<size_t> GetOptPayloadLength() const {
    return opt_payload_length_;
  }
  std::optional<size_t> GetOptMissingLength() const {
    return opt_missing_length_;
  }
  std::optional<size_t> GetOptAltsvcOriginLength() const {
    return opt_altsvc_origin_length_;
  }
  std::optional<size_t> GetOptAltsvcValueLength() const {
    return opt_altsvc_value_length_;
  }
  std::optional<size_t> GetOptWindowUpdateIncrement() const {
    return opt_window_update_increment_;
  }
  bool GetHasFrameSizeError() const { return has_frame_size_error_; }

  void SetOptPriority(std::optional<Http2PriorityFields> opt_priority) {
    opt_priority_ = opt_priority;
  }
  void SetOptRstStreamErrorCode(
      std::optional<Http2ErrorCode> opt_rst_stream_error_code) {
    opt_rst_stream_error_code_ = opt_rst_stream_error_code;
  }
  void SetOptPushPromise(
      std::optional<Http2PushPromiseFields> opt_push_promise) {
    opt_push_promise_ = opt_push_promise;
  }
  void SetOptPing(std::optional<Http2PingFields> opt_ping) {
    opt_ping_ = opt_ping;
  }
  void SetOptGoaway(std::optional<Http2GoAwayFields> opt_goaway) {
    opt_goaway_ = opt_goaway;
  }
  void SetOptPadLength(std::optional<size_t> opt_pad_length) {
    opt_pad_length_ = opt_pad_length;
  }
  void SetOptPayloadLength(std::optional<size_t> opt_payload_length) {
    opt_payload_length_ = opt_payload_length;
  }
  void SetOptMissingLength(std::optional<size_t> opt_missing_length) {
    opt_missing_length_ = opt_missing_length;
  }
  void SetOptAltsvcOriginLength(
      std::optional<size_t> opt_altsvc_origin_length) {
    opt_altsvc_origin_length_ = opt_altsvc_origin_length;
  }
  void SetOptAltsvcValueLength(std::optional<size_t> opt_altsvc_value_length) {
    opt_altsvc_value_length_ = opt_altsvc_value_length;
  }
  void SetOptWindowUpdateIncrement(
      std::optional<size_t> opt_window_update_increment) {
    opt_window_update_increment_ = opt_window_update_increment;
  }
  void SetOptPriorityUpdate(
      std::optional<Http2PriorityUpdateFields> priority_update) {
    opt_priority_update_ = priority_update;
  }

  void SetHasFrameSizeError(bool has_frame_size_error) {
    has_frame_size_error_ = has_frame_size_error;
  }

 private:
  // ASSERT during an On* method that we're handling a frame of type
  // expected_frame_type, and have not already received other On* methods
  // (i.e. got_start_callback is false).
  ::testing::AssertionResult StartFrameOfType(
      const Http2FrameHeader& header, Http2FrameType expected_frame_type);

  // ASSERT that StartFrameOfType has already been called with
  // expected_frame_type (i.e. got_start_callback has been called), and that
  // EndFrameOfType has not yet been called (i.e. got_end_callback is false).
  ::testing::AssertionResult InFrameOfType(Http2FrameType expected_frame_type);

  // ASSERT that we're InFrameOfType, and then sets got_end_callback=true.
  ::testing::AssertionResult EndFrameOfType(Http2FrameType expected_frame_type);

  // ASSERT that we're in the middle of processing a frame that is padded.
  ::testing::AssertionResult InPaddedFrame();

  // Append source to target. If opt_length is not nullptr, then verifies that
  // the optional has a value (i.e. that the necessary On*Start method has been
  // called), and that target is not longer than opt_length->value().
  ::testing::AssertionResult AppendString(absl::string_view source,
                                          std::string* target,
                                          std::optional<size_t>* opt_length);

  const Http2FrameHeader frame_header_;

  std::string payload_;
  std::string padding_;
  std::string altsvc_origin_;
  std::string altsvc_value_;

  std::optional<Http2PriorityFields> opt_priority_;
  std::optional<Http2ErrorCode> opt_rst_stream_error_code_;
  std::optional<Http2PushPromiseFields> opt_push_promise_;
  std::optional<Http2PingFields> opt_ping_;
  std::optional<Http2GoAwayFields> opt_goaway_;
  std::optional<Http2PriorityUpdateFields> opt_priority_update_;

  std::optional<size_t> opt_pad_length_;
  std::optional<size_t> opt_payload_length_;
  std::optional<size_t> opt_missing_length_;
  std::optional<size_t> opt_altsvc_origin_length_;
  std::optional<size_t> opt_altsvc_value_length_;

  std::optional<size_t> opt_window_update_increment_;

  bool has_frame_size_error_ = false;

  std::vector<Http2SettingFields> settings_;

  // These booleans are not checked by CompareCollectedFrames.
  bool got_start_callback_ = false;
  bool got_end_callback_ = false;
};

QUICHE_NO_EXPORT std::ostream& operator<<(std::ostream& out,
                                          const FrameParts& v);

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TEST_TOOLS_FRAME_PARTS_H_
