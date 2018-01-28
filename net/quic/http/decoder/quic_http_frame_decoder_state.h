// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_STATE_H_
#define NET_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_STATE_H_

// QuicHttpFrameDecoderState provides state and behaviors in support of decoding
// the common frame header and the payload of all frame types.
// It is an input to all of the payload decoders.

// TODO(jamessynge): Since QuicHttpFrameDecoderState has far more than state in
// it, rename to FrameDecoderHelper, or similar.

#include <stddef.h>

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/decoder/quic_http_structure_decoder.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpFrameDecoderStatePeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpFrameDecoderState {
 public:
  QuicHttpFrameDecoderState() {}

  // Sets the listener which the decoders should call as they decode HTTP/2
  // frames. The listener can be changed at any time, which allows for replacing
  // it with a no-op listener when an error is detected, either by the payload
  // decoder (OnPaddingTooLong or OnFrameSizeError) or by the "real" listener.
  // That in turn allows us to define QuicHttpFrameDecoderListener such that all
  // methods have return type void, with no direct way to indicate whether the
  // decoder should stop, and to eliminate from the decoder all checks of the
  // return value. Instead the listener/caller can simply replace the current
  // listener with a no-op listener implementation.
  // TODO(jamessynge): Make set_listener private as only QuicHttpFrameDecoder
  // and tests need to set it, so it doesn't need to be public.
  void set_listener(QuicHttpFrameDecoderListener* listener) {
    listener_ = listener;
  }
  QuicHttpFrameDecoderListener* listener() const { return listener_; }

  // The most recently decoded frame header.
  const QuicHttpFrameHeader& frame_header() const { return frame_header_; }

  // Decode a structure in the payload, adjusting remaining_payload_ to account
  // for the consumed portion of the payload. Returns kDecodeDone when fully
  // decoded, kDecodeError if it ran out of payload before decoding completed,
  // and kDecodeInProgress if the decode buffer didn't have enough of the
  // remaining payload.
  template <class S>
  QuicHttpDecodeStatus StartDecodingStructureInPayload(
      S* out,
      QuicHttpDecodeBuffer* db) {
    DVLOG(2) << __func__ << "\n\tdb->Remaining=" << db->Remaining()
             << "\n\tremaining_payload_=" << remaining_payload_
             << "\n\tneed=" << S::EncodedSize();
    QuicHttpDecodeStatus status =
        structure_decoder_.Start(out, db, &remaining_payload_);
    if (status != QuicHttpDecodeStatus::kDecodeError) {
      return status;
    }
    DVLOG(2) << "StartDecodingStructureInPayload: detected frame size error";
    return ReportFrameSizeError();
  }

  // Resume decoding of a structure that has been split across buffers,
  // adjusting remaining_payload_ to account for the consumed portion of
  // the payload. Returns values are as for StartDecodingStructureInPayload.
  template <class S>
  QuicHttpDecodeStatus ResumeDecodingStructureInPayload(
      S* out,
      QuicHttpDecodeBuffer* db) {
    DVLOG(2) << __func__ << "\n\tdb->Remaining=" << db->Remaining()
             << "\n\tremaining_payload_=" << remaining_payload_;
    if (structure_decoder_.Resume(out, db, &remaining_payload_)) {
      return QuicHttpDecodeStatus::kDecodeDone;
    } else if (remaining_payload_ > 0) {
      return QuicHttpDecodeStatus::kDecodeInProgress;
    } else {
      DVLOG(2) << "ResumeDecodingStructureInPayload: detected frame size error";
      return ReportFrameSizeError();
    }
  }

  // Initializes the two remaining* fields, which is needed if the frame's
  // payload is split across buffers, or the decoder calls ReadPadLength or
  // StartDecodingStructureInPayload, and of course the methods below which
  // read those fields, as their names imply.
  void InitializeRemainders() {
    remaining_payload_ = frame_header().payload_length;
    // Note that remaining_total_payload() relies on remaining_padding_ being
    // zero for frames that have no padding.
    remaining_padding_ = 0;
  }

  // Returns the number of bytes of the frame's payload that remain to be
  // decoded, including any trailing padding. This method must only be called
  // after the variables have been initialized, which in practice means once a
  // payload decoder has called InitializeRemainders and/or ReadPadLength.
  size_t remaining_total_payload() const {
    DCHECK(IsPaddable() || remaining_padding_ == 0) << frame_header();
    return remaining_payload_ + remaining_padding_;
  }

  // Returns the number of bytes of the frame's payload that remain to be
  // decoded, excluding any trailing padding. This method must only be called
  // after the variable has been initialized, which in practice means once a
  // payload decoder has called InitializeRemainders; ReadPadLength will deduct
  // the total number of padding bytes from remaining_payload_, including the
  // size of the Pad Length field itself (1 byte).
  size_t remaining_payload() const { return remaining_payload_; }

  // Returns the number of bytes of the frame's payload that remain to be
  // decoded, including any trailing padding. This method must only be called if
  // the frame type allows padding, and after the variable has been initialized,
  // which in practice means once a payload decoder has called
  // InitializeRemainders and/or ReadPadLength.
  size_t remaining_payload_and_padding() const {
    DCHECK(IsPaddable()) << frame_header();
    return remaining_payload_ + remaining_padding_;
  }

  // Returns the number of bytes of trailing padding after the payload that
  // remain to be decoded. This method must only be called if the frame type
  // allows padding, and after the variable has been initialized, which in
  // practice means once a payload decoder has called InitializeRemainders,
  // and isn't set to a non-zero value until ReadPadLength has been called.
  uint32_t remaining_padding() const {
    DCHECK(IsPaddable()) << frame_header();
    return remaining_padding_;
  }

  // Returns the amount of trailing padding after the payload that remains to be
  // decoded.
  uint32_t remaining_padding_for_test() const { return remaining_padding_; }

  // How many bytes of the remaining payload are in db?
  size_t AvailablePayload(QuicHttpDecodeBuffer* db) const {
    return db->MinLengthRemaining(remaining_payload_);
  }

  // How many bytes of the remaining payload and padding are in db?
  // Call only for frames whose type is paddable.
  size_t AvailablePayloadAndPadding(QuicHttpDecodeBuffer* db) const {
    DCHECK(IsPaddable()) << frame_header();
    return db->MinLengthRemaining(remaining_payload_ + remaining_padding_);
  }

  // How many bytes of the padding that have not yet been skipped are in db?
  // Call only after remaining_padding_ has been set (for padded frames), or
  // been cleared (for unpadded frames); and after all of the non-padding
  // payload has been decoded.
  size_t AvailablePadding(QuicHttpDecodeBuffer* db) const {
    DCHECK(IsPaddable()) << frame_header();
    DCHECK_EQ(remaining_payload_, 0u);
    return db->MinLengthRemaining(remaining_padding_);
  }

  // Reduces remaining_payload_ by amount. To be called by a payload decoder
  // after it has passed a variable length portion of the payload to the
  // listener; remaining_payload_ will be automatically reduced when fixed
  // size structures and padding, including the Pad Length field, are decoded.
  void ConsumePayload(size_t amount) {
    DCHECK_LE(amount, remaining_payload_);
    remaining_payload_ -= amount;
  }

  // Reads the Pad Length field into remaining_padding_, and appropriately sets
  // remaining_payload_. When present, the Pad Length field is always the first
  // field in the payload, which this method relies on so that the caller need
  // not set remaining_payload_ before calling this method.
  // If report_pad_length is true, calls the listener's OnPadLength method when
  // it decodes the Pad Length field.
  // Returns kDecodeDone if the decode buffer was not empty (i.e. because the
  // field is only a single byte long, it can always be decoded if the buffer is
  // not empty).
  // Returns kDecodeError if the buffer is empty because the frame has no
  // payload (i.e. payload_length() == 0).
  // Returns kDecodeInProgress if the buffer is empty but the frame has a
  // payload.
  QuicHttpDecodeStatus ReadPadLength(QuicHttpDecodeBuffer* db,
                                     bool report_pad_length);

  // Skip the trailing padding bytes; only call once remaining_payload_==0.
  // Returns true when the padding has been skipped.
  // Does NOT check that the padding is all zeroes.
  bool SkipPadding(QuicHttpDecodeBuffer* db);

  // Calls the listener's OnFrameSizeError method and returns kDecodeError.
  QuicHttpDecodeStatus ReportFrameSizeError();

 private:
  friend class QuicHttpFrameDecoder;
  friend class test::QuicHttpFrameDecoderStatePeer;

  // Starts the decoding of a common frame header. Returns true if completed the
  // decoding, false if the decode buffer didn't have enough data in it, in
  // which case the decode buffer will have been drained and the caller should
  // call ResumeDecodingFrameHeader when more data is available. This is called
  // from QuicHttpFrameDecoder, a friend class.
  bool StartDecodingFrameHeader(QuicHttpDecodeBuffer* db) {
    return structure_decoder_.Start(&frame_header_, db);
  }

  // Resumes decoding the common frame header after the preceding call to
  // StartDecodingFrameHeader returned false, as did any subsequent calls to
  // ResumeDecodingFrameHeader. This is called from QuicHttpFrameDecoder,
  // a friend class.
  bool ResumeDecodingFrameHeader(QuicHttpDecodeBuffer* db) {
    return structure_decoder_.Resume(&frame_header_, db);
  }

  // Clear any of the flags in the frame header that aren't set in valid_flags.
  void RetainFlags(uint8_t valid_flags) {
    frame_header_.RetainFlags(valid_flags);
  }

  // Clear all of the flags in the frame header; for use with frame types that
  // don't define any flags, such as WINDOW_UPDATE.
  void ClearFlags() { frame_header_.flags = QuicHttpFrameFlag(); }

  // Returns true if the type of frame being decoded can have padding.
  bool IsPaddable() const {
    return frame_header().type == QuicHttpFrameType::DATA ||
           frame_header().type == QuicHttpFrameType::HEADERS ||
           frame_header().type == QuicHttpFrameType::PUSH_PROMISE;
  }

  QuicHttpFrameDecoderListener* listener_ = nullptr;
  QuicHttpFrameHeader frame_header_;

  // Number of bytes remaining to be decoded, if set; does not include the
  // trailing padding once the length of padding has been determined.
  // See ReadPadLength.
  uint32_t remaining_payload_;

  // The amount of trailing padding after the payload that remains to be
  // decoded. See ReadPadLength.
  uint32_t remaining_padding_;

  // Generic decoder of structures, which takes care of buffering the needed
  // bytes if the encoded structure is split across decode buffers.
  QuicHttpStructureDecoder structure_decoder_;
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_QUIC_HTTP_FRAME_DECODER_STATE_H_
