// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/test_tools/frame_parts.h"

#include <type_traits>

#include "base/logging.h"
#include "net/base/escape.h"
#include "net/http2/http2_structures_test_util.h"
#include "net/http2/tools/failure.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::ContainerEq;

namespace net {
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
      return AssertionFailure() << "opt_b is not set; opt_a.value()="
                                << opt_a.value();
    }
  } else if (opt_b) {
    return AssertionFailure() << "opt_a is not set; opt_b.value()="
                              << opt_b.value();
  }
  return AssertionSuccess();
}

}  // namespace

FrameParts::FrameParts(const Http2FrameHeader& header) : frame_header(header) {
  VLOG(1) << "FrameParts, header: " << frame_header;
}

FrameParts::FrameParts(const Http2FrameHeader& header, Http2StringPiece payload)
    : FrameParts(header) {
  VLOG(1) << "FrameParts with payload.size() = " << payload.size();
  this->payload.append(payload.data(), payload.size());
  opt_payload_length = payload.size();
}
FrameParts::FrameParts(const Http2FrameHeader& header,
                       Http2StringPiece payload,
                       size_t total_pad_length)
    : FrameParts(header, payload) {
  VLOG(1) << "FrameParts with total_pad_length=" << total_pad_length;
  SetTotalPadLength(total_pad_length);
}

FrameParts::FrameParts(const FrameParts& header) = default;

FrameParts::~FrameParts() {}

AssertionResult FrameParts::VerifyEquals(const FrameParts& that) const {
#define COMMON_MESSAGE "\n  this: " << *this << "\n  that: " << that

  VERIFY_EQ(frame_header, that.frame_header) << COMMON_MESSAGE;
  VERIFY_EQ(payload, that.payload) << COMMON_MESSAGE;
  VERIFY_EQ(padding, that.padding) << COMMON_MESSAGE;
  VERIFY_EQ(altsvc_origin, that.altsvc_origin) << COMMON_MESSAGE;
  VERIFY_EQ(altsvc_value, that.altsvc_value) << COMMON_MESSAGE;
  VERIFY_EQ(settings, that.settings) << COMMON_MESSAGE;

#define VERIFY_OPTIONAL_FIELD(field_name) \
  VERIFY_SUCCESS(VerifyOptionalEq(field_name, that.field_name))

  VERIFY_OPTIONAL_FIELD(opt_altsvc_origin_length) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_altsvc_value_length) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_goaway) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_missing_length) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_pad_length) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_ping) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_priority) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_push_promise) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_rst_stream_error_code) << COMMON_MESSAGE;
  VERIFY_OPTIONAL_FIELD(opt_window_update_increment) << COMMON_MESSAGE;

#undef VERIFY_OPTIONAL_FIELD

  return AssertionSuccess();
}

void FrameParts::SetTotalPadLength(size_t total_pad_length) {
  opt_pad_length.reset();
  padding.clear();
  if (total_pad_length > 0) {
    ASSERT_LE(total_pad_length, 256u);
    ASSERT_TRUE(frame_header.IsPadded());
    opt_pad_length = total_pad_length - 1;
    char zero = 0;
    padding.append(opt_pad_length.value(), zero);
  }

  if (opt_pad_length) {
    VLOG(1) << "SetTotalPadLength: pad_length=" << opt_pad_length.value();
  } else {
    VLOG(1) << "SetTotalPadLength: has no pad length";
  }
}

void FrameParts::SetAltSvcExpected(Http2StringPiece origin,
                                   Http2StringPiece value) {
  altsvc_origin.append(origin.data(), origin.size());
  altsvc_value.append(value.data(), value.size());
  opt_altsvc_origin_length = origin.size();
  opt_altsvc_value_length = value.size();
}

bool FrameParts::OnFrameHeader(const Http2FrameHeader& header) {
  ADD_FAILURE() << "OnFrameHeader: " << *this;
  return true;
}

