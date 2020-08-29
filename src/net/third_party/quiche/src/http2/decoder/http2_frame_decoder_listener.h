// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_HTTP2_FRAME_DECODER_LISTENER_H_
#define QUICHE_HTTP2_DECODER_HTTP2_FRAME_DECODER_LISTENER_H_

// Http2FrameDecoderListener is the interface which the HTTP/2 decoder uses
// to report the decoded frames to a listener.
//
// The general design is to assume that the listener will copy the data it needs
// (e.g. frame headers) and will keep track of the implicit state of the
// decoding process (i.e. the decoder maintains just the information it needs in
// order to perform the decoding). Therefore, the parameters are just those with
// (potentially) new data, not previously provided info about the current frame.
//
// The calls are described as if they are made in quick succession, i.e. one
// after another, but of course the decoder needs input to decode, and the
// decoder will only call the listener once the necessary input has been
// provided. For example: OnDataStart can only be called once the 9 bytes of
// of an HTTP/2 common frame header have been received. The decoder will call
// the listener methods as soon as possible to avoid almost all buffering.
//
// The listener interface is designed so that it is possible to exactly
// reconstruct the serialized frames, with the exception of reserved bits,
// including in the frame header's flags and stream_id fields, which will have
// been cleared before the methods below are called.

#include <stddef.h>

#include <cstdint>
#include <type_traits>

#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"

namespace http2 {

// TODO(jamessynge): Consider sorting the methods by frequency of call, if that
// helps at all.
class Http2FrameDecoderListener {
 public:
  Http2FrameDecoderListener() {}
  virtual ~Http2FrameDecoderListener() {}

  // Called once the common frame header has been decoded for any frame, and
  // before any of the methods below, which will also be called. This method is
  // included in this interface only for the purpose of supporting SpdyFramer
  // semantics via an adapter. This is the only method that has a non-void
  // return type, and this is just so that Http2FrameDecoderAdapter (called
  // from SpdyFramer) can more readily pass existing tests that expect decoding
  // to stop if the headers alone indicate an error. Return false to stop
  // decoding just after decoding the header, else return true to continue
  // decoding.
  // TODO(jamessynge): Remove OnFrameHeader once done with supporting
  // SpdyFramer's exact states.
  virtual bool OnFrameHeader(const Http2FrameHeader& header) = 0;

  //////////////////////////////////////////////////////////////////////////////

  // Called once the common frame header has been decoded for a DATA frame,
  // before examining the frame's payload, after which:
  //   OnPadLength will be called if header.IsPadded() is true, i.e. if the
  //     PADDED flag is set;
  //   OnDataPayload will be called as the non-padding portion of the payload
  //     is available until all of it has been provided;
  //   OnPadding will be called if the frame is padded AND the Pad Length field
  //     is greater than zero;
  //   OnDataEnd will be called last. If the frame is unpadded and has no
  //     payload, then this will be called immediately after OnDataStart.
  virtual void OnDataStart(const Http2FrameHeader& header) = 0;

  // Called when the next non-padding portion of a DATA frame's payload is
  // received.
  // |data| The start of |len| bytes of data.
  // |len| The length of the data buffer. Maybe zero in some cases, which does
  //       not mean anything special.
  virtual void OnDataPayload(const char* data, size_t len) = 0;

  // Called after an entire DATA frame has been received.
  // If header.IsEndStream() == true, this is the last data for the stream.
  virtual void OnDataEnd() = 0;

  // Called once the common frame header has been decoded for a HEADERS frame,
  // before examining the frame's payload, after which:
  //   OnPadLength will be called if header.IsPadded() is true, i.e. if the
  //     PADDED flag is set;
  //   OnHeadersPriority will be called if header.HasPriority() is true, i.e. if
  //     the frame has the PRIORITY flag;
  //   OnHpackFragment as the remainder of the non-padding payload is available
  //     until all if has been provided;
  //   OnPadding will be called if the frame is padded AND the Pad Length field
  //     is greater than zero;
  //   OnHeadersEnd will be called last; If the frame is unpadded and has no
  //     payload, then this will be called immediately after OnHeadersStart;
  //     OnHeadersEnd indicates the end of the HPACK block only if the frame
  //     header had the END_HEADERS flag set, else the END_HEADERS should be
  //     looked for on a subsequent CONTINUATION frame.
  virtual void OnHeadersStart(const Http2FrameHeader& header) = 0;

