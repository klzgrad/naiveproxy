// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_HTTP2_STRUCTURE_DECODER_H_
#define NET_HTTP2_DECODER_HTTP2_STRUCTURE_DECODER_H_

// Http2StructureDecoder is a class for decoding the fixed size structures in
// the HTTP/2 spec, defined in net/http2/http2_structures.h. This class
// is in aid of deciding whether to keep the SlowDecode methods which I
// (jamessynge) now think may not be worth their complexity. In particular,
// if most transport buffers are large, so it is rare that a structure is
// split across buffer boundaries, than the cost of buffering upon
// those rare occurrences is small, which then simplifies the callers.

#include "base/logging.h"
#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/decode_http2_structures.h"
#include "net/http2/decoder/decode_status.h"
#include "net/http2/http2_structures.h"
#include "net/http2/platform/api/http2_export.h"

namespace net {
namespace test {
class Http2StructureDecoderPeer;
}  // namespace test

class HTTP2_EXPORT_PRIVATE Http2StructureDecoder {
 public:
  // The caller needs to keep track of whether to call Start or Resume.
  //
  // Start has an optimization for the case where the DecodeBuffer holds the
  // entire encoded structure; in that case it decodes into *out and returns
  // true, and does NOT touch the data members of the Http2StructureDecoder
  // instance because the caller won't be calling Resume later.
  //
  // However, if the DecodeBuffer is too small to hold the entire encoded
  // structure, Start copies the available bytes into the Http2StructureDecoder
  // instance, and returns false to indicate that it has not been able to
  // complete the decoding.
  //
  template <class S>
  bool Start(S* out, DecodeBuffer* db) {
    static_assert(S::EncodedSize() <= sizeof buffer_, "buffer_ is too small");
    DVLOG(2) << __func__ << "@" << this << ": db->Remaining=" << db->Remaining()
             << "; EncodedSize=" << S::EncodedSize();
    if (db->Remaining() >= S::EncodedSize()) {
      DoDecode(out, db);
      return true;
    }
    IncompleteStart(db, S::EncodedSize());
    return false;
  }

  template <class S>
  bool Resume(S* out, DecodeBuffer* db) {
    DVLOG(2) << __func__ << "@" << this << ": offset_=" << offset_
             << "; db->Remaining=" << db->Remaining();
    if (ResumeFillingBuffer(db, S::EncodedSize())) {
      // We have the whole thing now.
      DVLOG(2) << __func__ << "@" << this << "    offset_=" << offset_
               << "    Ready to decode from buffer_.";
      DecodeBuffer buffer_db(buffer_, S::EncodedSize());
      DoDecode(out, &buffer_db);
      return true;
    }
    DCHECK_LT(offset_, S::EncodedSize());
    return false;
  }

  // A second pair of Start and Resume, where the caller has a variable,
  // |remaining_payload| that is both tested for sufficiency and updated
  // during decoding. Note that the decode buffer may extend beyond the
  // remaining payload because the buffer may include padding.
  template <class S>
  DecodeStatus Start(S* out, DecodeBuffer* db, uint32_t* remaining_payload) {
    static_assert(S::EncodedSize() <= sizeof buffer_, "buffer_ is too small");
    DVLOG(2) << __func__ << "@" << this
             << ": *remaining_payload=" << *remaining_payload
             << "; db->Remaining=" << db->Remaining()
             << "; EncodedSize=" << S::EncodedSize();
    if (db->MinLengthRemaining(*remaining_payload) >= S::EncodedSize()) {
      DoDecode(out, db);
      *remaining_payload -= S::EncodedSize();
      return DecodeStatus::kDecodeDone;
    }
    return IncompleteStart(db, remaining_payload, S::EncodedSize());
  }

  template <class S>
  bool Resume(S* out, DecodeBuffer* db, uint32_t* remaining_payload) {
    DVLOG(3) << __func__ << "@" << this << ": offset_=" << offset_
             << "; *remaining_payload=" << *remaining_payload
             << "; db->Remaining=" << db->Remaining()
             << "; EncodedSize=" << S::EncodedSize();
    if (ResumeFillingBuffer(db, remaining_payload, S::EncodedSize())) {
      // We have the whole thing now.
      DVLOG(2) << __func__ << "@" << this << ": offset_=" << offset_
               << "; Ready to decode from buffer_.";
      DecodeBuffer buffer_db(buffer_, S::EncodedSize());
      DoDecode(out, &buffer_db);
      return true;
    }
    DCHECK_LT(offset_, S::EncodedSize());
    return false;
  }

  uint32_t offset() const { return offset_; }

 private:
  friend class test::Http2StructureDecoderPeer;

  uint32_t IncompleteStart(DecodeBuffer* db, uint32_t target_size);
  DecodeStatus IncompleteStart(DecodeBuffer* db,
                               uint32_t* remaining_payload,
                               uint32_t target_size);

  bool ResumeFillingBuffer(DecodeBuffer* db, uint32_t target_size);
  bool ResumeFillingBuffer(DecodeBuffer* db,
                           uint32_t* remaining_payload,
                           uint32_t target_size);

  uint32_t offset_;
  char buffer_[Http2FrameHeader::EncodedSize()];
};

}  // namespace net

#endif  // NET_HTTP2_DECODER_HTTP2_STRUCTURE_DECODER_H_
