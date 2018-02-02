// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/test_tools/quic_http_frame_parts.h"

#include <type_traits>

#include "net/base/escape.h"
#include "net/http2/tools/failure.h"
#include "net/quic/http/quic_http_structures_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::ContainerEq;

namespace net {
namespace test {
namespace {

static_assert(
    std::is_base_of<QuicHttpFrameDecoderListener, QuicHttpFrameParts>::value &&
        !std::is_abstract<QuicHttpFrameParts>::value,
    "QuicHttpFrameParts needs to implement all of the methods of "
    "QuicHttpFrameDecoderListener");

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

QuicHttpFrameParts::QuicHttpFrameParts(const QuicHttpFrameHeader& header)
    : frame_header(header) {
  VLOG(1) << "QuicHttpFrameParts, header: " << frame_header;
}

QuicHttpFrameParts::QuicHttpFrameParts(const QuicHttpFrameHeader& header,
                                       QuicStringPiece payload)
    : QuicHttpFrameParts(header) {
  VLOG(1) << "QuicHttpFrameParts with payload.size() = " << payload.size();
  this->payload.append(payload.data(), payload.size());
  opt_payload_length = payload.size();
}
QuicHttpFrameParts::QuicHttpFrameParts(const QuicHttpFrameHeader& header,
                                       QuicStringPiece payload,
                                       size_t total_pad_length)
    : QuicHttpFrameParts(header, payload) {
  VLOG(1) << "QuicHttpFrameParts with total_pad_length=" << total_pad_length;
  SetTotalPadLength(total_pad_length);
}

QuicHttpFrameParts::QuicHttpFrameParts(const QuicHttpFrameParts& header) =
    default;

QuicHttpFrameParts::~QuicHttpFrameParts() {}

AssertionResult QuicHttpFrameParts::VerifyEquals(
    const QuicHttpFrameParts& that) const {
#define COMMON_MESSAGE "\n  this: " << *this << "\n  that: " << that

  VERIFY_EQ(frame_header, that.frame_header) << COMMON_MESSAGE;
  VERIFY_EQ(payload, that.payload) << COMMON_MESSAGE;
  VERIFY_EQ(padding, that.padding) << COMMON_MESSAGE;
  VERIFY_EQ(altsvc_origin, that.altsvc_origin) << COMMON_MESSAGE;
  VERIFY_EQ(altsvc_value, that.altsvc_value) << COMMON_MESSAGE;
  VERIFY_THAT(settings, ContainerEq(that.settings)) << COMMON_MESSAGE;

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

void QuicHttpFrameParts::SetTotalPadLength(size_t total_pad_length) {
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

void QuicHttpFrameParts::SetAltSvcExpected(QuicStringPiece origin,
                                           QuicStringPiece value) {
  altsvc_origin.append(origin.data(), origin.size());
  altsvc_value.append(value.data(), value.size());
  opt_altsvc_origin_length = origin.size();
  opt_altsvc_value_length = value.size();
}

bool QuicHttpFrameParts::OnFrameHeader(const QuicHttpFrameHeader& header) {
  ADD_FAILURE() << "OnFrameHeader: " << *this;
  return true;
}

void QuicHttpFrameParts::OnDataStart(const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnDataStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::DATA)) << *this;
  opt_payload_length = header.payload_length;
}

void QuicHttpFrameParts::OnDataPayload(const char* data, size_t len) {
  VLOG(1) << "OnDataPayload: len=" << len << "; frame_header: " << frame_header;
  ASSERT_TRUE(InFrameOfType(QuicHttpFrameType::DATA)) << *this;
  ASSERT_TRUE(
      AppendString(QuicStringPiece(data, len), &payload, &opt_payload_length));
}

void QuicHttpFrameParts::OnDataEnd() {
  VLOG(1) << "OnDataEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::DATA)) << *this;
}

void QuicHttpFrameParts::OnHeadersStart(const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnHeadersStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::HEADERS)) << *this;
  opt_payload_length = header.payload_length;
}

void QuicHttpFrameParts::OnHeadersPriority(
    const QuicHttpPriorityFields& priority) {
  VLOG(1) << "OnHeadersPriority: priority: " << priority
          << "; frame_header: " << frame_header;
  ASSERT_TRUE(InFrameOfType(QuicHttpFrameType::HEADERS)) << *this;
  ASSERT_FALSE(opt_priority);
  opt_priority = priority;
  ASSERT_TRUE(opt_payload_length);
  opt_payload_length =
      opt_payload_length.value() - QuicHttpPriorityFields::EncodedSize();
}

void QuicHttpFrameParts::OnHpackFragment(const char* data, size_t len) {
  VLOG(1) << "OnHpackFragment: len=" << len
          << "; frame_header: " << frame_header;
  ASSERT_TRUE(got_start_callback);
  ASSERT_FALSE(got_end_callback);
  ASSERT_TRUE(FrameCanHaveHpackPayload(frame_header)) << *this;
  ASSERT_TRUE(
      AppendString(QuicStringPiece(data, len), &payload, &opt_payload_length));
}

void QuicHttpFrameParts::OnHeadersEnd() {
  VLOG(1) << "OnHeadersEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::HEADERS)) << *this;
}

void QuicHttpFrameParts::OnPriorityFrame(
    const QuicHttpFrameHeader& header,
    const QuicHttpPriorityFields& priority) {
  VLOG(1) << "OnPriorityFrame: " << header << "; priority: " << priority;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::QUIC_HTTP_PRIORITY))
      << *this;
  ASSERT_FALSE(opt_priority);
  opt_priority = priority;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::QUIC_HTTP_PRIORITY)) << *this;
}

