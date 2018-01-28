// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_BUFFERED_SPDY_FRAMER_H_
#define NET_SPDY_CHROMIUM_BUFFERED_SPDY_FRAMER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/log/net_log_source.h"
#include "net/spdy/chromium/header_coalescer.h"
#include "net/spdy/core/http2_frame_decoder_adapter.h"
#include "net/spdy/core/spdy_alt_svc_wire_format.h"
#include "net/spdy/core/spdy_framer.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/core/spdy_protocol.h"
#include "net/spdy/platform/api/spdy_string.h"
#include "net/spdy/platform/api/spdy_string_piece.h"

namespace net {

class NET_EXPORT_PRIVATE BufferedSpdyFramerVisitorInterface {
 public:
  BufferedSpdyFramerVisitorInterface() {}

  // Called if an error is detected in the SpdySerializedFrame protocol.
  virtual void OnError(
      Http2DecoderAdapter::SpdyFramerError spdy_framer_error) = 0;

  // Called if an error is detected in a HTTP2 stream.
  virtual void OnStreamError(SpdyStreamId stream_id,
                             const SpdyString& description) = 0;

  // Called after all the header data for HEADERS control frame is received.
  virtual void OnHeaders(SpdyStreamId stream_id,
                         bool has_priority,
                         int weight,
                         SpdyStreamId parent_stream_id,
                         bool exclusive,
                         bool fin,
                         SpdyHeaderBlock headers) = 0;

  // Called when a data frame header is received.
  virtual void OnDataFrameHeader(SpdyStreamId stream_id,
                                 size_t length,
                                 bool fin) = 0;

  // Called when data is received.
  // |stream_id| The stream receiving data.
  // |data| A buffer containing the data received.
  // |len| The length of the data buffer (at most 2^16 - 1 - 8).
  virtual void OnStreamFrameData(SpdyStreamId stream_id,
                                 const char* data,
                                 size_t len) = 0;

  // Called when the other side has finished sending data on this stream.
  // |stream_id| The stream that was receivin data.
  virtual void OnStreamEnd(SpdyStreamId stream_id) = 0;

  // Called when padding is received (padding length field or padding octets).
  // |stream_id| The stream receiving data.
  // |len| The number of padding octets.
  virtual void OnStreamPadding(SpdyStreamId stream_id, size_t len) = 0;

  // Called when a SETTINGS frame is received.
  virtual void OnSettings() = 0;

  // Called when an individual setting within a SETTINGS frame has been parsed
  // and validated.
  virtual void OnSetting(SpdySettingsIds id, uint32_t value) = 0;

  // Called when a SETTINGS frame is received with the ACK flag set.
  virtual void OnSettingsAck() = 0;

  // Called at the completion of parsing SETTINGS id and value tuples.
  virtual void OnSettingsEnd() = 0;

  // Called when a PING frame has been parsed.
  virtual void OnPing(SpdyPingId unique_id, bool is_ack) = 0;

  // Called when a RST_STREAM frame has been parsed.
  virtual void OnRstStream(SpdyStreamId stream_id,
                           SpdyErrorCode error_code) = 0;

  // Called when a GOAWAY frame has been parsed.
  virtual void OnGoAway(SpdyStreamId last_accepted_stream_id,
                        SpdyErrorCode error_code,
                        SpdyStringPiece debug_data) = 0;

  // Called when a WINDOW_UPDATE frame has been parsed.
  virtual void OnWindowUpdate(SpdyStreamId stream_id,
                              int delta_window_size) = 0;

  // Called when a PUSH_PROMISE frame has been parsed.
  virtual void OnPushPromise(SpdyStreamId stream_id,
                             SpdyStreamId promised_stream_id,
                             SpdyHeaderBlock headers) = 0;

  // Called when an ALTSVC frame has been parsed.
  virtual void OnAltSvc(
      SpdyStreamId stream_id,
      SpdyStringPiece origin,
      const SpdyAltSvcWireFormat::AlternativeServiceVector& altsvc_vector) = 0;

  // Called when a frame type we don't recognize is received.
  // Return true if this appears to be a valid extension frame, false otherwise.
  // We distinguish between extension frames and nonsense by checking
  // whether the stream id is valid.
  virtual bool OnUnknownFrame(SpdyStreamId stream_id, uint8_t frame_type) = 0;

