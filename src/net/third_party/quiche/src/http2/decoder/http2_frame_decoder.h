// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_HTTP2_FRAME_DECODER_H_
#define QUICHE_HTTP2_DECODER_HTTP2_FRAME_DECODER_H_

// Http2FrameDecoder decodes the available input until it reaches the end of
// the input or it reaches the end of the first frame in the input.
// Note that Http2FrameDecoder does only minimal validation; for example,
// stream ids are not checked, nor is the sequence of frames such as
// CONTINUATION frame placement.
//
// Http2FrameDecoder enters state kError once it has called the listener's
// OnFrameSizeError or OnPaddingTooLong methods, and at this time has no
// provision for leaving that state. While the HTTP/2 spec (RFC7540) allows
// for some such errors to be considered as just stream errors in some cases,
// this implementation treats them all as connection errors.

#include <stddef.h>

#include <cstdint>

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/altsvc_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/continuation_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/data_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/goaway_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/headers_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/ping_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/priority_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/push_promise_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/rst_stream_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/settings_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/unknown_payload_decoder.h"
#include "net/third_party/quiche/src/http2/decoder/payload_decoders/window_update_payload_decoder.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class Http2FrameDecoderPeer;
}  // namespace test

class QUICHE_EXPORT_PRIVATE Http2FrameDecoder {
 public:
  explicit Http2FrameDecoder(Http2FrameDecoderListener* listener);
  Http2FrameDecoder() : Http2FrameDecoder(nullptr) {}

  Http2FrameDecoder(const Http2FrameDecoder&) = delete;
  Http2FrameDecoder& operator=(const Http2FrameDecoder&) = delete;

  // The decoder will call the listener's methods as it decodes a frame.
  void set_listener(Http2FrameDecoderListener* listener);
  Http2FrameDecoderListener* listener() const;

  // The decoder will reject frame's whose payload
  // length field exceeds the maximum payload size.
  void set_maximum_payload_size(size_t v) { maximum_payload_size_ = v; }
  size_t maximum_payload_size() const { return maximum_payload_size_; }

  // Decodes the input up to the next frame boundary (i.e. at most one frame).
  //
  // Returns kDecodeDone if it decodes the final byte of a frame, OR if there
  // is no input and it is awaiting the start of a new frame (e.g. if this
  // is the first call to DecodeFrame, or if the previous call returned
  // kDecodeDone).
  //
  // Returns kDecodeInProgress if it decodes all of the decode buffer, but has
  // not reached the end of the frame.
  //
  // Returns kDecodeError if the frame's padding or length wasn't valid (i.e. if
  // the decoder called either the listener's OnPaddingTooLong or
  // OnFrameSizeError method).
  DecodeStatus DecodeFrame(DecodeBuffer* db);

  //////////////////////////////////////////////////////////////////////////////
  // Methods that support Http2FrameDecoderAdapter.

  // Is the remainder of the frame's payload being discarded?
  bool IsDiscardingPayload() const { return state_ == State::kDiscardPayload; }

  // Returns the number of bytes of the frame's payload that remain to be
  // decoded, excluding any trailing padding. This method must only be called
  // after the frame header has been decoded AND DecodeFrame has returned
  // kDecodeInProgress.
  size_t remaining_payload() const;

  // Returns the number of bytes of trailing padding after the payload that
  // remain to be decoded. This method must only be called if the frame type
  // allows padding, and after the frame header has been decoded AND
  // DecodeFrame has returned. Will return 0 if the Pad Length field has not
  // yet been decoded.
  uint32_t remaining_padding() const;

 private:
  enum class State {
    // Ready to start decoding a new frame's header.
    kStartDecodingHeader,
    // Was in state kStartDecodingHeader, but unable to read the entire frame
    // header, so needs more input to complete decoding the header.
    kResumeDecodingHeader,

    // Have decoded the frame header, and started decoding the available bytes
    // of the frame's payload, but need more bytes to finish the job.
    kResumeDecodingPayload,

    // Decoding of the most recently started frame resulted in an error:
    // OnPaddingTooLong or OnFrameSizeError was called to indicate that the
    // decoder detected a problem, or OnFrameHeader returned false, indicating
    // that the listener detected a problem. Regardless of which, the decoder
    // will stay in state kDiscardPayload until it has been passed the rest
    // of the bytes of the frame's payload that it hasn't yet seen, after
    // which it will be ready to decode another frame.
    kDiscardPayload,
  };

  friend class test::Http2FrameDecoderPeer;
  friend std::ostream& operator<<(std::ostream& out, State v);

  DecodeStatus StartDecodingPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingPayload(DecodeBuffer* db);
  DecodeStatus DiscardPayload(DecodeBuffer* db);

  const Http2FrameHeader& frame_header() const {
    return frame_decoder_state_.frame_header();
  }

  // Clear any of the flags in the frame header that aren't set in valid_flags.
  void RetainFlags(uint8_t valid_flags);

  // Clear all of the flags in the frame header; for use with frame types that
  // don't define any flags, such as WINDOW_UPDATE.
  void ClearFlags();

  // These methods call the StartDecodingPayload() method of the frame type's
  // payload decoder, after first clearing invalid flags in the header. The
  // caller must ensure that the decode buffer does not extend beyond the
  // end of the payload (handled by Http2FrameDecoder::StartDecodingPayload).
  DecodeStatus StartDecodingAltSvcPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingContinuationPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingDataPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingGoAwayPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingHeadersPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingPingPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingPriorityPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingPushPromisePayload(DecodeBuffer* db);
  DecodeStatus StartDecodingRstStreamPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingSettingsPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingUnknownPayload(DecodeBuffer* db);
  DecodeStatus StartDecodingWindowUpdatePayload(DecodeBuffer* db);

  // These methods call the ResumeDecodingPayload() method of the frame type's
  // payload decoder; they are called only if the preceding call to the
  // corresponding Start method (above) returned kDecodeInProgress, as did any
  // subsequent calls to the resume method.
  // Unlike the Start methods, the decode buffer may extend beyond the
  // end of the payload, so the method will create a DecodeBufferSubset
  // before calling the ResumeDecodingPayload method of the frame type's
  // payload decoder.
  DecodeStatus ResumeDecodingAltSvcPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingContinuationPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingDataPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingGoAwayPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingHeadersPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingPingPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingPriorityPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingPushPromisePayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingRstStreamPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingSettingsPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingUnknownPayload(DecodeBuffer* db);
  DecodeStatus ResumeDecodingWindowUpdatePayload(DecodeBuffer* db);

  FrameDecoderState frame_decoder_state_;

  // We only need one payload decoder at a time, so they share the same storage.
  union {
    AltSvcPayloadDecoder altsvc_payload_decoder_;
    ContinuationPayloadDecoder continuation_payload_decoder_;
    DataPayloadDecoder data_payload_decoder_;
    GoAwayPayloadDecoder goaway_payload_decoder_;
    HeadersPayloadDecoder headers_payload_decoder_;
    PingPayloadDecoder ping_payload_decoder_;
    PriorityPayloadDecoder priority_payload_decoder_;
    PushPromisePayloadDecoder push_promise_payload_decoder_;
    RstStreamPayloadDecoder rst_stream_payload_decoder_;
    SettingsPayloadDecoder settings_payload_decoder_;
    UnknownPayloadDecoder unknown_payload_decoder_;
    WindowUpdatePayloadDecoder window_update_payload_decoder_;
  };

  State state_;
  size_t maximum_payload_size_;

  // Listener used whenever caller passes nullptr to set_listener.
  Http2FrameDecoderNoOpListener no_op_listener_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_HTTP2_FRAME_DECODER_H_
