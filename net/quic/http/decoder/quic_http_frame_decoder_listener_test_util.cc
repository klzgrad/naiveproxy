// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_frame_decoder_listener_test_util.h"

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

FailingQuicHttpFrameDecoderListener::FailingQuicHttpFrameDecoderListener() {}
FailingQuicHttpFrameDecoderListener::~FailingQuicHttpFrameDecoderListener() {}

bool FailingQuicHttpFrameDecoderListener::OnFrameHeader(
    const QuicHttpFrameHeader& header) {
  ADD_FAILURE() << "OnFrameHeader: " << header;
  return false;
}

void FailingQuicHttpFrameDecoderListener::OnDataStart(
    const QuicHttpFrameHeader& header) {
  FAIL() << "OnDataStart: " << header;
}

void FailingQuicHttpFrameDecoderListener::OnDataPayload(const char* data,
                                                        size_t len) {
  FAIL() << "OnDataPayload: len=" << len;
}

void FailingQuicHttpFrameDecoderListener::OnDataEnd() {
  FAIL() << "OnDataEnd";
}

void FailingQuicHttpFrameDecoderListener::OnHeadersStart(
    const QuicHttpFrameHeader& header) {
  FAIL() << "OnHeadersStart: " << header;
}

void FailingQuicHttpFrameDecoderListener::OnHeadersPriority(
    const QuicHttpPriorityFields& priority) {
  FAIL() << "OnHeadersPriority: " << priority;
}

void FailingQuicHttpFrameDecoderListener::OnHpackFragment(const char* data,
                                                          size_t len) {
  FAIL() << "OnHpackFragment: len=" << len;
}

void FailingQuicHttpFrameDecoderListener::OnHeadersEnd() {
  FAIL() << "OnHeadersEnd";
}

void FailingQuicHttpFrameDecoderListener::OnPriorityFrame(
    const QuicHttpFrameHeader& header,
    const QuicHttpPriorityFields& priority) {
  FAIL() << "OnPriorityFrame: " << header << "; priority: " << priority;
}

void FailingQuicHttpFrameDecoderListener::OnContinuationStart(
    const QuicHttpFrameHeader& header) {
  FAIL() << "OnContinuationStart: " << header;
}

void FailingQuicHttpFrameDecoderListener::OnContinuationEnd() {
  FAIL() << "OnContinuationEnd";
}

void FailingQuicHttpFrameDecoderListener::OnPadLength(size_t trailing_length) {
  FAIL() << "OnPadLength: trailing_length=" << trailing_length;
}

void FailingQuicHttpFrameDecoderListener::OnPadding(const char* padding,
                                                    size_t skipped_length) {
  FAIL() << "OnPadding: skipped_length=" << skipped_length;
}

void FailingQuicHttpFrameDecoderListener::OnRstStream(
    const QuicHttpFrameHeader& header,
    QuicHttpErrorCode error_code) {
  FAIL() << "OnRstStream: " << header << "; code=" << error_code;
}

void FailingQuicHttpFrameDecoderListener::OnSettingsStart(
    const QuicHttpFrameHeader& header) {
  FAIL() << "OnSettingsStart: " << header;
}

void FailingQuicHttpFrameDecoderListener::OnSetting(
    const QuicHttpSettingFields& setting_fields) {
  FAIL() << "OnSetting: " << setting_fields;
}

void FailingQuicHttpFrameDecoderListener::OnSettingsEnd() {
  FAIL() << "OnSettingsEnd";
}

void FailingQuicHttpFrameDecoderListener::OnSettingsAck(
    const QuicHttpFrameHeader& header) {
  FAIL() << "OnSettingsAck: " << header;
}

void FailingQuicHttpFrameDecoderListener::OnPushPromiseStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpPushPromiseFields& promise,
    size_t total_padding_length) {
  FAIL() << "OnPushPromiseStart: " << header << "; promise: " << promise
         << "; total_padding_length: " << total_padding_length;
}

void FailingQuicHttpFrameDecoderListener::OnPushPromiseEnd() {
  FAIL() << "OnPushPromiseEnd";
}

void FailingQuicHttpFrameDecoderListener::OnPing(
    const QuicHttpFrameHeader& header,
    const QuicHttpPingFields& ping) {
  FAIL() << "OnPing: " << header << "; ping: " << ping;
}

void FailingQuicHttpFrameDecoderListener::OnPingAck(
    const QuicHttpFrameHeader& header,
    const QuicHttpPingFields& ping) {
  FAIL() << "OnPingAck: " << header << "; ping: " << ping;
}

