// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/test_tools/frame_parts.h"

#include <type_traits>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/http2_structures_test_util.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::ContainerEq;

namespace http2 {
namespace test {
namespace {

static_assert(std::is_base_of<Http2FrameDecoderListener, FrameParts>::value &&
                  !std::is_abstract<FrameParts>::value,
              "FrameParts needs to implement all of the methods of "
              "Http2FrameDecoderListener");

// Compare two optional variables of the same type.
// TODO(jamessynge): Maybe create a ::testing::Matcher for this.
template <class T>
AssertionResult VerifyOptionalEq(const T& opt_a, const T& opt_b) {
  if (opt_a) {
    if (opt_b) {
      VERIFY_EQ(opt_a.value(), opt_b.value());
    } else {
      return AssertionFailure()
             << "opt_b is not set; opt_a.value()=" << opt_a.value();
    }
  } else if (opt_b) {
    return AssertionFailure()
           << "opt_a is not set; opt_b.value()=" << opt_b.value();
  }
  return AssertionSuccess();
}

}  // namespace

FrameParts::FrameParts(const Http2FrameHeader& header) : frame_header_(header) {
  HTTP2_VLOG(1) << "FrameParts, header: " << frame_header_;
}

FrameParts::FrameParts(const Http2FrameHeader& header,
                       quiche::QuicheStringPiece payload)
    : FrameParts(header) {
  HTTP2_VLOG(1) << "FrameParts with payload.size() = " << payload.size();
  this->payload_.append(payload.data(), payload.size());
  opt_payload_length_ = payload.size();
}
FrameParts::FrameParts(const Http2FrameHeader& header,
                       quiche::QuicheStringPiece payload,
                       size_t total_pad_length)
    : FrameParts(header, payload) {
  HTTP2_VLOG(1) << "FrameParts with total_pad_length=" << total_pad_length;
  SetTotalPadLength(total_pad_length);
}

FrameParts::FrameParts(const FrameParts& header) = default;

FrameParts::~FrameParts() = default;

AssertionResult FrameParts::VerifyEquals(const FrameParts& that) const {
#define COMMON_MESSAGE "\n  this: " << *this << "\n  that: " << that

  VERIFY_EQ(frame_header_, that.frame_header_) << COMMON_MESSAGE;
  VERIFY_EQ(payload_, that.payload_) << COMMON_MESSAGE;
  VERIFY_EQ(padding_, that.padding_) << COMMON_MESSAGE;
  VERIFY_EQ(altsvc_origin_, that.altsvc_origin_) << COMMON_MESSAGE;
  VERIFY_EQ(altsvc_value_, that.altsvc_value_) << COMMON_MESSAGE;
  VERIFY_THAT(settings_, ContainerEq(that.settings_)) << COMMON_MESSAGE;

#define VERIFY_OPTIONAL_FIELD(field_name) \
  VERIFY_SUCCESS(VerifyOptionalEq(field_name, that.field_name))

  VERIFY_OPTIONAL_FIELD(opt_altsvc_origin_length_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_altsvc_value_length_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_goaway_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_missing_length_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_pad_length_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_ping_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_priority_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_push_promise_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_rst_stream_error_code_) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_window_update_increment_) << COMMON_MESSAGE;

#undef VERIFY_OPTIONAL_FIELD

  return AssertionSuccess();
}

void FrameParts::SetTotalPadLength(size_t total_pad_length) {
  opt_pad_length_.reset();
  padding_.clear();
  if (total_pad_length > 0) {
    ASSERT_LE(total_pad_length, 256u);
    ASSERT_TRUE(frame_header_.IsPadded());
    opt_pad_length_ = total_pad_length - 1;
    char zero = 0;
    padding_.append(opt_pad_length_.value(), zero);
  }

  if (opt_pad_length_) {
    HTTP2_VLOG(1) << "SetTotalPadLength: pad_length="
                  << opt_pad_length_.value();
  } else {
    HTTP2_VLOG(1) << "SetTotalPadLength: has no pad length";
  }
}

void FrameParts::SetAltSvcExpected(quiche::QuicheStringPiece origin,
                                   quiche::QuicheStringPiece value) {
  altsvc_origin_.append(origin.data(), origin.size());
  altsvc_value_.append(value.data(), value.size());
  opt_altsvc_origin_length_ = origin.size();
  opt_altsvc_value_length_ = value.size();
}

bool FrameParts::OnFrameHeader(const Http2FrameHeader& header) {
  ADD_FAILURE() << "OnFrameHeader: " << *this;
  return true;
}

void FrameParts::OnDataStart(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnDataStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::DATA)) << *this;
  opt_payload_length_ = header.payload_length;
}