void FrameParts::OnDataStart(const Http2FrameHeader& header) {
  VLOG(1) << "OnDataStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::DATA)) << *this;
  opt_payload_length = header.payload_length;
}

void FrameParts::OnDataPayload(const char* data, size_t len) {
  VLOG(1) << "OnDataPayload: len=" << len << "; frame_header: " << frame_header;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::DATA)) << *this;
  ASSERT_TRUE(
      AppendString(Http2StringPiece(data, len), &payload, &opt_payload_length));
}

void FrameParts::OnDataEnd() {
  VLOG(1) << "OnDataEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::DATA)) << *this;
}

void FrameParts::OnHeadersStart(const Http2FrameHeader& header) {
  VLOG(1) << "OnHeadersStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::HEADERS)) << *this;
  opt_payload_length = header.payload_length;
}

void FrameParts::OnHeadersPriority(const Http2PriorityFields& priority) {
  VLOG(1) << "OnHeadersPriority: priority: " << priority
          << "; frame_header: " << frame_header;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::HEADERS)) << *this;
  ASSERT_FALSE(opt_priority);
  opt_priority = priority;
  ASSERT_TRUE(opt_payload_length);
  opt_payload_length =
      opt_payload_length.value() - Http2PriorityFields::EncodedSize();
}

void FrameParts::OnHpackFragment(const char* data, size_t len) {
  VLOG(1) << "OnHpackFragment: len=" << len
          << "; frame_header: " << frame_header;
  ASSERT_TRUE(got_start_callback);
  ASSERT_FALSE(got_end_callback);
  ASSERT_TRUE(FrameCanHaveHpackPayload(frame_header)) << *this;
  ASSERT_TRUE(
      AppendString(Http2StringPiece(data, len), &payload, &opt_payload_length));
}

void FrameParts::OnHeadersEnd() {
  VLOG(1) << "OnHeadersEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::HEADERS)) << *this;
}

void FrameParts::OnPriorityFrame(const Http2FrameHeader& header,
                                 const Http2PriorityFields& priority) {
  VLOG(1) << "OnPriorityFrame: " << header << "; priority: " << priority;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PRIORITY)) << *this;
  ASSERT_FALSE(opt_priority);
  opt_priority = priority;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PRIORITY)) << *this;
}

void FrameParts::OnContinuationStart(const Http2FrameHeader& header) {
  VLOG(1) << "OnContinuationStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::CONTINUATION)) << *this;
  opt_payload_length = header.payload_length;
}

void FrameParts::OnContinuationEnd() {
  VLOG(1) << "OnContinuationEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::CONTINUATION)) << *this;
}

void FrameParts::OnPadLength(size_t trailing_length) {
  VLOG(1) << "OnPadLength: trailing_length=" << trailing_length;
  ASSERT_TRUE(InPaddedFrame()) << *this;
  ASSERT_FALSE(opt_pad_length);
  ASSERT_TRUE(opt_payload_length);
  size_t total_padding_length = trailing_length + 1;
  ASSERT_GE(opt_payload_length.value(), total_padding_length);
  opt_payload_length = opt_payload_length.value() - total_padding_length;
  opt_pad_length = trailing_length;
}

void FrameParts::OnPadding(const char* pad, size_t skipped_length) {
  VLOG(1) << "OnPadding: skipped_length=" << skipped_length;
  ASSERT_TRUE(InPaddedFrame()) << *this;
  ASSERT_TRUE(opt_pad_length);
  ASSERT_TRUE(AppendString(Http2StringPiece(pad, skipped_length), &padding,
                           &opt_pad_length));
}

void FrameParts::OnRstStream(const Http2FrameHeader& header,
                             Http2ErrorCode error_code) {
  VLOG(1) << "OnRstStream: " << header << "; code=" << error_code;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::RST_STREAM)) << *this;
  ASSERT_FALSE(opt_rst_stream_error_code);
  opt_rst_stream_error_code = error_code;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::RST_STREAM)) << *this;
}

