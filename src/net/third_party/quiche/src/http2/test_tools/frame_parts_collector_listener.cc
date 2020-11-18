// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/test_tools/frame_parts_collector_listener.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {
namespace test {

bool FramePartsCollectorListener::OnFrameHeader(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnFrameHeader: " << header;
  ExpectFrameHeader(header);
  return true;
}

void FramePartsCollectorListener::OnDataStart(const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnDataStart: " << header;
  StartFrame(header)->OnDataStart(header);
}

void FramePartsCollectorListener::OnDataPayload(const char* data, size_t len) {
  HTTP2_VLOG(1) << "OnDataPayload: len=" << len;
  CurrentFrame()->OnDataPayload(data, len);
}

void FramePartsCollectorListener::OnDataEnd() {
  HTTP2_VLOG(1) << "OnDataEnd";
  EndFrame()->OnDataEnd();
}

void FramePartsCollectorListener::OnHeadersStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnHeadersStart: " << header;
  StartFrame(header)->OnHeadersStart(header);
}

void FramePartsCollectorListener::OnHeadersPriority(
    const Http2PriorityFields& priority) {
  HTTP2_VLOG(1) << "OnHeadersPriority: " << priority;
  CurrentFrame()->OnHeadersPriority(priority);
}

void FramePartsCollectorListener::OnHpackFragment(const char* data,
                                                  size_t len) {
  HTTP2_VLOG(1) << "OnHpackFragment: len=" << len;
  CurrentFrame()->OnHpackFragment(data, len);
}

void FramePartsCollectorListener::OnHeadersEnd() {
  HTTP2_VLOG(1) << "OnHeadersEnd";
  EndFrame()->OnHeadersEnd();
}

void FramePartsCollectorListener::OnPriorityFrame(
    const Http2FrameHeader& header,
    const Http2PriorityFields& priority_fields) {
  HTTP2_VLOG(1) << "OnPriority: " << header << "; " << priority_fields;
  StartAndEndFrame(header)->OnPriorityFrame(header, priority_fields);
}

void FramePartsCollectorListener::OnContinuationStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnContinuationStart: " << header;
  StartFrame(header)->OnContinuationStart(header);
}

void FramePartsCollectorListener::OnContinuationEnd() {
  HTTP2_VLOG(1) << "OnContinuationEnd";
  EndFrame()->OnContinuationEnd();
}

void FramePartsCollectorListener::OnPadLength(size_t pad_length) {
  HTTP2_VLOG(1) << "OnPadLength: " << pad_length;
  CurrentFrame()->OnPadLength(pad_length);
}

void FramePartsCollectorListener::OnPadding(const char* padding,
                                            size_t skipped_length) {
  HTTP2_VLOG(1) << "OnPadding: " << skipped_length;
  CurrentFrame()->OnPadding(padding, skipped_length);
}

void FramePartsCollectorListener::OnRstStream(const Http2FrameHeader& header,
                                              Http2ErrorCode error_code) {
  HTTP2_VLOG(1) << "OnRstStream: " << header << "; error_code=" << error_code;
  StartAndEndFrame(header)->OnRstStream(header, error_code);
}

void FramePartsCollectorListener::OnSettingsStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnSettingsStart: " << header;
  EXPECT_EQ(Http2FrameType::SETTINGS, header.type) << header;
  EXPECT_EQ(Http2FrameFlag(), header.flags) << header;
  StartFrame(header)->OnSettingsStart(header);
}

void FramePartsCollectorListener::OnSetting(
    const Http2SettingFields& setting_fields) {
  HTTP2_VLOG(1) << "Http2SettingFields: setting_fields=" << setting_fields;
  CurrentFrame()->OnSetting(setting_fields);
}

void FramePartsCollectorListener::OnSettingsEnd() {
  HTTP2_VLOG(1) << "OnSettingsEnd";
  EndFrame()->OnSettingsEnd();
}

void FramePartsCollectorListener::OnSettingsAck(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnSettingsAck: " << header;
  StartAndEndFrame(header)->OnSettingsAck(header);
}

void FramePartsCollectorListener::OnPushPromiseStart(
    const Http2FrameHeader& header,
    const Http2PushPromiseFields& promise,
    size_t total_padding_length) {
  HTTP2_VLOG(1) << "OnPushPromiseStart header: " << header
                << "  promise: " << promise
                << "  total_padding_length: " << total_padding_length;
  EXPECT_EQ(Http2FrameType::PUSH_PROMISE, header.type);
  StartFrame(header)->OnPushPromiseStart(header, promise, total_padding_length);
}