void FrameParts::OnDataPayload(const char* data, size_t len) {
  HTTP2_VLOG(1) << "OnDataPayload: len=" << len
                << "; frame_header_: " << frame_header_;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::DATA)) << *this;
  ASSERT_TRUE(AppendString(quiche::QuicheStringPiece(data, len), &payload_,
                           &opt_payload_length_));
}

void FrameParts::OnDataEnd() {
  HTTP2_VLOG(1) << "OnDataEnd; frame_header_: " << frame_header_;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::DATA)) << *this;
}

void FrameParts::OnHeadersStart(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnHeadersStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::HEADERS)) << *this;
  opt_payload_length_ = header.payload_length;
}

void FrameParts::OnHeadersPriority(const Http2PriorityFields& priority) {
  HTTP2_VLOG(1) << "OnHeadersPriority: priority: " << priority
                << "; frame_header_: " << frame_header_;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::HEADERS)) << *this;
  ASSERT_FALSE(opt_priority_);
  opt_priority_ = priority;
  ASSERT_TRUE(opt_payload_length_);
  opt_payload_length_ =
      opt_payload_length_.value() - Http2PriorityFields::EncodedSize();
}

void FrameParts::OnHpackFragment(const char* data, size_t len) {
  HTTP2_VLOG(1) << "OnHpackFragment: len=" << len
                << "; frame_header_: " << frame_header_;
  ASSERT_TRUE(got_start_callback_);
  ASSERT_FALSE(got_end_callback_);
  ASSERT_TRUE(FrameCanHaveHpackPayload(frame_header_)) << *this;
  ASSERT_TRUE(AppendString(quiche::QuicheStringPiece(data, len), &payload_,
                           &opt_payload_length_));
}

void FrameParts::OnHeadersEnd() {
  HTTP2_VLOG(1) << "OnHeadersEnd; frame_header_: " << frame_header_;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::HEADERS)) << *this;
}

void FrameParts::OnPriorityFrame(const Http2FrameHeader& header,
                                 const Http2PriorityFields& priority) {
  HTTP2_VLOG(1) << "OnPriorityFrame: " << header << "; priority: " << priority;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PRIORITY)) << *this;
  ASSERT_FALSE(opt_priority_);
  opt_priority_ = priority;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PRIORITY)) << *this;
}

void FrameParts::OnContinuationStart(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnContinuationStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::CONTINUATION)) << *this;
  opt_payload_length_ = header.payload_length;
}

void FrameParts::OnContinuationEnd() {
  HTTP2_VLOG(1) << "OnContinuationEnd; frame_header_: " << frame_header_;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::CONTINUATION)) << *this;
}

void FrameParts::OnPadLength(size_t trailing_length) {
  HTTP2_VLOG(1) << "OnPadLength: trailing_length=" << trailing_length;
  ASSERT_TRUE(InPaddedFrame()) << *this;
  ASSERT_FALSE(opt_pad_length_);
  ASSERT_TRUE(opt_payload_length_);
  size_t total_padding_length = trailing_length + 1;
  ASSERT_GE(opt_payload_length_.value(), total_padding_length);
  opt_payload_length_ = opt_payload_length_.value() - total_padding_length;
  opt_pad_length_ = trailing_length;
}

void FrameParts::OnPadding(const char* pad, size_t skipped_length) {
  HTTP2_VLOG(1) << "OnPadding: skipped_length=" << skipped_length;
  ASSERT_TRUE(InPaddedFrame()) << *this;
  ASSERT_TRUE(opt_pad_length_);
  ASSERT_TRUE(AppendString(quiche::QuicheStringPiece(pad, skipped_length),
                           &padding_, &opt_pad_length_));
}

