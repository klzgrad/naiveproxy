// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/test_tools/quic_http_frame_parts_collector_listener.h"

#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

bool QuicHttpFramePartsCollectorListener::OnFrameHeader(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnFrameHeader: " << header;
  ExpectFrameHeader(header);
  return true;
}

void QuicHttpFramePartsCollectorListener::OnDataStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnDataStart: " << header;
  StartFrame(header)->OnDataStart(header);
}

void QuicHttpFramePartsCollectorListener::OnDataPayload(const char* data,
                                                        size_t len) {
  VLOG(1) << "OnDataPayload: len=" << len;
  CurrentFrame()->OnDataPayload(data, len);
}

void QuicHttpFramePartsCollectorListener::OnDataEnd() {
  VLOG(1) << "OnDataEnd";
  EndFrame()->OnDataEnd();
}

void QuicHttpFramePartsCollectorListener::OnHeadersStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnHeadersStart: " << header;
  StartFrame(header)->OnHeadersStart(header);
}

void QuicHttpFramePartsCollectorListener::OnHeadersPriority(
    const QuicHttpPriorityFields& priority) {
  VLOG(1) << "OnHeadersPriority: " << priority;
  CurrentFrame()->OnHeadersPriority(priority);
}

void QuicHttpFramePartsCollectorListener::OnHpackFragment(const char* data,
                                                          size_t len) {
  VLOG(1) << "OnHpackFragment: len=" << len;
  CurrentFrame()->OnHpackFragment(data, len);
}

void QuicHttpFramePartsCollectorListener::OnHeadersEnd() {
  VLOG(1) << "OnHeadersEnd";
  EndFrame()->OnHeadersEnd();
}

void QuicHttpFramePartsCollectorListener::OnPriorityFrame(
    const QuicHttpFrameHeader& header,
    const QuicHttpPriorityFields& priority_fields) {
  VLOG(1) << "OnPriority: " << header << "; " << priority_fields;
  StartAndEndFrame(header)->OnPriorityFrame(header, priority_fields);
}

void QuicHttpFramePartsCollectorListener::OnContinuationStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnContinuationStart: " << header;
  StartFrame(header)->OnContinuationStart(header);
}

void QuicHttpFramePartsCollectorListener::OnContinuationEnd() {
  VLOG(1) << "OnContinuationEnd";
  EndFrame()->OnContinuationEnd();
}

void QuicHttpFramePartsCollectorListener::OnPadLength(size_t pad_length) {
  VLOG(1) << "OnPadLength: " << pad_length;
  CurrentFrame()->OnPadLength(pad_length);
}

void QuicHttpFramePartsCollectorListener::OnPadding(const char* padding,
                                                    size_t skipped_length) {
  VLOG(1) << "OnPadding: " << skipped_length;
  CurrentFrame()->OnPadding(padding, skipped_length);
}

void QuicHttpFramePartsCollectorListener::OnRstStream(
    const QuicHttpFrameHeader& header,
    QuicHttpErrorCode error_code) {
  VLOG(1) << "OnRstStream: " << header << "; error_code=" << error_code;
  StartAndEndFrame(header)->OnRstStream(header, error_code);
}

void QuicHttpFramePartsCollectorListener::OnSettingsStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnSettingsStart: " << header;
  EXPECT_EQ(QuicHttpFrameType::SETTINGS, header.type) << header;
  EXPECT_EQ(QuicHttpFrameFlag(), header.flags) << header;
  StartFrame(header)->OnSettingsStart(header);
}

void QuicHttpFramePartsCollectorListener::OnSetting(
    const QuicHttpSettingFields& setting_fields) {
  VLOG(1) << "QuicHttpSettingFields: setting_fields=" << setting_fields;
  CurrentFrame()->OnSetting(setting_fields);
}

void QuicHttpFramePartsCollectorListener::OnSettingsEnd() {
  VLOG(1) << "OnSettingsEnd";
  EndFrame()->OnSettingsEnd();
}

void QuicHttpFramePartsCollectorListener::OnSettingsAck(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnSettingsAck: " << header;
  StartAndEndFrame(header)->OnSettingsAck(header);
}

void QuicHttpFramePartsCollectorListener::OnPushPromiseStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpPushPromiseFields& promise,
    size_t total_padding_length) {
  VLOG(1) << "OnPushPromiseStart header: " << header << "  promise: " << promise
          << "  total_padding_length: " << total_padding_length;
  EXPECT_EQ(QuicHttpFrameType::PUSH_PROMISE, header.type);
  StartFrame(header)->OnPushPromiseStart(header, promise, total_padding_length);
}