  // Called when a HEADERS frame is received with the PRIORITY flag set and
  // the priority fields have been decoded.
  virtual void OnHeadersPriority(
      const Http2PriorityFields& priority_fields) = 0;

  // Called when a fragment (i.e. some or all of an HPACK Block) is received;
  // this may be part of a HEADERS, PUSH_PROMISE or CONTINUATION frame.
  // |data| The start of |len| bytes of data.
  // |len| The length of the data buffer. Maybe zero in some cases, which does
  //       not mean anything special, except that it simplified the decoder.
  virtual void OnHpackFragment(const char* data, size_t len) = 0;

  // Called after an entire HEADERS frame has been received. The frame is the
  // end of the HEADERS if the END_HEADERS flag is set; else there should be
  // CONTINUATION frames after this frame.
  virtual void OnHeadersEnd() = 0;

  // Called when an entire PRIORITY frame has been decoded.
  virtual void OnPriorityFrame(const Http2FrameHeader& header,
                               const Http2PriorityFields& priority_fields) = 0;

  // Called once the common frame header has been decoded for a CONTINUATION
  // frame, before examining the frame's payload, after which:
  //   OnHpackFragment as the frame's payload is available until all of it
  //     has been provided;
  //   OnContinuationEnd will be called last; If the frame has no payload,
  //     then this will be called immediately after OnContinuationStart;
  //     the HPACK block is at an end if and only if the frame header passed
  //     to OnContinuationStart had the END_HEADERS flag set.
  virtual void OnContinuationStart(const Http2FrameHeader& header) = 0;

  // Called after an entire CONTINUATION frame has been received. The frame is
  // the end of the HEADERS if the END_HEADERS flag is set.
  virtual void OnContinuationEnd() = 0;

  // Called when Pad Length field has been read. Applies to DATA and HEADERS
  // frames. For PUSH_PROMISE frames, the Pad Length + 1 is provided in the
  // OnPushPromiseStart call as total_padding_length.
  virtual void OnPadLength(size_t pad_length) = 0;

  // Called when padding is skipped over.
  virtual void OnPadding(const char* padding, size_t skipped_length) = 0;

  // Called when an entire RST_STREAM frame has been decoded.
  // This is the only callback for RST_STREAM frames.
  virtual void OnRstStream(const Http2FrameHeader& header,
                           Http2ErrorCode error_code) = 0;

  // Called once the common frame header has been decoded for a SETTINGS frame
  // without the ACK flag, before examining the frame's payload, after which:
  //   OnSetting will be called in turn for each pair of settings parameter and
  //     value found in the payload;
  //   OnSettingsEnd will be called last; If the frame has no payload,
  //     then this will be called immediately after OnSettingsStart.
  // The frame header is passed so that the caller can check the stream_id,
  // which should be zero, but that hasn't been checked by the decoder.
  virtual void OnSettingsStart(const Http2FrameHeader& header) = 0;

  // Called for each setting parameter and value within a SETTINGS frame.
  virtual void OnSetting(const Http2SettingFields& setting_fields) = 0;

  // Called after parsing the complete payload of SETTINGS frame (non-ACK).
  virtual void OnSettingsEnd() = 0;

  // Called when an entire SETTINGS frame, with the ACK flag, has been decoded.
  virtual void OnSettingsAck(const Http2FrameHeader& header) = 0;

  // Called just before starting to process the HPACK block of a PUSH_PROMISE
  // frame. The Pad Length field has already been decoded at this point, so
  // OnPadLength will not be called; note that total_padding_length is Pad
  // Length + 1. After OnPushPromiseStart:
  //   OnHpackFragment as the remainder of the non-padding payload is available
  //     until all if has been provided;
  //   OnPadding will be called if the frame is padded AND the Pad Length field
  //     is greater than zero (i.e. total_padding_length > 1);
  //   OnPushPromiseEnd will be called last; If the frame is unpadded and has no
  //     payload, then this will be called immediately after OnPushPromiseStart.
  virtual void OnPushPromiseStart(const Http2FrameHeader& header,
                                  const Http2PushPromiseFields& promise,
                                  size_t total_padding_length) = 0;

  // Called after all of the HPACK block fragment and padding of a PUSH_PROMISE
  // has been decoded and delivered to the listener. This call indicates the end
  // of the HPACK block if and only if the frame header had the END_HEADERS flag
  // set (i.e. header.IsEndHeaders() is true); otherwise the next block must be
  // a CONTINUATION frame with the same stream id (not the same promised stream
  // id).
  virtual void OnPushPromiseEnd() = 0;

