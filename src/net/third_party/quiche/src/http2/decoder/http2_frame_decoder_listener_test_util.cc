// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener_test_util.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

FailingHttp2FrameDecoderListener::FailingHttp2FrameDecoderListener() = default;
FailingHttp2FrameDecoderListener::~FailingHttp2FrameDecoderListener() = default;

bool FailingHttp2FrameDecoderListener::OnFrameHeader(
    const Http2FrameHeader& header) {
  ADD_FAILURE() << "OnFrameHeader: " << header;
  return false;
}

void FailingHttp2FrameDecoderListener::OnDataStart(
    const Http2FrameHeader& header) {
  FAIL() << "OnDataStart: " << header;
}

void FailingHttp2FrameDecoderListener::OnDataPayload(const char* /*data*/,
                                                     size_t len) {
  FAIL() << "OnDataPayload: len=" << len;
}

void FailingHttp2FrameDecoderListener::OnDataEnd() {
  FAIL() << "OnDataEnd";
}

void FailingHttp2FrameDecoderListener::OnHeadersStart(
    const Http2FrameHeader& header) {
  FAIL() << "OnHeadersStart: " << header;
}

void FailingHttp2FrameDecoderListener::OnHeadersPriority(
    const Http2PriorityFields& priority) {
  FAIL() << "OnHeadersPriority: " << priority;
}

void FailingHttp2FrameDecoderListener::OnHpackFragment(const char* /*data*/,
                                                       size_t len) {
  FAIL() << "OnHpackFragment: len=" << len;
}

void FailingHttp2FrameDecoderListener::OnHeadersEnd() {
  FAIL() << "OnHeadersEnd";
}

void FailingHttp2FrameDecoderListener::OnPriorityFrame(
    const Http2FrameHeader& header,
    const Http2PriorityFields& priority) {
  FAIL() << "OnPriorityFrame: " << header << "; priority: " << priority;
}

void FailingHttp2FrameDecoderListener::OnContinuationStart(
    const Http2FrameHeader& header) {
  FAIL() << "OnContinuationStart: " << header;
}

void FailingHttp2FrameDecoderListener::OnContinuationEnd() {
  FAIL() << "OnContinuationEnd";
}

void FailingHttp2FrameDecoderListener::OnPadLength(size_t trailing_length) {
  FAIL() << "OnPadLength: trailing_length=" << trailing_length;
}

void FailingHttp2FrameDecoderListener::OnPadding(const char* /*padding*/,
                                                 size_t skipped_length) {
  FAIL() << "OnPadding: skipped_length=" << skipped_length;
}

void FailingHttp2FrameDecoderListener::OnRstStream(
    const Http2FrameHeader& header,
    Http2ErrorCode error_code) {
  FAIL() << "OnRstStream: " << header << "; code=" << error_code;
}

void FailingHttp2FrameDecoderListener::OnSettingsStart(
    const Http2FrameHeader& header) {
  FAIL() << "OnSettingsStart: " << header;
}

void FailingHttp2FrameDecoderListener::OnSetting(
    const Http2SettingFields& setting_fields) {
  FAIL() << "OnSetting: " << setting_fields;
}

void FailingHttp2FrameDecoderListener::OnSettingsEnd() {
  FAIL() << "OnSettingsEnd";
}

void FailingHttp2FrameDecoderListener::OnSettingsAck(
    const Http2FrameHeader& header) {
  FAIL() << "OnSettingsAck: " << header;
}

void FailingHttp2FrameDecoderListener::OnPushPromiseStart(
    const Http2FrameHeader& header,
    const Http2PushPromiseFields& promise,
    size_t total_padding_length) {
  FAIL() << "OnPushPromiseStart: " << header << "; promise: " << promise
         << "; total_padding_length: " << total_padding_length;
}

void FailingHttp2FrameDecoderListener::OnPushPromiseEnd() {
  FAIL() << "OnPushPromiseEnd";
}

void FailingHttp2FrameDecoderListener::OnPing(const Http2FrameHeader& header,
                                              const Http2PingFields& ping) {
  FAIL() << "OnPing: " << header << "; ping: " << ping;
}

void FailingHttp2FrameDecoderListener::OnPingAck(const Http2FrameHeader& header,
                                                 const Http2PingFields& ping) {
  FAIL() << "OnPingAck: " << header << "; ping: " << ping;
}

void FailingHttp2FrameDecoderListener::OnGoAwayStart(
    const Http2FrameHeader& header,
    const Http2GoAwayFields& goaway) {
  FAIL() << "OnGoAwayStart: " << header << "; goaway: " << goaway;
}

void FailingHttp2FrameDecoderListener::OnGoAwayOpaqueData(const char* /*data*/,
                                                          size_t len) {
  FAIL() << "OnGoAwayOpaqueData: len=" << len;
}

void FailingHttp2FrameDecoderListener::OnGoAwayEnd() {
  FAIL() << "OnGoAwayEnd";
}