void FrameParts::OnRstStream(const Http2FrameHeader& header,
                             Http2ErrorCode error_code) {
  HTTP2_VLOG(1) << "OnRstStream: " << header << "; code=" << error_code;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::RST_STREAM)) << *this;
  ASSERT_FALSE(opt_rst_stream_error_code_);
  opt_rst_stream_error_code_ = error_code;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::RST_STREAM)) << *this;
}

void FrameParts::OnSettingsStart(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnSettingsStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::SETTINGS)) << *this;
  ASSERT_EQ(0u, settings_.size());
  ASSERT_FALSE(header.IsAck()) << header;
}

void FrameParts::OnSetting(const Http2SettingFields& setting_fields) {
  HTTP2_VLOG(1) << "OnSetting: " << setting_fields;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::SETTINGS)) << *this;
  settings_.push_back(setting_fields);
}

void FrameParts::OnSettingsEnd() {
  HTTP2_VLOG(1) << "OnSettingsEnd; frame_header_: " << frame_header_;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::SETTINGS)) << *this;
}

void FrameParts::OnSettingsAck(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnSettingsAck: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::SETTINGS)) << *this;
  ASSERT_EQ(0u, settings_.size());
  ASSERT_TRUE(header.IsAck());
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::SETTINGS)) << *this;
}

void FrameParts::OnPushPromiseStart(const Http2FrameHeader& header,
                                    const Http2PushPromiseFields& promise,
                                    size_t total_padding_length) {
  HTTP2_VLOG(1) << "OnPushPromiseStart header: " << header
                << "; promise: " << promise
                << "; total_padding_length: " << total_padding_length;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PUSH_PROMISE)) << *this;
  ASSERT_GE(header.payload_length, Http2PushPromiseFields::EncodedSize());
  opt_payload_length_ =
      header.payload_length - Http2PushPromiseFields::EncodedSize();
  ASSERT_FALSE(opt_push_promise_);
  opt_push_promise_ = promise;
  if (total_padding_length > 0) {
    ASSERT_GE(opt_payload_length_.value(), total_padding_length);
    OnPadLength(total_padding_length - 1);
  } else {
    ASSERT_FALSE(header.IsPadded());
  }
}

void FrameParts::OnPushPromiseEnd() {
  HTTP2_VLOG(1) << "OnPushPromiseEnd; frame_header_: " << frame_header_;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PUSH_PROMISE)) << *this;
}

void FrameParts::OnPing(const Http2FrameHeader& header,
                        const Http2PingFields& ping) {
  HTTP2_VLOG(1) << "OnPing header: " << header << "   ping: " << ping;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PING)) << *this;
  ASSERT_FALSE(header.IsAck());
  ASSERT_FALSE(opt_ping_);
  opt_ping_ = ping;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PING)) << *this;
}

void FrameParts::OnPingAck(const Http2FrameHeader& header,
                           const Http2PingFields& ping) {
  HTTP2_VLOG(1) << "OnPingAck header: " << header << "   ping: " << ping;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PING)) << *this;
  ASSERT_TRUE(header.IsAck());
  ASSERT_FALSE(opt_ping_);
  opt_ping_ = ping;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PING)) << *this;
}

void FrameParts::OnGoAwayStart(const Http2FrameHeader& header,
                               const Http2GoAwayFields& goaway) {
  HTTP2_VLOG(1) << "OnGoAwayStart: " << goaway;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::GOAWAY)) << *this;
  ASSERT_FALSE(opt_goaway_);
  opt_goaway_ = goaway;
  opt_payload_length_ =
      header.payload_length - Http2GoAwayFields::EncodedSize();
}

void FrameParts::OnGoAwayOpaqueData(const char* data, size_t len) {
  HTTP2_VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::GOAWAY)) << *this;
  ASSERT_TRUE(AppendString(quiche::QuicheStringPiece(data, len), &payload_,
                           &opt_payload_length_));
}

void FrameParts::OnGoAwayEnd() {
  HTTP2_VLOG(1) << "OnGoAwayEnd; frame_header_: " << frame_header_;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::GOAWAY)) << *this;
}