  // Called when an entire PING frame, without the ACK flag, has been decoded.
  virtual void OnPing(const Http2FrameHeader& header,
                      const Http2PingFields& ping) = 0;

  // Called when an entire PING frame, with the ACK flag, has been decoded.
  virtual void OnPingAck(const Http2FrameHeader& header,
                         const Http2PingFields& ping) = 0;

  // Called after parsing a GOAWAY frame's header and fixed size fields, after
  // which:
  //   OnGoAwayOpaqueData will be called as opaque data of the payload becomes
  //     available to the decoder, until all of it has been provided to the
  //     listener;
  //   OnGoAwayEnd will be called last, after all the opaque data has been
  //     provided to the listener; if there is no opaque data, then OnGoAwayEnd
  //     will be called immediately after OnGoAwayStart.
  virtual void OnGoAwayStart(const Http2FrameHeader& header,
                             const Http2GoAwayFields& goaway) = 0;

  // Called when the next portion of a GOAWAY frame's payload is received.
  // |data| The start of |len| bytes of opaque data.
  // |len| The length of the opaque data buffer. Maybe zero in some cases,
  //       which does not mean anything special.
  virtual void OnGoAwayOpaqueData(const char* data, size_t len) = 0;

  // Called after finishing decoding all of a GOAWAY frame.
  virtual void OnGoAwayEnd() = 0;

  // Called when an entire WINDOW_UPDATE frame has been decoded. The
  // window_size_increment is required to be non-zero, but that has not been
  // checked. If header.stream_id==0, the connection's flow control window is
  // being increased, else the specified stream's flow control is being
  // increased.
  virtual void OnWindowUpdate(const Http2FrameHeader& header,
                              uint32_t window_size_increment) = 0;

  // Called when an ALTSVC frame header and origin length have been parsed.
  // Either or both lengths may be zero. After OnAltSvcStart:
  //   OnAltSvcOriginData will be called until all of the (optional) Origin
  //     has been provided;
  //   OnAltSvcValueData will be called until all of the Alt-Svc-Field-Value
  //     has been provided;
  //   OnAltSvcEnd will called last, after all of the origin and
  //     Alt-Svc-Field-Value have been delivered to the listener.
  virtual void OnAltSvcStart(const Http2FrameHeader& header,
                             size_t origin_length,
                             size_t value_length) = 0;

  // Called when decoding the (optional) origin of an ALTSVC;
  // the field is uninterpreted.
  virtual void OnAltSvcOriginData(const char* data, size_t len) = 0;

  // Called when decoding the Alt-Svc-Field-Value of an ALTSVC;
  // the field is uninterpreted.
  virtual void OnAltSvcValueData(const char* data, size_t len) = 0;

  // Called after decoding all of a ALTSVC frame and providing to the listener
  // via the above methods.
  virtual void OnAltSvcEnd() = 0;

  // Called when the common frame header has been decoded, but the frame type
  // is unknown, after which:
  //   OnUnknownPayload is called as the payload of the frame is provided to the
  //     decoder, until all of the payload has been decoded;
  //   OnUnknownEnd will called last, after the entire frame of the unknown type
  //     has been decoded and provided to the listener.
  virtual void OnUnknownStart(const Http2FrameHeader& header) = 0;

  // Called when the payload of an unknown frame type is received.
  // |data| A buffer containing the data received.
  // |len| The length of the data buffer.
  virtual void OnUnknownPayload(const char* data, size_t len) = 0;

  // Called after decoding all of the payload of an unknown frame type.
  virtual void OnUnknownEnd() = 0;

  //////////////////////////////////////////////////////////////////////////////
  // Below here are events indicating a problem has been detected during
  // decoding (i.e. the received frames are malformed in some way).

  // Padding field (uint8) has a value that is too large (i.e. the amount of
  // padding is greater than the remainder of the payload that isn't required).
  // From RFC Section 6.1, DATA:
  //     If the length of the padding is the length of the frame payload or
  //     greater, the recipient MUST treat this as a connection error
  //     (Section 5.4.1) of type PROTOCOL_ERROR.
  // The same is true for HEADERS and PUSH_PROMISE.
  virtual void OnPaddingTooLong(const Http2FrameHeader& header,
                                size_t missing_length) = 0;