void FailingHttp2FrameDecoderListener::OnWindowUpdate(
    const Http2FrameHeader& header,
    uint32_t increment) {
  FAIL() << "OnWindowUpdate: " << header << "; increment=" << increment;
}

void FailingHttp2FrameDecoderListener::OnAltSvcStart(
    const Http2FrameHeader& header,
    size_t origin_length,
    size_t value_length) {
  FAIL() << "OnAltSvcStart: " << header << "; origin_length: " << origin_length
         << "; value_length: " << value_length;
}

void FailingHttp2FrameDecoderListener::OnAltSvcOriginData(const char* /*data*/,
                                                          size_t len) {
  FAIL() << "OnAltSvcOriginData: len=" << len;
}

void FailingHttp2FrameDecoderListener::OnAltSvcValueData(const char* /*data*/,
                                                         size_t len) {
  FAIL() << "OnAltSvcValueData: len=" << len;
}

void FailingHttp2FrameDecoderListener::OnAltSvcEnd() {
  FAIL() << "OnAltSvcEnd";
}

void FailingHttp2FrameDecoderListener::OnUnknownStart(
    const Http2FrameHeader& header) {
  FAIL() << "OnUnknownStart: " << header;
}

void FailingHttp2FrameDecoderListener::OnUnknownPayload(const char* /*data*/,
                                                        size_t len) {
  FAIL() << "OnUnknownPayload: len=" << len;
}

void FailingHttp2FrameDecoderListener::OnUnknownEnd() {
  FAIL() << "OnUnknownEnd";
}

void FailingHttp2FrameDecoderListener::OnPaddingTooLong(
    const Http2FrameHeader& header,
    size_t missing_length) {
  FAIL() << "OnPaddingTooLong: " << header
         << "; missing_length: " << missing_length;
}

void FailingHttp2FrameDecoderListener::OnFrameSizeError(
    const Http2FrameHeader& header) {
  FAIL() << "OnFrameSizeError: " << header;
}

LoggingHttp2FrameDecoderListener::LoggingHttp2FrameDecoderListener()
    : wrapped_(nullptr) {}
LoggingHttp2FrameDecoderListener::LoggingHttp2FrameDecoderListener(
    Http2FrameDecoderListener* wrapped)
    : wrapped_(wrapped) {}
LoggingHttp2FrameDecoderListener::~LoggingHttp2FrameDecoderListener() = default;

bool LoggingHttp2FrameDecoderListener::OnFrameHeader(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnFrameHeader: " << header;
  if (wrapped_ != nullptr) {
    return wrapped_->OnFrameHeader(header);
  }
  return true;
}

void LoggingHttp2FrameDecoderListener::OnDataStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnDataStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnDataStart(header);
  }
}

void LoggingHttp2FrameDecoderListener::OnDataPayload(const char* data,
                                                     size_t len) {
  HTTP2_VLOG(1) << "OnDataPayload: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnDataPayload(data, len);
  }
}

void LoggingHttp2FrameDecoderListener::OnDataEnd() {
  HTTP2_VLOG(1) << "OnDataEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnDataEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnHeadersStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnHeadersStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnHeadersStart(header);
  }
}

void LoggingHttp2FrameDecoderListener::OnHeadersPriority(
    const Http2PriorityFields& priority) {
  HTTP2_VLOG(1) << "OnHeadersPriority: " << priority;
  if (wrapped_ != nullptr) {
    wrapped_->OnHeadersPriority(priority);
  }
}

void LoggingHttp2FrameDecoderListener::OnHpackFragment(const char* data,
                                                       size_t len) {
  HTTP2_VLOG(1) << "OnHpackFragment: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnHpackFragment(data, len);
  }
}

void LoggingHttp2FrameDecoderListener::OnHeadersEnd() {
  HTTP2_VLOG(1) << "OnHeadersEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnHeadersEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnPriorityFrame(
    const Http2FrameHeader& header,
    const Http2PriorityFields& priority) {
  HTTP2_VLOG(1) << "OnPriorityFrame: " << header << "; priority: " << priority;
  if (wrapped_ != nullptr) {
    wrapped_->OnPriorityFrame(header, priority);
  }
}

void LoggingHttp2FrameDecoderListener::OnContinuationStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnContinuationStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnContinuationStart(header);
  }
}

void LoggingHttp2FrameDecoderListener::OnContinuationEnd() {
  HTTP2_VLOG(1) << "OnContinuationEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnContinuationEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnPadLength(size_t trailing_length) {
  HTTP2_VLOG(1) << "OnPadLength: trailing_length=" << trailing_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPadLength(trailing_length);
  }
}

void LoggingHttp2FrameDecoderListener::OnPadding(const char* padding,
                                                 size_t skipped_length) {
  HTTP2_VLOG(1) << "OnPadding: skipped_length=" << skipped_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPadding(padding, skipped_length);
  }
}

void LoggingHttp2FrameDecoderListener::OnRstStream(
    const Http2FrameHeader& header,
    Http2ErrorCode error_code) {
  HTTP2_VLOG(1) << "OnRstStream: " << header << "; code=" << error_code;
  if (wrapped_ != nullptr) {
    wrapped_->OnRstStream(header, error_code);
  }
}