 protected:
  virtual ~BufferedSpdyFramerVisitorInterface() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(BufferedSpdyFramerVisitorInterface);
};

class NET_EXPORT_PRIVATE BufferedSpdyFramer
    : public SpdyFramerVisitorInterface {
 public:
  BufferedSpdyFramer(uint32_t max_header_list_size,
                     const NetLogWithSource& net_log);
  BufferedSpdyFramer() = delete;
  ~BufferedSpdyFramer() override;

  // Sets callbacks to be called from the buffered spdy framer.  A visitor must
  // be set, or else the framer will likely crash.  It is acceptable for the
  // visitor to do nothing.  If this is called multiple times, only the last
  // visitor will be used.
  void set_visitor(BufferedSpdyFramerVisitorInterface* visitor);

  // Set debug callbacks to be called from the framer. The debug visitor is
  // completely optional and need not be set in order for normal operation.
  // If this is called multiple times, only the last visitor will be used.
  void set_debug_visitor(SpdyFramerDebugVisitorInterface* debug_visitor);

  // SpdyFramerVisitorInterface
  void OnError(Http2DecoderAdapter::SpdyFramerError spdy_framer_error) override;
  void OnHeaders(SpdyStreamId stream_id,
                 bool has_priority,
                 int weight,
                 SpdyStreamId parent_stream_id,
                 bool exclusive,
                 bool fin,
                 bool end) override;
  void OnStreamFrameData(SpdyStreamId stream_id,
                         const char* data,
                         size_t len) override;
  void OnStreamEnd(SpdyStreamId stream_id) override;
  void OnStreamPadding(SpdyStreamId stream_id, size_t len) override;
  SpdyHeadersHandlerInterface* OnHeaderFrameStart(
      SpdyStreamId stream_id) override;
  void OnHeaderFrameEnd(SpdyStreamId stream_id) override;
  void OnSettings() override;
  void OnSetting(SpdySettingsIds id, uint32_t value) override;
  void OnSettingsAck() override;
  void OnSettingsEnd() override;
  void OnPing(SpdyPingId unique_id, bool is_ack) override;
  void OnRstStream(SpdyStreamId stream_id, SpdyErrorCode error_code) override;
  void OnGoAway(SpdyStreamId last_accepted_stream_id,
                SpdyErrorCode error_code) override;
  bool OnGoAwayFrameData(const char* goaway_data, size_t len) override;
  void OnWindowUpdate(SpdyStreamId stream_id, int delta_window_size) override;
  void OnPushPromise(SpdyStreamId stream_id,
                     SpdyStreamId promised_stream_id,
                     bool end) override;
  void OnAltSvc(SpdyStreamId stream_id,
                SpdyStringPiece origin,
                const SpdyAltSvcWireFormat::AlternativeServiceVector&
                    altsvc_vector) override;
  void OnDataFrameHeader(SpdyStreamId stream_id,
                         size_t length,
                         bool fin) override;
  void OnContinuation(SpdyStreamId stream_id, bool end) override;
  void OnPriority(SpdyStreamId stream_id,
                  SpdyStreamId parent_stream_id,
                  int weight,
                  bool exclusive) override {}
  bool OnUnknownFrame(SpdyStreamId stream_id, uint8_t frame_type) override;

  // SpdyFramer methods.
  size_t ProcessInput(const char* data, size_t len);
  void UpdateHeaderDecoderTableSize(uint32_t value);
  void Reset();
  Http2DecoderAdapter::SpdyFramerError spdy_framer_error() const;
  Http2DecoderAdapter::SpdyState state() const;
  bool MessageFullyRead();
  bool HasError();
  std::unique_ptr<SpdySerializedFrame> CreateRstStream(
      SpdyStreamId stream_id,
      SpdyErrorCode error_code) const;
  std::unique_ptr<SpdySerializedFrame> CreateSettings(
      const SettingsMap& values) const;
  std::unique_ptr<SpdySerializedFrame> CreatePingFrame(SpdyPingId unique_id,
                                                       bool is_ack) const;
  std::unique_ptr<SpdySerializedFrame> CreateWindowUpdate(
      SpdyStreamId stream_id,
      uint32_t delta_window_size) const;
  std::unique_ptr<SpdySerializedFrame> CreateDataFrame(SpdyStreamId stream_id,
                                                       const char* data,
                                                       uint32_t len,
                                                       SpdyDataFlags flags);
  std::unique_ptr<SpdySerializedFrame> CreatePriority(
      SpdyStreamId stream_id,
      SpdyStreamId dependency_id,
      int weight,
      bool exclusive) const;

  // Serialize a frame of unknown type.
  SpdySerializedFrame SerializeFrame(const SpdyFrameIR& frame) {
    return spdy_framer_.SerializeFrame(frame);
  }

  int frames_received() const { return frames_received_; }

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  SpdyFramer spdy_framer_;
  Http2DecoderAdapter deframer_;
  BufferedSpdyFramerVisitorInterface* visitor_;

  int frames_received_;

  // Collection of fields from control frames that we need to
  // buffer up from the spdy framer.
  struct ControlFrameFields {
    SpdyFrameType type;
    SpdyStreamId stream_id;
    SpdyStreamId associated_stream_id;
    SpdyStreamId promised_stream_id;
    bool has_priority;
    SpdyPriority priority;
    int weight;
    SpdyStreamId parent_stream_id;
    bool exclusive;
    bool fin;
    bool unidirectional;
  };
  std::unique_ptr<ControlFrameFields> control_frame_fields_;

  // Collection of fields of a GOAWAY frame that this class needs to buffer.
  struct GoAwayFields {
    SpdyStreamId last_accepted_stream_id;
    SpdyErrorCode error_code;
    SpdyString debug_data;

    // Returns the estimate of dynamically allocated memory in bytes.
    size_t EstimateMemoryUsage() const;
  };
  std::unique_ptr<GoAwayFields> goaway_fields_;

  std::unique_ptr<HeaderCoalescer> coalescer_;

  const uint32_t max_header_list_size_;
  NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(BufferedSpdyFramer);
};

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_BUFFERED_SPDY_FRAMER_H_