  // Frame size error. Depending upon the effected frame, this may or may not
  // require terminating the connection, though that is probably the best thing
  // to do.
  // From RFC Section 4.2, Frame Size:
  //     An endpoint MUST send an error code of FRAME_SIZE_ERROR if a frame
  //     exceeds the size defined in SETTINGS_MAX_FRAME_SIZE, exceeds any limit
  //     defined for the frame type, or is too small to contain mandatory frame
  //     data. A frame size error in a frame that could alter the state of the
  //     the entire connection MUST be treated as a connection error
  //     (Section 5.4.1); this includes any frame carrying a header block
  //     (Section 4.3) (that is, HEADERS, PUSH_PROMISE, and CONTINUATION),
  //     SETTINGS, and any frame with a stream identifier of 0.
  virtual void OnFrameSizeError(const Http2FrameHeader& header) = 0;
};

// Do nothing for each call. Useful for ignoring a frame that is invalid.
class Http2FrameDecoderNoOpListener : public Http2FrameDecoderListener {
 public:
  Http2FrameDecoderNoOpListener() {}
  ~Http2FrameDecoderNoOpListener() override {}

  // TODO(jamessynge): Remove OnFrameHeader once done with supporting
  // SpdyFramer's exact states.
  bool OnFrameHeader(const Http2FrameHeader& header) override;

  void OnDataStart(const Http2FrameHeader& /*header*/) override {}
  void OnDataPayload(const char* /*data*/, size_t /*len*/) override {}
  void OnDataEnd() override {}
  void OnHeadersStart(const Http2FrameHeader& /*header*/) override {}
  void OnHeadersPriority(const Http2PriorityFields& /*priority*/) override {}
  void OnHpackFragment(const char* /*data*/, size_t /*len*/) override {}
  void OnHeadersEnd() override {}
  void OnPriorityFrame(const Http2FrameHeader& /*header*/,
                       const Http2PriorityFields& /*priority*/) override {}
  void OnContinuationStart(const Http2FrameHeader& /*header*/) override {}
  void OnContinuationEnd() override {}
  void OnPadLength(size_t /*trailing_length*/) override {}
  void OnPadding(const char* /*padding*/, size_t /*skipped_length*/) override {}
  void OnRstStream(const Http2FrameHeader& /*header*/,
                   Http2ErrorCode /*error_code*/) override {}
  void OnSettingsStart(const Http2FrameHeader& /*header*/) override {}
  void OnSetting(const Http2SettingFields& /*setting_fields*/) override {}
  void OnSettingsEnd() override {}
  void OnSettingsAck(const Http2FrameHeader& /*header*/) override {}
  void OnPushPromiseStart(const Http2FrameHeader& /*header*/,
                          const Http2PushPromiseFields& /*promise*/,
                          size_t /*total_padding_length*/) override {}
  void OnPushPromiseEnd() override {}
  void OnPing(const Http2FrameHeader& /*header*/,
              const Http2PingFields& /*ping*/) override {}
  void OnPingAck(const Http2FrameHeader& /*header*/,
                 const Http2PingFields& /*ping*/) override {}
  void OnGoAwayStart(const Http2FrameHeader& /*header*/,
                     const Http2GoAwayFields& /*goaway*/) override {}
  void OnGoAwayOpaqueData(const char* /*data*/, size_t /*len*/) override {}
  void OnGoAwayEnd() override {}
  void OnWindowUpdate(const Http2FrameHeader& /*header*/,
                      uint32_t /*increment*/) override {}
  void OnAltSvcStart(const Http2FrameHeader& /*header*/,
                     size_t /*origin_length*/,
                     size_t /*value_length*/) override {}
  void OnAltSvcOriginData(const char* /*data*/, size_t /*len*/) override {}
  void OnAltSvcValueData(const char* /*data*/, size_t /*len*/) override {}
  void OnAltSvcEnd() override {}
  void OnUnknownStart(const Http2FrameHeader& /*header*/) override {}
  void OnUnknownPayload(const char* /*data*/, size_t /*len*/) override {}
  void OnUnknownEnd() override {}
  void OnPaddingTooLong(const Http2FrameHeader& /*header*/,
                        size_t /*missing_length*/) override {}
  void OnFrameSizeError(const Http2FrameHeader& /*header*/) override {}
};

static_assert(!std::is_abstract<Http2FrameDecoderNoOpListener>(),
              "Http2FrameDecoderNoOpListener ought to be concrete.");

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_HTTP2_FRAME_DECODER_LISTENER_H_
