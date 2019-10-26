// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_STRING_DECODER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_STRING_DECODER_H_

// HpackStringDecoder decodes strings encoded per the HPACK spec; this does
// not mean decompressing Huffman encoded strings, just identifying the length,
// encoding and contents for a listener.

#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_decoder.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_export.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_macros.h"

namespace http2 {

// Decodes a single string in an HPACK header entry. The high order bit of
// the first byte of the length is the H (Huffman) bit indicating whether
// the value is Huffman encoded, and the remainder of the byte is the first
// 7 bits of an HPACK varint.
//
// Call Start() to begin decoding; if it returns kDecodeInProgress, then call
// Resume() when more input is available, repeating until kDecodeInProgress is
// not returned. If kDecodeDone or kDecodeError is returned, then Resume() must
// not be called until Start() has been called to start decoding a new string.
class HTTP2_EXPORT_PRIVATE HpackStringDecoder {
 public:
  enum StringDecoderState {
    kStartDecodingLength,
    kDecodingString,
    kResumeDecodingLength,
  };

  template <class Listener>
  DecodeStatus Start(DecodeBuffer* db, Listener* cb) {
    // Fast decode path is used if the string is under 127 bytes and the
    // entire length of the string is in the decode buffer. More than 83% of
    // string lengths are encoded in just one byte.
    if (db->HasData() && (*db->cursor() & 0x7f) != 0x7f) {
      // The string is short.
      uint8_t h_and_prefix = db->DecodeUInt8();
      uint8_t length = h_and_prefix & 0x7f;
      bool huffman_encoded = (h_and_prefix & 0x80) == 0x80;
      cb->OnStringStart(huffman_encoded, length);
      if (length <= db->Remaining()) {
        // Yeah, we've got the whole thing in the decode buffer.
        // Ideally this will be the common case. Note that we don't
        // update any of the member variables in this path.
        cb->OnStringData(db->cursor(), length);
        db->AdvanceCursor(length);
        cb->OnStringEnd();
        return DecodeStatus::kDecodeDone;
      }
      // Not all in the buffer.
      huffman_encoded_ = huffman_encoded;
      remaining_ = length;
      // Call Resume to decode the string body, which is only partially
      // in the decode buffer (or not at all).
      state_ = kDecodingString;
      return Resume(db, cb);
    }
    // Call Resume to decode the string length, which is either not in
    // the decode buffer, or spans multiple bytes.
    state_ = kStartDecodingLength;
    return Resume(db, cb);
  }

  template <class Listener>
  DecodeStatus Resume(DecodeBuffer* db, Listener* cb) {
    DecodeStatus status;
    while (true) {
      switch (state_) {
        case kStartDecodingLength:
          HTTP2_DVLOG(2) << "kStartDecodingLength: db->Remaining="
                         << db->Remaining();
          if (!StartDecodingLength(db, cb, &status)) {
            // The length is split across decode buffers.
            return status;
          }
          // We've finished decoding the length, which spanned one or more
          // bytes. Approximately 17% of strings have a length that is greater
          // than 126 bytes, and thus the length is encoded in more than one
          // byte, and so doesn't get the benefit of the optimization in
          // Start() for single byte lengths. But, we still expect that most
          // of such strings will be contained entirely in a single decode
          // buffer, and hence this fall through skips another trip through the
          // switch above and more importantly skips setting the state_ variable
          // again in those cases where we don't need it.
          HTTP2_FALLTHROUGH;

        case kDecodingString:
          HTTP2_DVLOG(2) << "kDecodingString: db->Remaining=" << db->Remaining()
                         << "    remaining_=" << remaining_;
          return DecodeString(db, cb);

        case kResumeDecodingLength:
          HTTP2_DVLOG(2) << "kResumeDecodingLength: db->Remaining="
                         << db->Remaining();
          if (!ResumeDecodingLength(db, cb, &status)) {
            return status;
          }
      }
    }
  }

  std::string DebugString() const;

 private:
  static std::string StateToString(StringDecoderState v);

  // Returns true if the length is fully decoded and the listener wants the
  // decoding to continue, false otherwise; status is set to the status from
  // the varint decoder.
  // If the length is not fully decoded, case state_ is set appropriately
  // for the next call to Resume.
  template <class Listener>
  bool StartDecodingLength(DecodeBuffer* db,
                           Listener* cb,
                           DecodeStatus* status) {
    if (db->Empty()) {
      *status = DecodeStatus::kDecodeInProgress;
      state_ = kStartDecodingLength;
      return false;
    }
    uint8_t h_and_prefix = db->DecodeUInt8();
    huffman_encoded_ = (h_and_prefix & 0x80) == 0x80;
    *status = length_decoder_.Start(h_and_prefix, 7, db);
    if (*status == DecodeStatus::kDecodeDone) {
      OnStringStart(cb, status);
      return true;
    }
    // Set the state to cover the DecodeStatus::kDecodeInProgress case.
    // Won't be needed if the status is kDecodeError.
    state_ = kResumeDecodingLength;
    return false;
  }

  // Returns true if the length is fully decoded and the listener wants the
  // decoding to continue, false otherwise; status is set to the status from
  // the varint decoder; state_ is updated when fully decoded.
  // If the length is not fully decoded, case state_ is set appropriately
  // for the next call to Resume.
  template <class Listener>
  bool ResumeDecodingLength(DecodeBuffer* db,
                            Listener* cb,
                            DecodeStatus* status) {
    DCHECK_EQ(state_, kResumeDecodingLength);
    *status = length_decoder_.Resume(db);
    if (*status == DecodeStatus::kDecodeDone) {
      state_ = kDecodingString;
      OnStringStart(cb, status);
      return true;
    }
    return false;
  }

  // Returns true if the listener wants the decoding to continue, and
  // false otherwise, in which case status set.
  template <class Listener>
  void OnStringStart(Listener* cb, DecodeStatus* status) {
    // TODO(vasilvv): fail explicitly in case of truncation.
    remaining_ = static_cast<size_t>(length_decoder_.value());
    // Make callback so consumer knows what is coming.
    cb->OnStringStart(huffman_encoded_, remaining_);
  }

  // Passes the available portion of the string to the listener, and signals
  // the end of the string when it is reached. Returns kDecodeDone or
  // kDecodeInProgress as appropriate.
  template <class Listener>
  DecodeStatus DecodeString(DecodeBuffer* db, Listener* cb) {
    size_t len = std::min(remaining_, db->Remaining());
    if (len > 0) {
      cb->OnStringData(db->cursor(), len);
      db->AdvanceCursor(len);
      remaining_ -= len;
    }
    if (remaining_ == 0) {
      cb->OnStringEnd();
      return DecodeStatus::kDecodeDone;
    }
    state_ = kDecodingString;
    return DecodeStatus::kDecodeInProgress;
  }

  HpackVarintDecoder length_decoder_;

  // These fields are initialized just to keep ASAN happy about reading
  // them from DebugString().
  size_t remaining_ = 0;
  StringDecoderState state_ = kStartDecodingLength;
  bool huffman_encoded_ = false;
};

HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              const HpackStringDecoder& v);

}  // namespace http2
#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_STRING_DECODER_H_