void FramePartsCollectorListener::OnPushPromiseEnd() {
  HTTP2_VLOG(1) << "OnPushPromiseEnd";
  EndFrame()->OnPushPromiseEnd();
}

void FramePartsCollectorListener::OnPing(const Http2FrameHeader& header,
                                         const Http2PingFields& ping) {
  HTTP2_VLOG(1) << "OnPing: " << header << "; " << ping;
  StartAndEndFrame(header)->OnPing(header, ping);
}

void FramePartsCollectorListener::OnPingAck(const Http2FrameHeader& header,
                                            const Http2PingFields& ping) {
  HTTP2_VLOG(1) << "OnPingAck: " << header << "; " << ping;
  StartAndEndFrame(header)->OnPingAck(header, ping);
}

void FramePartsCollectorListener::OnGoAwayStart(
    const Http2FrameHeader& header,
    const Http2GoAwayFields& goaway) {
  HTTP2_VLOG(1) << "OnGoAwayStart header: " << header << "; goaway: " << goaway;
  StartFrame(header)->OnGoAwayStart(header, goaway);
}

void FramePartsCollectorListener::OnGoAwayOpaqueData(const char* data,
                                                     size_t len) {
  HTTP2_VLOG(1) << "OnGoAwayOpaqueData: len=" << len;
  CurrentFrame()->OnGoAwayOpaqueData(data, len);
}

void FramePartsCollectorListener::OnGoAwayEnd() {
  HTTP2_VLOG(1) << "OnGoAwayEnd";
  EndFrame()->OnGoAwayEnd();
}

void FramePartsCollectorListener::OnWindowUpdate(
    const Http2FrameHeader& header,
    uint32_t window_size_increment) {
  HTTP2_VLOG(1) << "OnWindowUpdate: " << header
                << "; window_size_increment=" << window_size_increment;
  EXPECT_EQ(Http2FrameType::WINDOW_UPDATE, header.type);
  StartAndEndFrame(header)->OnWindowUpdate(header, window_size_increment);
}

void FramePartsCollectorListener::OnAltSvcStart(const Http2FrameHeader& header,
                                                size_t origin_length,
                                                size_t value_length) {
  HTTP2_VLOG(1) << "OnAltSvcStart header: " << header
                << "; origin_length=" << origin_length
                << "; value_length=" << value_length;
  StartFrame(header)->OnAltSvcStart(header, origin_length, value_length);
}

void FramePartsCollectorListener::OnAltSvcOriginData(const char* data,
                                                     size_t len) {
  HTTP2_VLOG(1) << "OnAltSvcOriginData: len=" << len;
  CurrentFrame()->OnAltSvcOriginData(data, len);
}

void FramePartsCollectorListener::OnAltSvcValueData(const char* data,
                                                    size_t len) {
  HTTP2_VLOG(1) << "OnAltSvcValueData: len=" << len;
  CurrentFrame()->OnAltSvcValueData(data, len);
}

void FramePartsCollectorListener::OnAltSvcEnd() {
  HTTP2_VLOG(1) << "OnAltSvcEnd";
  EndFrame()->OnAltSvcEnd();
}

void FramePartsCollectorListener::OnUnknownStart(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnUnknownStart: " << header;
  StartFrame(header)->OnUnknownStart(header);
}

void FramePartsCollectorListener::OnUnknownPayload(const char* data,
                                                   size_t len) {
  HTTP2_VLOG(1) << "OnUnknownPayload: len=" << len;
  CurrentFrame()->OnUnknownPayload(data, len);
}

void FramePartsCollectorListener::OnUnknownEnd() {
  HTTP2_VLOG(1) << "OnUnknownEnd";
  EndFrame()->OnUnknownEnd();
}

void FramePartsCollectorListener::OnPaddingTooLong(
    const Http2FrameHeader& header,
    size_t missing_length) {
  HTTP2_VLOG(1) << "OnPaddingTooLong: " << header
                << "    missing_length: " << missing_length;
  EndFrame()->OnPaddingTooLong(header, missing_length);
}

void FramePartsCollectorListener::OnFrameSizeError(
    const Http2FrameHeader& header) {
  HTTP2_VLOG(1) << "OnFrameSizeError: " << header;
  FrameError(header)->OnFrameSizeError(header);
}

}  // namespace test
}  // namespace http2
