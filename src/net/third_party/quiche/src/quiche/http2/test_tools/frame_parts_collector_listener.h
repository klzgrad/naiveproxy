// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TEST_TOOLS_FRAME_PARTS_COLLECTOR_LISTENER_H_
#define QUICHE_HTTP2_TEST_TOOLS_FRAME_PARTS_COLLECTOR_LISTENER_H_

// FramePartsCollectorListener extends FramePartsCollector with an
// implementation of every method of Http2FrameDecoderListener; it is
// essentially the union of all the Listener classes in the tests of the
// payload decoders (i.e. in ./payload_decoders/*_test.cc files), with the
// addition of the OnFrameHeader method.
// FramePartsCollectorListener supports tests of Http2FrameDecoder.

#include <stddef.h>

#include <cstdint>

#include "quiche/http2/core/http2_constants.h"
#include "quiche/http2/core/http2_structures.h"
#include "quiche/http2/decoder/http2_frame_decoder_listener.h"
#include "quiche/http2/test_tools/frame_parts_collector.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {

class QUICHE_NO_EXPORT FramePartsCollectorListener
    : public FramePartsCollector {
 public:
  FramePartsCollectorListener() {}
  ~FramePartsCollectorListener() override {}

  // TODO(jamessynge): Remove OnFrameHeader once done with supporting
  // SpdyFramer's exact states.
  bool OnFrameHeader(const Http2FrameHeader& header) override;
  void OnDataStart(const Http2FrameHeader& header) override;
  void OnDataPayload(const char* data, size_t len) override;
  void OnDataEnd() override;
  void OnHeadersStart(const Http2FrameHeader& header) override;
  void OnHeadersPriority(const Http2PriorityFields& priority) override;
  void OnHpackFragment(const char* data, size_t len) override;
  void OnHeadersEnd() override;
  void OnPriorityFrame(const Http2FrameHeader& header,
                       const Http2PriorityFields& priority_fields) override;
  void OnContinuationStart(const Http2FrameHeader& header) override;
  void OnContinuationEnd() override;
  void OnPadLength(size_t pad_length) override;
  void OnPadding(const char* padding, size_t skipped_length) override;
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
                      uint32_t window_size_increment) override;
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
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TEST_TOOLS_FRAME_PARTS_COLLECTOR_LISTENER_H_