void QuicHttpFramePartsCollectorListener::OnPushPromiseEnd() {
  VLOG(1) << "OnPushPromiseEnd";
  EndFrame()->OnPushPromiseEnd();
}

void QuicHttpFramePartsCollectorListener::OnPing(
    const QuicHttpFrameHeader& header,
    const QuicHttpPingFields& ping) {
  VLOG(1) << "OnPing: " << header << "; " << ping;
  StartAndEndFrame(header)->OnPing(header, ping);
}

void QuicHttpFramePartsCollectorListener::OnPingAck(
    const QuicHttpFrameHeader& header,
    const QuicHttpPingFields& ping) {
  VLOG(1) << "OnPingAck: " << header << "; " << ping;
  StartAndEndFrame(header)->OnPingAck(header, ping);
}

void QuicHttpFramePartsCollectorListener::OnGoAwayStart(
    const QuicHttpFrameHeader& header,
    const QuicHttpGoAwayFields& goaway) {
  VLOG(1) << "OnGoAwayStart header: " << header << "; goaway: " << goaway;
  StartFrame(header)->OnGoAwayStart(header, goaway);
}

void QuicHttpFramePartsCollectorListener::OnGoAwayOpaqueData(const char* data,
                                                             size_t len) {
  VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  CurrentFrame()->OnGoAwayOpaqueData(data, len);
}

void QuicHttpFramePartsCollectorListener::OnGoAwayEnd() {
  VLOG(1) << "OnGoAwayEnd";
  EndFrame()->OnGoAwayEnd();
}

void QuicHttpFramePartsCollectorListener::OnWindowUpdate(
    const QuicHttpFrameHeader& header,
    uint32_t window_size_increment) {
  VLOG(1) << "OnWindowUpdate: " << header
          << "; window_size_increment=" << window_size_increment;
  EXPECT_EQ(QuicHttpFrameType::WINDOW_UPDATE, header.type);
  StartAndEndFrame(header)->OnWindowUpdate(header, window_size_increment);
}

void QuicHttpFramePartsCollectorListener::OnAltSvcStart(
    const QuicHttpFrameHeader& header,
    size_t origin_length,
    size_t value_length) {
  VLOG(1) << "OnAltSvcStart header: " << header
          << "; origin_length=" << origin_length
          << "; value_length=" << value_length;
  StartFrame(header)->OnAltSvcStart(header, origin_length, value_length);
}

void QuicHttpFramePartsCollectorListener::OnAltSvcOriginData(const char* data,
                                                             size_t len) {
  VLOG(1) << "OnAltSvcOriginData: len=" << len;
  CurrentFrame()->OnAltSvcOriginData(data, len);
}

void QuicHttpFramePartsCollectorListener::OnAltSvcValueData(const char* data,
                                                            size_t len) {
  VLOG(1) << "OnAltSvcValueData: len=" << len;
  CurrentFrame()->OnAltSvcValueData(data, len);
}

void QuicHttpFramePartsCollectorListener::OnAltSvcEnd() {
  VLOG(1) << "OnAltSvcEnd";
  EndFrame()->OnAltSvcEnd();
}

void QuicHttpFramePartsCollectorListener::OnUnknownStart(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnUnknownStart: " << header;
  StartFrame(header)->OnUnknownStart(header);
}

void QuicHttpFramePartsCollectorListener::OnUnknownPayload(const char* data,
                                                           size_t len) {
  VLOG(1) << "OnUnknownPayload: len=" << len;
  CurrentFrame()->OnUnknownPayload(data, len);
}

void QuicHttpFramePartsCollectorListener::OnUnknownEnd() {
  VLOG(1) << "OnUnknownEnd";
  EndFrame()->OnUnknownEnd();
}

void QuicHttpFramePartsCollectorListener::OnPaddingTooLong(
    const QuicHttpFrameHeader& header,
    size_t missing_length) {
  VLOG(1) << "OnPaddingTooLong: " << header
          << "    missing_length: " << missing_length;
  EndFrame()->OnPaddingTooLong(header, missing_length);
}

void QuicHttpFramePartsCollectorListener::OnFrameSizeError(
    const QuicHttpFrameHeader& header) {
  VLOG(1) << "OnFrameSizeError: " << header;
  FrameError(header)->OnFrameSizeError(header);
}

}  // namespace test
}  // namespace net