void FailingQuicHttpFrameDecoderListener::OnGoAwayStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpGoAwayFields& goaway) {
  FAIL() << "OnGoAwayStart: " << header << "; goaway: " << goaway;
}

void FailingQuicHttpFrameDecoderListener::OnGoAwayOpaqueData(const char* data,
                                                             size_t len) {
  FAIL() << "OnGoAwayOpaqueData: len=" << len;
}

void FailingQuicHttpFrameDecoderListener::OnGoAwayEnd() {
  FAIL() << "OnGoAwayEnd";
}

void FailingQuicHttpFrameDecoderListener::OnWindowUpdate(
    const QuicHttpFrameHeader& header,
    uint32_t increment) {
  FAIL() << "OnWindowUpdate: " << header << "; increment=" << increment;
}

void FailingQuicHttpFrameDecoderListener::OnAltSvcStart(
    const QuicHttpFrameHeader& header,
    size_t origin_length,
    size_t value_length) {
  FAIL() << "OnAltSvcStart: " << header << "; origin_length: " << origin_length
         << "; value_length: " << value_length;
}

void FailingQuicHttpFrameDecoderListener::OnAltSvcOriginData(const char* data,
                                                             size_t len) {
  FAIL() << "OnAltSvcOriginData: len=" << len;
}

void FailingQuicHttpFrameDecoderListener::OnAltSvcValueData(const char* data,
                                                            size_t len) {
  FAIL() << "OnAltSvcValueData: len=" << len;
}

void FailingQuicHttpFrameDecoderListener::OnAltSvcEnd() {
  FAIL() << "OnAltSvcEnd";
}

void FailingQuicHttpFrameDecoderListener::OnUnknownStart(
    const QuicHttpFrameHeader& header) {
  FAIL() << "OnUnknownStart: " << header;
}

void FailingQuicHttpFrameDecoderListener::OnUnknownPayload(const char* data,
                                                           size_t len) {
  FAIL() << "OnUnknownPayload: len=" << len;
}

void FailingQuicHttpFrameDecoderListener::OnUnknownEnd() {
  FAIL() << "OnUnknownEnd";
}

void FailingQuicHttpFrameDecoderListener::OnPaddingTooLong(
    const QuicHttpFrameHeader& header,
    size_t missing_length) {
  FAIL() << "OnPaddingTooLong: " << header
         << "; missing_length: " << missing_length;
}

void FailingQuicHttpFrameDecoderListener::OnFrameSizeError(
    const QuicHttpFrameHeader& header) {
  FAIL() << "OnFrameSizeError: " << header;
}

LoggingQuicHttpFrameDecoderListener::LoggingQuicHttpFrameDecoderListener()
    : wrapped_(nullptr) {}
LoggingQuicHttpFrameDecoderListener::LoggingQuicHttpFrameDecoderListener(
    QuicHttpFrameDecoderListener* wrapped)
    : wrapped_(wrapped) {}
LoggingQuicHttpFrameDecoderListener::~LoggingQuicHttpFrameDecoderListener() {}

bool LoggingQuicHttpFrameDecoderListener::OnFrameHeader(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnFrameHeader: " << header;
  if (wrapped_ != nullptr) {
    return wrapped_->OnFrameHeader(header);
  }
  return true;
}