void FrameParts::OnSettingsStart(const Http2FrameHeader& header) {
  VLOG(1) << "OnSettingsStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::SETTINGS)) << *this;
  ASSERT_EQ(0u, settings.size());
  ASSERT_FALSE(header.IsAck()) << header;
}

void FrameParts::OnSetting(const Http2SettingFields& setting_fields) {
  VLOG(1) << "OnSetting: " << setting_fields;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::SETTINGS)) << *this;
  settings.push_back(setting_fields);
}

void FrameParts::OnSettingsEnd() {
  VLOG(1) << "OnSettingsEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::SETTINGS)) << *this;
}

void FrameParts::OnSettingsAck(const Http2FrameHeader& header) {
  VLOG(1) << "OnSettingsAck: " << header;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::SETTINGS)) << *this;
  ASSERT_EQ(0u, settings.size());
  ASSERT_TRUE(header.IsAck());
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::SETTINGS)) << *this;
}

void FrameParts::OnPushPromiseStart(const Http2FrameHeader& header,
                                    const Http2PushPromiseFields& promise,
                                    size_t total_padding_length) {
  VLOG(1) << "OnPushPromiseStart header: " << header << "; promise: " << promise
          << "; total_padding_length: " << total_padding_length;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PUSH_PROMISE)) << *this;
  ASSERT_GE(header.payload_length, Http2PushPromiseFields::EncodedSize());
  opt_payload_length =
      header.payload_length - Http2PushPromiseFields::EncodedSize();
  ASSERT_FALSE(opt_push_promise);
  opt_push_promise = promise;
  if (total_padding_length > 0) {
    ASSERT_GE(opt_payload_length.value(), total_padding_length);
    OnPadLength(total_padding_length - 1);
  } else {
    ASSERT_FALSE(header.IsPadded());
  }
}

void FrameParts::OnPushPromiseEnd() {
  VLOG(1) << "OnPushPromiseEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PUSH_PROMISE)) << *this;
}

void FrameParts::OnPing(const Http2FrameHeader& header,
                        const Http2PingFields& ping) {
  VLOG(1) << "OnPing header: " << header << "   ping: " << ping;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PING)) << *this;
  ASSERT_FALSE(header.IsAck());
  ASSERT_FALSE(opt_ping);
  opt_ping = ping;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PING)) << *this;
}

void FrameParts::OnPingAck(const Http2FrameHeader& header,
                           const Http2PingFields& ping) {
  VLOG(1) << "OnPingAck header: " << header << "   ping: " << ping;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::PING)) << *this;
  ASSERT_TRUE(header.IsAck());
  ASSERT_FALSE(opt_ping);
  opt_ping = ping;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::PING)) << *this;
}

void FrameParts::OnGoAwayStart(const Http2FrameHeader& header,
                               const Http2GoAwayFields& goaway) {
  VLOG(1) << "OnGoAwayStart: " << goaway;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::GOAWAY)) << *this;
  ASSERT_FALSE(opt_goaway);
  opt_goaway = goaway;
  opt_payload_length = header.payload_length - Http2GoAwayFields::EncodedSize();
}

void FrameParts::OnGoAwayOpaqueData(const char* data, size_t len) {
  VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::GOAWAY)) << *this;
  ASSERT_TRUE(
      AppendString(Http2StringPiece(data, len), &payload, &opt_payload_length));
}

void FrameParts::OnGoAwayEnd() {
  VLOG(1) << "OnGoAwayEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::GOAWAY)) << *this;
}

void FrameParts::OnWindowUpdate(const Http2FrameHeader& header,
                                uint32_t increment) {
  VLOG(1) << "OnWindowUpdate header: " << header
          << "     increment=" << increment;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::WINDOW_UPDATE)) << *this;
  ASSERT_FALSE(opt_window_update_increment);
  opt_window_update_increment = increment;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::WINDOW_UPDATE)) << *this;
}