void QuicHttpFrameParts::OnContinuationStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnContinuationStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::CONTINUATION))
      << *this;
  opt_payload_length = header.payload_length;
}

void QuicHttpFrameParts::OnContinuationEnd() {
  VLOG(1) << "OnContinuationEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::CONTINUATION)) << *this;
}

void QuicHttpFrameParts::OnPadLength(size_t trailing_length) {
  VLOG(1) << "OnPadLength: trailing_length=" << trailing_length;
  ASSERT_TRUE(InPaddedFrame()) << *this;
  ASSERT_FALSE(opt_pad_length);
  ASSERT_TRUE(opt_payload_length);
  size_t total_padding_length = trailing_length + 1;
  ASSERT_GE(opt_payload_length.value(), total_padding_length);
  opt_payload_length = opt_payload_length.value() - total_padding_length;
  opt_pad_length = trailing_length;
}

void QuicHttpFrameParts::OnPadding(const char* pad, size_t skipped_length) {
  VLOG(1) << "OnPadding: skipped_length=" << skipped_length;
  ASSERT_TRUE(InPaddedFrame()) << *this;
  ASSERT_TRUE(opt_pad_length);
  ASSERT_TRUE(AppendString(QuicStringPiece(pad, skipped_length), &padding,
                           &opt_pad_length));
}

void QuicHttpFrameParts::OnRstStream(const QuicHttpFrameHeader& header,
                                     QuicHttpErrorCode error_code) {
  VLOG(1) << "OnRstStream: " << header << "; code=" << error_code;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::RST_STREAM)) << *this;
  ASSERT_FALSE(opt_rst_stream_error_code);
  opt_rst_stream_error_code = error_code;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::RST_STREAM)) << *this;
}

void QuicHttpFrameParts::OnSettingsStart(const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnSettingsStart: " << header;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::SETTINGS)) << *this;
  ASSERT_EQ(0u, settings.size());
  ASSERT_FALSE(header.IsAck()) << header;
}

void QuicHttpFrameParts::OnSetting(
    const QuicHttpSettingFields& setting_fields) {
  VLOG(1) << "OnSetting: " << setting_fields;
  ASSERT_TRUE(InFrameOfType(QuicHttpFrameType::SETTINGS)) << *this;
  settings.push_back(setting_fields);
}

void QuicHttpFrameParts::OnSettingsEnd() {
  VLOG(1) << "OnSettingsEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::SETTINGS)) << *this;
}

void QuicHttpFrameParts::OnSettingsAck(const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnSettingsAck: " << header;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::SETTINGS)) << *this;
  ASSERT_EQ(0u, settings.size());
  ASSERT_TRUE(header.IsAck());
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::SETTINGS)) << *this;
}

