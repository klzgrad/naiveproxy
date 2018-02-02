// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_QUIC_HTTP_STRUCTURE_DECODER_H_
#define NET_QUIC_HTTP_DECODER_QUIC_HTTP_STRUCTURE_DECODER_H_

// QuicHttpStructureDecoder is a class for decoding the fixed size structures in
// the HTTP/2 spec, defined in gfe/quic/http/quic_http_structures.h. This class
// is in aid of deciding whether to keep the SlowDecode methods which I
// (jamessynge) now think may not be worth their complexity. In particular,
// if most transport buffers are large, so it is rare that a structure is
// split across buffer boundaries, than the cost of buffering upon
// those rare occurrences is small, which then simplifies the callers.

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_decode_structures.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpStructureDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpStructureDecoder {
 public:
  // The caller needs to keep track of whether to call Start or Resume.
  //
  // Start has an optimization for the case where the QuicHttpDecodeBuffer holds
  // the entire encoded structure; in that case it decodes into *out and returns
  // true, and does NOT touch the data members of the QuicHttpStructureDecoder
  // instance because the caller won't be calling Resume later.
  //
  // However, if the QuicHttpDecodeBuffer is too small to hold the entire
  // encoded structure, Start copies the available bytes into the
  // QuicHttpStructureDecoder instance, and returns false to indicate that it
  // has not been able to complete the decoding.
  //
  template <class S>
  bool Start(S* out, QuicHttpDecodeBuffer* db) {
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
  bool Resume(S* out, QuicHttpDecodeBuffer* db) {
    DVLOG(2) << __func__ << "@" << this << ": offset_=" << offset_
             << "; db->Remaining=" << db->Remaining();
    if (ResumeFillingBuffer(db, S::EncodedSize())) {
      // We have the whole thing now.
      DVLOG(2) << __func__ << "@" << this << "    offset_=" << offset_
               << "    Ready to decode from buffer_.";
      QuicHttpDecodeBuffer buffer_db(buffer_, S::EncodedSize());
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
  QuicHttpDecodeStatus Start(S* out,
                             QuicHttpDecodeBuffer* db,
                             uint32_t* remaining_payload) {
    static_assert(S::EncodedSize() <= sizeof buffer_, "buffer_ is too small");
    DVLOG(2) << __func__ << "@" << this
             << ": *remaining_payload=" << *remaining_payload
             << "; db->Remaining=" << db->Remaining()
             << "; EncodedSize=" << S::EncodedSize();
    if (db->MinLengthRemaining(*remaining_payload) >= S::EncodedSize()) {
      DoDecode(out, db);
      *remaining_payload -= S::EncodedSize();
      return QuicHttpDecodeStatus::kDecodeDone;
    }
    return IncompleteStart(db, remaining_payload, S::EncodedSize());
  }

  template <class S>
  bool Resume(S* out, QuicHttpDecodeBuffer* db, uint32_t* remaining_payload) {
    DVLOG(3) << __func__ << "@" << this << ": offset_=" << offset_
             << "; *remaining_payload=" << *remaining_payload
             << "; db->Remaining=" << db->Remaining()
             << "; EncodedSize=" << S::EncodedSize();
    if (ResumeFillingBuffer(db, remaining_payload, S::EncodedSize())) {
      // We have the whole thing now.
      DVLOG(2) << __func__ << "@" << this << ": offset_=" << offset_
               << "; Ready to decode from buffer_.";
      QuicHttpDecodeBuffer buffer_db(buffer_, S::EncodedSize());
      DoDecode(out, &buffer_db);
      return true;
    }
    DCHECK_LT(offset_, S::EncodedSize());
    return false;
  }

  uint32_t offset() const { return offset_; }

 private:
  friend class test::QuicHttpStructureDecoderPeer;

  uint32_t IncompleteStart(QuicHttpDecodeBuffer* db, uint32_t target_size);
  QuicHttpDecodeStatus IncompleteStart(QuicHttpDecodeBuffer* db,
                                       uint32_t* remaining_payload,
                                       uint32_t target_size);

  bool ResumeFillingBuffer(QuicHttpDecodeBuffer* db, uint32_t target_size);
  bool ResumeFillingBuffer(QuicHttpDecodeBuffer* db,
                           uint32_t* remaining_payload,
                           uint32_t target_size);

  uint32_t offset_;
  char buffer_[QuicHttpFrameHeader::EncodedSize()];
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_QUIC_HTTP_STRUCTURE_DECODER_H_