void FrameParts::OnAltSvcStart(const Http2FrameHeader& header,
                               size_t origin_length,
                               size_t value_length) {
  VLOG(1) << "OnAltSvcStart: " << header
          << "    origin_length: " << origin_length
          << "    value_length: " << value_length;
  ASSERT_TRUE(StartFrameOfType(header, Http2FrameType::ALTSVC)) << *this;
  ASSERT_FALSE(opt_altsvc_origin_length);
  opt_altsvc_origin_length = origin_length;
  ASSERT_FALSE(opt_altsvc_value_length);
  opt_altsvc_value_length = value_length;
}

void FrameParts::OnAltSvcOriginData(const char* data, size_t len) {
  VLOG(1) << "OnAltSvcOriginData: len=" << len;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::ALTSVC)) << *this;
  ASSERT_TRUE(AppendString(Http2StringPiece(data, len), &altsvc_origin,
                           &opt_altsvc_origin_length));
}

void FrameParts::OnAltSvcValueData(const char* data, size_t len) {
  VLOG(1) << "OnAltSvcValueData: len=" << len;
  ASSERT_TRUE(InFrameOfType(Http2FrameType::ALTSVC)) << *this;
  ASSERT_TRUE(AppendString(Http2StringPiece(data, len), &altsvc_value,
                           &opt_altsvc_value_length));
}

void FrameParts::OnAltSvcEnd() {
  VLOG(1) << "OnAltSvcEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(Http2FrameType::ALTSVC)) << *this;
}

void FrameParts::OnUnknownStart(const Http2FrameHeader& header) {
  VLOG(1) << "OnUnknownStart: " << header;
  ASSERT_FALSE(IsSupportedHttp2FrameType(header.type)) << header;
  ASSERT_FALSE(got_start_callback);
  ASSERT_EQ(frame_header, header);
  got_start_callback = true;
  opt_payload_length = header.payload_length;
}

void FrameParts::OnUnknownPayload(const char* data, size_t len) {
  VLOG(1) << "OnUnknownPayload: len=" << len;
  ASSERT_FALSE(IsSupportedHttp2FrameType(frame_header.type)) << *this;
  ASSERT_TRUE(got_start_callback);
  ASSERT_FALSE(got_end_callback);
  ASSERT_TRUE(
      AppendString(Http2StringPiece(data, len), &payload, &opt_payload_length));
}

void FrameParts::OnUnknownEnd() {
  VLOG(1) << "OnUnknownEnd; frame_header: " << frame_header;
  ASSERT_FALSE(IsSupportedHttp2FrameType(frame_header.type)) << *this;
  ASSERT_TRUE(got_start_callback);
  ASSERT_FALSE(got_end_callback);
  got_end_callback = true;
}

void FrameParts::OnPaddingTooLong(const Http2FrameHeader& header,
                                  size_t missing_length) {
  VLOG(1) << "OnPaddingTooLong: " << header
          << "; missing_length: " << missing_length;
  ASSERT_EQ(frame_header, header);
  ASSERT_FALSE(got_end_callback);
  ASSERT_TRUE(FrameIsPadded(header));
  ASSERT_FALSE(opt_pad_length);
  ASSERT_FALSE(opt_missing_length);
  opt_missing_length = missing_length;
  got_start_callback = true;
  got_end_callback = true;
}

void FrameParts::OnFrameSizeError(const Http2FrameHeader& header) {
  VLOG(1) << "OnFrameSizeError: " << header;
  ASSERT_EQ(frame_header, header);
  ASSERT_FALSE(got_end_callback);
  ASSERT_FALSE(has_frame_size_error);
  has_frame_size_error = true;
  got_end_callback = true;
}