void LoggingQuicHttpFrameDecoderListener::OnDataStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnDataStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnDataStart(header);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnDataPayload(const char* data,
                                                        size_t len) {
  VLOG(1) << "OnDataPayload: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnDataPayload(data, len);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnDataEnd() {
  VLOG(1) << "OnDataEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnDataEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnHeadersStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnHeadersStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnHeadersStart(header);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnHeadersPriority(
    const QuicHttpPriorityFields& priority) {
  VLOG(1) << "OnHeadersPriority: " << priority;
  if (wrapped_ != nullptr) {
    wrapped_->OnHeadersPriority(priority);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnHpackFragment(const char* data,
                                                          size_t len) {
  VLOG(1) << "OnHpackFragment: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnHpackFragment(data, len);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnHeadersEnd() {
  VLOG(1) << "OnHeadersEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnHeadersEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPriorityFrame(
    const QuicHttpFrameHeader& header,
    const QuicHttpPriorityFields& priority) {
  VLOG(1) << "OnPriorityFrame: " << header << "; priority: " << priority;
  if (wrapped_ != nullptr) {
    wrapped_->OnPriorityFrame(header, priority);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnContinuationStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnContinuationStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnContinuationStart(header);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnContinuationEnd() {
  VLOG(1) << "OnContinuationEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnContinuationEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPadLength(size_t trailing_length) {
  VLOG(1) << "OnPadLength: trailing_length=" << trailing_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPadLength(trailing_length);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPadding(const char* padding,
                                                    size_t skipped_length) {
  VLOG(1) << "OnPadding: skipped_length=" << skipped_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPadding(padding, skipped_length);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnRstStream(
    const QuicHttpFrameHeader& header,
    QuicHttpErrorCode error_code) {
  VLOG(1) << "OnRstStream: " << header << "; code=" << error_code;
  if (wrapped_ != nullptr) {
    wrapped_->OnRstStream(header, error_code);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnSettingsStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnSettingsStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnSettingsStart(header);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnSetting(
    const QuicHttpSettingFields& setting_fields) {
  VLOG(1) << "OnSetting: " << setting_fields;
  if (wrapped_ != nullptr) {
    wrapped_->OnSetting(setting_fields);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnSettingsEnd() {
  VLOG(1) << "OnSettingsEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnSettingsEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnSettingsAck(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnSettingsAck: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnSettingsAck(header);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPushPromiseStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpPushPromiseFields& promise,
    size_t total_padding_length) {
  VLOG(1) << "OnPushPromiseStart: " << header << "; promise: " << promise
          << "; total_padding_length: " << total_padding_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPushPromiseStart(header, promise, total_padding_length);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPushPromiseEnd() {
  VLOG(1) << "OnPushPromiseEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnPushPromiseEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPing(
    const QuicHttpFrameHeader& header,
    const QuicHttpPingFields& ping) {
  VLOG(1) << "OnPing: " << header << "; ping: " << ping;
  if (wrapped_ != nullptr) {
    wrapped_->OnPing(header, ping);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPingAck(
    const QuicHttpFrameHeader& header,
    const QuicHttpPingFields& ping) {
  VLOG(1) << "OnPingAck: " << header << "; ping: " << ping;
  if (wrapped_ != nullptr) {
    wrapped_->OnPingAck(header, ping);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnGoAwayStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpGoAwayFields& goaway) {
  VLOG(1) << "OnGoAwayStart: " << header << "; goaway: " << goaway;
  if (wrapped_ != nullptr) {
    wrapped_->OnGoAwayStart(header, goaway);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnGoAwayOpaqueData(const char* data,
                                                             size_t len) {
  VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnGoAwayOpaqueData(data, len);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnGoAwayEnd() {
  VLOG(1) << "OnGoAwayEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnGoAwayEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnWindowUpdate(
    const QuicHttpFrameHeader& header,
    uint32_t increment) {
  VLOG(1) << "OnWindowUpdate: " << header << "; increment=" << increment;
  if (wrapped_ != nullptr) {
    wrapped_->OnWindowUpdate(header, increment);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnAltSvcStart(
    const QuicHttpFrameHeader& header,
    size_t origin_length,
    size_t value_length) {
  VLOG(1) << "OnAltSvcStart: " << header << "; origin_length: " << origin_length
          << "; value_length: " << value_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcStart(header, origin_length, value_length);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnAltSvcOriginData(const char* data,
                                                             size_t len) {
  VLOG(1) << "OnAltSvcOriginData: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcOriginData(data, len);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnAltSvcValueData(const char* data,
                                                            size_t len) {
  VLOG(1) << "OnAltSvcValueData: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcValueData(data, len);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnAltSvcEnd() {
  VLOG(1) << "OnAltSvcEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnAltSvcEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnUnknownStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnUnknownStart: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnUnknownStart(header);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnUnknownPayload(const char* data,
                                                           size_t len) {
  VLOG(1) << "OnUnknownPayload: len=" << len;
  if (wrapped_ != nullptr) {
    wrapped_->OnUnknownPayload(data, len);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnUnknownEnd() {
  VLOG(1) << "OnUnknownEnd";
  if (wrapped_ != nullptr) {
    wrapped_->OnUnknownEnd();
  }
}

void LoggingQuicHttpFrameDecoderListener::OnPaddingTooLong(
    const QuicHttpFrameHeader& header,
    size_t missing_length) {
  VLOG(1) << "OnPaddingTooLong: " << header
          << "; missing_length: " << missing_length;
  if (wrapped_ != nullptr) {
    wrapped_->OnPaddingTooLong(header, missing_length);
  }
}

void LoggingQuicHttpFrameDecoderListener::OnFrameSizeError(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnFrameSizeError: " << header;
  if (wrapped_ != nullptr) {
    wrapped_->OnFrameSizeError(header);
  }
}

}  // namespace net