void QuicHttpFrameParts::OnPushPromiseStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpPushPromiseFields& promise,
    size_t total_padding_length) {
  VLOG(1) << "OnPushPromiseStart header: " << header << "; promise: " << promise
          << "; total_padding_length: " << total_padding_length;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::PUSH_PROMISE))
      << *this;
  ASSERT_GE(header.payload_length, QuicHttpPushPromiseFields::EncodedSize());
  opt_payload_length =
      header.payload_length - QuicHttpPushPromiseFields::EncodedSize();
  ASSERT_FALSE(opt_push_promise);
  opt_push_promise = promise;
  if (total_padding_length > 0) {
    ASSERT_GE(opt_payload_length.value(), total_padding_length);
    OnPadLength(total_padding_length - 1);
  } else {
    ASSERT_FALSE(header.IsPadded());
  }
}

void QuicHttpFrameParts::OnPushPromiseEnd() {
  VLOG(1) << "OnPushPromiseEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::PUSH_PROMISE)) << *this;
}

void QuicHttpFrameParts::OnPing(const QuicHttpFrameHeader& header,
                                const QuicHttpPingFields& ping) {
  VLOG(1) << "OnPing header: " << header << "   ping: " << ping;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::PING)) << *this;
  ASSERT_FALSE(header.IsAck());
  ASSERT_FALSE(opt_ping);
  opt_ping = ping;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::PING)) << *this;
}

void QuicHttpFrameParts::OnPingAck(const QuicHttpFrameHeader& header,
                                   const QuicHttpPingFields& ping) {
  VLOG(1) << "OnPingAck header: " << header << "   ping: " << ping;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::PING)) << *this;
  ASSERT_TRUE(header.IsAck());
  ASSERT_FALSE(opt_ping);
  opt_ping = ping;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::PING)) << *this;
}

void QuicHttpFrameParts::OnGoAwayStart(const QuicHttpFrameHeader& header,
                                       const QuicHttpGoAwayFields& goaway) {
  VLOG(1) << "OnGoAwayStart: " << goaway;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::GOAWAY)) << *this;
  ASSERT_FALSE(opt_goaway);
  opt_goaway = goaway;
  opt_payload_length =
      header.payload_length - QuicHttpGoAwayFields::EncodedSize();
}

void QuicHttpFrameParts::OnGoAwayOpaqueData(const char* data, size_t len) {
  VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  ASSERT_TRUE(InFrameOfType(QuicHttpFrameType::GOAWAY)) << *this;
  ASSERT_TRUE(
      AppendString(QuicStringPiece(data, len), &payload, &opt_payload_length));
}

void QuicHttpFrameParts::OnGoAwayEnd() {
  VLOG(1) << "OnGoAwayEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::GOAWAY)) << *this;
}

void QuicHttpFrameParts::OnWindowUpdate(const QuicHttpFrameHeader& header,
                                        uint32_t increment) {
  VLOG(1) << "OnWindowUpdate header: " << header
          << "     increment=" << increment;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::WINDOW_UPDATE))
      << *this;
  ASSERT_FALSE(opt_window_update_increment);
  opt_window_update_increment = increment;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::WINDOW_UPDATE)) << *this;
}

void QuicHttpFrameParts::OnAltSvcStart(const QuicHttpFrameHeader& header,
                                       size_t origin_length,
                                       size_t value_length) {
  VLOG(1) << "OnAltSvcStart: " << header
          << "    origin_length: " << origin_length
          << "    value_length: " << value_length;
  ASSERT_TRUE(StartFrameOfType(header, QuicHttpFrameType::ALTSVC)) << *this;
  ASSERT_FALSE(opt_altsvc_origin_length);
  opt_altsvc_origin_length = origin_length;
  ASSERT_FALSE(opt_altsvc_value_length);
  opt_altsvc_value_length = value_length;
}

void QuicHttpFrameParts::OnAltSvcOriginData(const char* data, size_t len) {
  VLOG(1) << "OnAltSvcOriginData: len=" << len;
  ASSERT_TRUE(InFrameOfType(QuicHttpFrameType::ALTSVC)) << *this;
  ASSERT_TRUE(AppendString(QuicStringPiece(data, len), &altsvc_origin,
                           &opt_altsvc_origin_length));
}

void QuicHttpFrameParts::OnAltSvcValueData(const char* data, size_t len) {
  VLOG(1) << "OnAltSvcValueData: len=" << len;
  ASSERT_TRUE(InFrameOfType(QuicHttpFrameType::ALTSVC)) << *this;
  ASSERT_TRUE(AppendString(QuicStringPiece(data, len), &altsvc_value,
                           &opt_altsvc_value_length));
}