void FrameParts::OutputTo(std::ostream& out) const {
  out << "FrameParts{\n  frame_header: " << frame_header << "\n";
  if (!payload.empty()) {
    out << "  payload=\"" << EscapeQueryParamValue(payload, false) << "\"\n";
  }
  if (!padding.empty()) {
    out << "  padding=\"" << EscapeQueryParamValue(padding, false) << "\"\n";
  }
  if (!altsvc_origin.empty()) {
    out << "  altsvc_origin=\"" << EscapeQueryParamValue(altsvc_origin, false)
        << "\"\n";
  }
  if (!altsvc_value.empty()) {
    out << "  altsvc_value=\"" << EscapeQueryParamValue(altsvc_value, false)
        << "\"\n";
  }
  if (opt_priority) {
    out << "  priority=" << opt_priority.value() << "\n";
  }
  if (opt_rst_stream_error_code) {
    out << "  rst_stream=" << opt_rst_stream_error_code.value() << "\n";
  }
  if (opt_push_promise) {
    out << "  push_promise=" << opt_push_promise.value() << "\n";
  }
  if (opt_ping) {
    out << "  ping=" << opt_ping.value() << "\n";
  }
  if (opt_goaway) {
    out << "  goaway=" << opt_goaway.value() << "\n";
  }
  if (opt_window_update_increment) {
    out << "  window_update=" << opt_window_update_increment.value() << "\n";
  }
  if (opt_payload_length) {
    out << "  payload_length=" << opt_payload_length.value() << "\n";
  }
  if (opt_pad_length) {
    out << "  pad_length=" << opt_pad_length.value() << "\n";
  }
  if (opt_missing_length) {
    out << "  missing_length=" << opt_missing_length.value() << "\n";
  }
  if (opt_altsvc_origin_length) {
    out << "  origin_length=" << opt_altsvc_origin_length.value() << "\n";
  }
  if (opt_altsvc_value_length) {
    out << "  value_length=" << opt_altsvc_value_length.value() << "\n";
  }
  if (has_frame_size_error) {
    out << "  has_frame_size_error\n";
  }
  if (got_start_callback) {
    out << "  got_start_callback\n";
  }
  if (got_end_callback) {
    out << "  got_end_callback\n";
  }
  for (size_t ndx = 0; ndx < settings.size(); ++ndx) {
    out << "  setting[" << ndx << "]=" << settings[ndx];
  }
  out << "}";
}

AssertionResult FrameParts::StartFrameOfType(
    const Http2FrameHeader& header,
    Http2FrameType expected_frame_type) {
  VERIFY_EQ(header.type, expected_frame_type);
  VERIFY_FALSE(got_start_callback);
  VERIFY_FALSE(got_end_callback);
  VERIFY_EQ(frame_header, header);
  got_start_callback = true;
  return AssertionSuccess();
}

AssertionResult FrameParts::InFrameOfType(Http2FrameType expected_frame_type) {
  VERIFY_TRUE(got_start_callback);
  VERIFY_FALSE(got_end_callback);
  VERIFY_EQ(frame_header.type, expected_frame_type);
  return AssertionSuccess();
}

AssertionResult FrameParts::EndFrameOfType(Http2FrameType expected_frame_type) {
  VERIFY_SUCCESS(InFrameOfType(expected_frame_type));
  got_end_callback = true;
  return AssertionSuccess();
}

AssertionResult FrameParts::InPaddedFrame() {
  VERIFY_TRUE(got_start_callback);
  VERIFY_FALSE(got_end_callback);
  VERIFY_TRUE(FrameIsPadded(frame_header));
  return AssertionSuccess();
}

AssertionResult FrameParts::AppendString(Http2StringPiece source,
                                         Http2String* target,
                                         base::Optional<size_t>* opt_length) {
  target->append(source.data(), source.size());
  if (opt_length != nullptr) {
    VERIFY_TRUE(*opt_length) << "Length is not set yet\n" << *this;
    VERIFY_LE(target->size(), static_cast<size_t>(opt_length->value()))
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
}  // namespace net