void FrameParts::OnWindowUpdate(const Http2FrameHeader& header,
                                uint32_t increment) {
  HTTP2_VLOG(1) << "OnWindowUpdate header: " << header
                << "     increment=" << increment;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::WINDOW_UPDATE)) << *this;
  ASSERT_FALSE(opt_window_update_increment_);
  opt_window_update_increment_ = increment;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::WINDOW_UPDATE)) << *this;
}

void FrameParts::OnAltSvcStart(const Http2FrameHeader& header,
                               size_t origin_length,
                               size_t value_length) {
  HTTP2_VLOG(1) << "OnAltSvcStart: " << header
                << "    origin_length: " << origin_length
                << "    value_length: " << value_length;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::ALTSVC)) << *this;
  ASSERT_FALSE(opt_altsvc_origin_length_);
  opt_altsvc_origin_length_ = origin_length;
  ASSERT_FALSE(opt_altsvc_value_length_);
  opt_altsvc_value_length_ = value_length;
}

void FrameParts::OnAltSvcOriginData(const char* data, size_t len) {
  HTTP2_VLOG(1) << "OnAltSvcOriginData: len=" << len;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::ALTSVC)) << *this;
  ASSERT_TRUE(AppendString(quiche::QuicheStringPiece(data, len),
                           &altsvc_origin_, &opt_altsvc_origin_length_));
}

void FrameParts::OnAltSvcValueData(const char* data, size_t len) {
  HTTP2_VLOG(1) << "OnAltSvcValueData: len=" << len;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::ALTSVC)) << *this;
  ASSERT_TRUE(AppendString(quiche::QuicheStringPiece(data, len), &altsvc_value_,
                           &opt_altsvc_value_length_));
}

void FrameParts::OnAltSvcEnd() {
  HTTP2_VLOG(1) << "OnAltSvcEnd; frame_header_: " << frame_header_;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::ALTSVC)) << *this;
}

void FrameParts::OnUnknownStart(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnUnknownStart: " << header;
  ASSERT_FALSE(IsSupportedHttp2FrameType(header.type)) << header;
  ASSERT_FALSE(got_start_callback_);
  ASSERT_EQ(frame_header_, header);
  got_start_callback_ = true;
  opt_payload_length_ = header.payload_length;
}

void FrameParts::OnUnknownPayload(const char* data, size_t len) {
  HTTP2_VLOG(1) << "OnUnknownPayload: len=" << len;
  ASSERT_FALSE(IsSupportedHttp2FrameType(frame_header_.type)) << *this;
  ASSERT_TRUE(got_start_callback_);
  ASSERT_FALSE(got_end_callback_);
  ASSERT_TRUE(AppendString(quiche::QuicheStringPiece(data, len), &payload_,
                           &opt_payload_length_));
}

void FrameParts::OnUnknownEnd() {
  HTTP2_VLOG(1) << "OnUnknownEnd; frame_header_: " << frame_header_;
  ASSERT_FALSE(IsSupportedHttp2FrameType(frame_header_.type)) << *this;
  ASSERT_TRUE(got_start_callback_);
  ASSERT_FALSE(got_end_callback_);
  got_end_callback_ = true;
}

void FrameParts::OnPaddingTooLong(const Http2FrameHeader& header,
                                  size_t missing_length) {
  HTTP2_VLOG(1) << "OnPaddingTooLong: " << header
                << "; missing_length: " << missing_length;
  ASSERT_EQ(frame_header_, header);
  ASSERT_FALSE(got_end_callback_);
  ASSERT_TRUE(FrameIsPadded(header));
  ASSERT_FALSE(opt_pad_length_);
  ASSERT_FALSE(opt_missing_length_);
  opt_missing_length_ = missing_length;
  got_start_callback_ = true;
  got_end_callback_ = true;
}

void FrameParts::OnFrameSizeError(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
  ASSERT_EQ(frame_header_, header);
  ASSERT_FALSE(got_end_callback_);
  ASSERT_FALSE(has_frame_size_error_);
  has_frame_size_error_ = true;
  got_end_callback_ = true;
}