void QuicHttpFrameParts::OnAltSvcEnd() {
  VLOG(1) << "OnAltSvcEnd; frame_header: " << frame_header;
  ASSERT_TRUE(EndFrameOfType(QuicHttpFrameType::ALTSVC)) << *this;
}

void QuicHttpFrameParts::OnUnknownStart(const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnUnknownStart: " << header;
  ASSERT_FALSE(IsSupportedQuicHttpFrameType(header.type)) << header;
  ASSERT_FALSE(got_start_callback);
  ASSERT_EQ(frame_header, header);
  got_start_callback = true;
  opt_payload_length = header.payload_length;
}

void QuicHttpFrameParts::OnUnknownPayload(const char* data, size_t len) {
  VLOG(1) << "OnUnknownPayload: len=" << len;
  ASSERT_FALSE(IsSupportedQuicHttpFrameType(frame_header.type)) << *this;
  ASSERT_TRUE(got_start_callback);
  ASSERT_FALSE(got_end_callback);
  ASSERT_TRUE(
      AppendString(QuicStringPiece(data, len), &payload, &opt_payload_length));
}

void QuicHttpFrameParts::OnUnknownEnd() {
  VLOG(1) << "OnUnknownEnd; frame_header: " << frame_header;
  ASSERT_FALSE(IsSupportedQuicHttpFrameType(frame_header.type)) << *this;
  ASSERT_TRUE(got_start_callback);
  ASSERT_FALSE(got_end_callback);
  got_end_callback = true;
}

void QuicHttpFrameParts::OnPaddingTooLong(const QuicHttpFrameHeader& header,
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

void QuicHttpFrameParts::OnFrameSizeError(const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnFrameSizeError: " << header;
  ASSERT_EQ(frame_header, header);
  ASSERT_FALSE(got_end_callback);
  ASSERT_FALSE(has_frame_size_error);
  has_frame_size_error = true;
  got_end_callback = true;
}

void QuicHttpFrameParts::OutputTo(std::ostream& out) const {
  out << "QuicHttpFrameParts{\n  frame_header: " << frame_header << "\n";
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

AssertionResult QuicHttpFrameParts::StartFrameOfType(
    const QuicHttpFrameHeader& header,
    QuicHttpFrameType expected_frame_type) {
  VERIFY_EQ(header.type, expected_frame_type);
  VERIFY_FALSE(got_start_callback);
  VERIFY_FALSE(got_end_callback);
  VERIFY_EQ(frame_header, header);
  got_start_callback = true;
  return AssertionSuccess();
}

AssertionResult QuicHttpFrameParts::InFrameOfType(
    QuicHttpFrameType expected_frame_type) {
  VERIFY_TRUE(got_start_callback);
  VERIFY_FALSE(got_end_callback);
  VERIFY_EQ(frame_header.type, expected_frame_type);
  return AssertionSuccess();
}

AssertionResult QuicHttpFrameParts::EndFrameOfType(
    QuicHttpFrameType expected_frame_type) {
  VERIFY_SUCCESS(InFrameOfType(expected_frame_type));
  got_end_callback = true;
  return AssertionSuccess();
}

AssertionResult QuicHttpFrameParts::InPaddedFrame() {
  VERIFY_TRUE(got_start_callback);
  VERIFY_FALSE(got_end_callback);
  VERIFY_TRUE(FrameIsPadded(frame_header));
  return AssertionSuccess();
}

AssertionResult QuicHttpFrameParts::AppendString(
    QuicStringPiece source,
    QuicString* target,
    QuicOptional<size_t>* opt_length) {
  target->append(source.data(), source.size());
  if (opt_length != nullptr) {
    VERIFY_TRUE(*opt_length) << "Length is not set yet\n" << *this;
    VERIFY_LE(target->size(), opt_length->value())
        << "String too large; source.size() = " << source.size() << "\n"
        << *this;
  }
  return ::testing::AssertionSuccess();
}

std::ostream& operator<<(std::ostream& out, const QuicHttpFrameParts& v) {
  v.OutputTo(out);
  return out;
}

}  // namespace test
}  // namespace net