void LoggingHttp2FrameDecoderListener::OnSettingsStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnSettingsStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnSettingsStart(header);
  }
}

void LoggingHttp2FrameDecoderListener::OnSetting(
    const Http2SettingFields& setting_fields) {
  HTTP2_VLOG(1) << "OnSetting: " << setting_fields;
  if (wrapped_ != nullptr) {
    wrapped_->OnSetting(setting_fields);
  }
}

void LoggingHttp2FrameDecoderListener::OnSettingsEnd() {
  HTTP2_VLOG(1) << "OnSettingsEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnSettingsEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnSettingsAck(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnSettingsAck: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnSettingsAck(header);
  }
}

void LoggingHttp2FrameDecoderListener::OnPushPromiseStart(
    const Http2FrameHeader& header,
    const Http2PushPromiseFields& promise,
    size_t total_padding_length) {
  HTTP2_VLOG(1) << "OnPushPromiseStart: " << header << "; promise: " << promise
                << "; total_padding_length: " << total_padding_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPushPromiseStart(header, promise, total_padding_length);
  }
}

void LoggingHttp2FrameDecoderListener::OnPushPromiseEnd() {
  HTTP2_VLOG(1) << "OnPushPromiseEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnPushPromiseEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnPing(const Http2FrameHeader& header,
                                              const Http2PingFields& ping) {
  HTTP2_VLOG(1) << "OnPing: " << header << "; ping: " << ping;
  if (wrapped_ != nullptr) {
    wrapped_->OnPing(header, ping);
  }
}

void LoggingHttp2FrameDecoderListener::OnPingAck(const Http2FrameHeader& header,
                                                 const Http2PingFields& ping) {
  HTTP2_VLOG(1) << "OnPingAck: " << header << "; ping: " << ping;
  if (wrapped_ != nullptr) {
    wrapped_->OnPingAck(header, ping);
  }
}

void LoggingHttp2FrameDecoderListener::OnGoAwayStart(
    const Http2FrameHeader& header,
    const Http2GoAwayFields& goaway) {
  HTTP2_VLOG(1) << "OnGoAwayStart: " << header << "; goaway: " << goaway;
  if (wrapped_ != nullptr) {
    wrapped_->OnGoAwayStart(header, goaway);
  }
}

void LoggingHttp2FrameDecoderListener::OnGoAwayOpaqueData(const char* data,
                                                          size_t len) {
  HTTP2_VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnGoAwayOpaqueData(data, len);
  }
}

void LoggingHttp2FrameDecoderListener::OnGoAwayEnd() {
  HTTP2_VLOG(1) << "OnGoAwayEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnGoAwayEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnWindowUpdate(
    const Http2FrameHeader& header,
    uint32_t increment) {
  HTTP2_VLOG(1) << "OnWindowUpdate: " << header << "; increment=" << increment;
  if (wrapped_ != nullptr) {
    wrapped_->OnWindowUpdate(header, increment);
  }
}

void LoggingHttp2FrameDecoderListener::OnAltSvcStart(
    const Http2FrameHeader& header,
    size_t origin_length,
    size_t value_length) {
  HTTP2_VLOG(1) << "OnAltSvcStart: " << header
                << "; origin_length: " << origin_length
                << "; value_length: " << value_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcStart(header, origin_length, value_length);
  }
}

void LoggingHttp2FrameDecoderListener::OnAltSvcOriginData(const char* data,
                                                          size_t len) {
  HTTP2_VLOG(1) << "OnAltSvcOriginData: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcOriginData(data, len);
  }
}

void LoggingHttp2FrameDecoderListener::OnAltSvcValueData(const char* data,
                                                         size_t len) {
  HTTP2_VLOG(1) << "OnAltSvcValueData: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcValueData(data, len);
  }
}

void LoggingHttp2FrameDecoderListener::OnAltSvcEnd() {
  HTTP2_VLOG(1) << "OnAltSvcEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnUnknownStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnUnknownStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnUnknownStart(header);
  }
}

void LoggingHttp2FrameDecoderListener::OnUnknownPayload(const char* data,
                                                        size_t len) {
  HTTP2_VLOG(1) << "OnUnknownPayload: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnUnknownPayload(data, len);
  }
}

void LoggingHttp2FrameDecoderListener::OnUnknownEnd() {
  HTTP2_VLOG(1) << "OnUnknownEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnUnknownEnd();
  }
}

void LoggingHttp2FrameDecoderListener::OnPaddingTooLong(
    const Http2FrameHeader& header,
    size_t missing_length) {
  HTTP2_VLOG(1) << "OnPaddingTooLong: " << header
                << "; missing_length: " << missing_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPaddingTooLong(header, missing_length);
  }
}

void LoggingHttp2FrameDecoderListener::OnFrameSizeError(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnFrameSizeError(header);
  }
}

}  // namespace http2