void FrameParts::OutputTo(std::ostream& out) const {
  out << "FrameParts{\n  frame_header_: " << frame_header_ << "\n";
  if (!payload_.empty()) {
    out << "  payload_=\"" << Http2HexEscape(payload_) << "\"\n";
  }
  if (!padding_.empty()) {
    out << "  padding_=\"" << Http2HexEscape(padding_) << "\"\n";
  }
  if (!altsvc_origin_.empty()) {
    out << "  altsvc_origin_=\"" << Http2HexEscape(altsvc_origin_) << "\"\n";
  }
  if (!altsvc_value_.empty()) {
    out << "  altsvc_value_=\"" << Http2HexEscape(altsvc_value_) << "\"\n";
  }
  if (opt_priority_) {
    out << "  priority=" << opt_priority_.value() << "\n";
  }
  if (opt_rst_stream_error_code_) {
    out << "  rst_stream=" << opt_rst_stream_error_code_.value() << "\n";
  }
  if (opt_push_promise_) {
    out << "  push_promise=" << opt_push_promise_.value() << "\n";
  }
  if (opt_ping_) {
    out << "  ping=" << opt_ping_.value() << "\n";
  }
  if (opt_goaway_) {
    out << "  goaway=" << opt_goaway_.value() << "\n";
  }
  if (opt_window_update_increment_) {
    out << "  window_update=" << opt_window_update_increment_.value() << "\n";
  }
  if (opt_payload_length_) {
    out << "  payload_length=" << opt_payload_length_.value() << "\n";
  }
  if (opt_pad_length_) {
    out << "  pad_length=" << opt_pad_length_.value() << "\n";
  }
  if (opt_missing_length_) {
    out << "  missing_length=" << opt_missing_length_.value() << "\n";
  }
  if (opt_altsvc_origin_length_) {
    out << "  origin_length=" << opt_altsvc_origin_length_.value() << "\n";
  }
  if (opt_altsvc_value_length_) {
    out << "  value_length=" << opt_altsvc_value_length_.value() << "\n";
  }
  if (has_frame_size_error_) {
    out << "  has_frame_size_error\n";
  }
  if (got_start_callback_) {
    out << "  got_start_callback\n";
  }
  if (got_end_callback_) {
    out << "  got_end_callback\n";
  }
  for (size_t ndx = 0; ndx < settings_.size(); ++ndx) {
    out << "  setting[" << ndx << "]=" << settings_[ndx];
  }
  out << "}";
}

AssertionResult FrameParts::StartFrameOfType(
    const Http2FrameHeader& header,
    Http2FrameType expected_frame_type) {
  VERIFY_EQ(header.type, expected_frame_type);
  VERIFY_FALSE(got_start_callback_);
  VERIFY_FALSE(got_end_callback_);
  VERIFY_EQ(frame_header_, header);
  got_start_callback_ = true;
  return AssertionSuccess();
}

AssertionResult FrameParts::InFrameOfType(Http2FrameType expected_frame_type) {
  VERIFY_TRUE(got_start_callback_);
  VERIFY_FALSE(got_end_callback_);
  VERIFY_EQ(frame_header_.type, expected_frame_type);
  return AssertionSuccess();
}

AssertionResult FrameParts::EndFrameOfType(Http2FrameType expected_frame_type) {
  VERIFY_SUCCESS(InFrameOfType(expected_frame_type));
  got_end_callback_ = true;
  return AssertionSuccess();
}

AssertionResult FrameParts::InPaddedFrame() {
  VERIFY_TRUE(got_start_callback_);
  VERIFY_FALSE(got_end_callback_);
  VERIFY_TRUE(FrameIsPadded(frame_header_));
  return AssertionSuccess();
}

AssertionResult FrameParts::AppendString(
    quiche::QuicheStringPiece source,
    std::string* target,
    quiche::QuicheOptional<size_t>* opt_length) {
  target->append(source.data(), source.size());
  if (opt_length != nullptr) {
    VERIFY_TRUE(*opt_length) << "Length is not set yet\n" << *this;
    VERIFY_LE(target->size(), opt_length->value())
        << "String too large; source.size() = " << source.size() << "\n"
        << *this;
  }
  return ::testing::AssertionSuccess();
}

std::ostream& operator<<(std::ostream& out, const FrameParts& v) {
  v.OutputTo(out);
  return out;
}

}  // namespace test
}  // namespace http2
