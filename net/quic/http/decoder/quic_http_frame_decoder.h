// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_H_
#define NET_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_H_

// QuicHttpFrameDecoder decodes the available input until it reaches the end of
// the input or it reaches the end of the first frame in the input.
// Note that QuicHttpFrameDecoder does only minimal validation; for example,
// stream ids are not checked, nor is the sequence of frames such as
// CONTINUATION frame placement.
//
// QuicHttpFrameDecoder enters state kError once it has called the listener's
// OnFrameSizeError or OnPaddingTooLong methods, and at this time has no
// provision for leaving that state. While the HTTP/2 spec (RFC7540) allows
// for some such errors to be considered as just stream errors in some cases,
// this implementation treats them all as connection errors.

#include <stddef.h>

#include <cstdint>

#include "base/logging.h"
#include "base/macros.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_altsvc_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_continuation_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_data_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_goaway_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_headers_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_ping_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_priority_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_push_promise_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_rst_stream_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_settings_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_unknown_payload_decoder.h"
#include "net/quic/http/decoder/payload_decoders/quic_http_window_update_payload_decoder.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpFrameDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpFrameDecoder {
 public:
  explicit QuicHttpFrameDecoder(QuicHttpFrameDecoderListener* listener);
  QuicHttpFrameDecoder() : QuicHttpFrameDecoder(nullptr) {}

  // The decoder will call the listener's methods as it decodes a frame.
  void set_listener(QuicHttpFrameDecoderListener* listener);
  QuicHttpFrameDecoderListener* listener() const;

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
  QuicHttpDecodeStatus DecodeFrame(QuicHttpDecodeBuffer* db);

  //////////////////////////////////////////////////////////////////////////////
  // Methods that support QuicHttpFrameDecoderAdapter.

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

  friend class test::QuicHttpFrameDecoderPeer;
  friend std::ostream& operator<<(std::ostream& out, State v);

  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus DiscardPayload(QuicHttpDecodeBuffer* db);

  const QuicHttpFrameHeader& frame_header() const {
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
  // end of the payload (handled by QuicHttpFrameDecoder::StartDecodingPayload).
  QuicHttpDecodeStatus StartDecodingAltSvcPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingContinuationPayload(
      QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingDataPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingGoAwayPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingHeadersPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingPingPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingPriorityPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingPushPromisePayload(
      QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingRstStreamPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingSettingsPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingUnknownPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus StartDecodingWindowUpdatePayload(
      QuicHttpDecodeBuffer* db);

  // These methods call the ResumeDecodingPayload() method of the frame type's
  // payload decoder; they are called only if the preceding call to the
  // corresponding Start method (above) returned kDecodeInProgress, as did any
  // subsequent calls to the resume method.
  // Unlike the Start methods, the decode buffer may extend beyond the
  // end of the payload, so the method will create a QuicHttpDecodeBufferSubset
  // before calling the ResumeDecodingPayload method of the frame type's
  // payload decoder.
  QuicHttpDecodeStatus ResumeDecodingAltSvcPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingContinuationPayload(
      QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingDataPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingGoAwayPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingHeadersPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingPingPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingPriorityPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingPushPromisePayload(
      QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingRstStreamPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingSettingsPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingUnknownPayload(QuicHttpDecodeBuffer* db);
  QuicHttpDecodeStatus ResumeDecodingWindowUpdatePayload(
      QuicHttpDecodeBuffer* db);

  QuicHttpFrameDecoderState frame_decoder_state_;

  // We only need one payload decoder at a time, so they share the same storage.
  union {
    QuicHttpAltSvcQuicHttpPayloadDecoder altsvc_payload_decoder_;
    QuicHttpContinuationQuicHttpPayloadDecoder continuation_payload_decoder_;
    QuicHttpDataQuicHttpPayloadDecoder data_payload_decoder_;
    QuicHttpGoAwayQuicHttpPayloadDecoder goaway_payload_decoder_;
    QuicHttpHeadersQuicHttpPayloadDecoder headers_payload_decoder_;
    QuicHttpPingQuicHttpPayloadDecoder ping_payload_decoder_;
    QuicHttpPriorityQuicHttpPayloadDecoder priority_payload_decoder_;
    QuicHttpPushPromiseQuicHttpPayloadDecoder push_promise_payload_decoder_;
    QuicHttpRstStreamQuicHttpPayloadDecoder rst_stream_payload_decoder_;
    QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder settings_payload_decoder_;
    QuicHttpUnknownQuicHttpPayloadDecoder unknown_payload_decoder_;
    QuicHttpWindowUpdateQuicHttpPayloadDecoder window_update_payload_decoder_;
  };

  State state_;
  size_t maximum_payload_size_;

  // Listener used whenever caller passes nullptr to set_listener.
  QuicHttpFrameDecoderNoOpListener no_op_listener_;

  DISALLOW_COPY_AND_ASSIGN(QuicHttpFrameDecoder);
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_H_
