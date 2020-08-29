// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_STRING_BUFFER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_STRING_BUFFER_H_

// HpackDecoderStringBuffer helps an HPACK decoder to avoid copies of a string
// literal (name or value) except when necessary (e.g. when split across two
// or more HPACK block fragments).

#include <stddef.h>

#include <ostream>
#include <string>

#include "net/third_party/quiche/src/http2/hpack/huffman/hpack_huffman_decoder.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {

class QUICHE_EXPORT_PRIVATE HpackDecoderStringBuffer {
 public:
  enum class State : uint8_t { RESET, COLLECTING, COMPLETE };
  enum class Backing : uint8_t { RESET, UNBUFFERED, BUFFERED, STATIC };

  HpackDecoderStringBuffer();
  ~HpackDecoderStringBuffer();

  HpackDecoderStringBuffer(const HpackDecoderStringBuffer&) = delete;
  HpackDecoderStringBuffer& operator=(const HpackDecoderStringBuffer&) = delete;

  void Reset();
  void Set(quiche::QuicheStringPiece value, bool is_static);

  // Note that for Huffman encoded strings the length of the string after
  // decoding may be larger (expected), the same or even smaller; the latter
  // are unlikely, but possible if the encoder makes odd choices.
  void OnStart(bool huffman_encoded, size_t len);
  bool OnData(const char* data, size_t len);
  bool OnEnd();
  void BufferStringIfUnbuffered();
  bool IsBuffered() const;
  size_t BufferedLength() const;

  // Accessors for the completely collected string (i.e. Set or OnEnd has just
  // been called, and no reset of the state has occurred).

  // Returns a QuicheStringPiece pointing to the backing store for the string,
  // either the internal buffer or the original transport buffer (e.g. for a
  // literal value that wasn't Huffman encoded, and that wasn't split across
  // transport buffers).
  quiche::QuicheStringPiece str() const;

  // Returns the completely collected string by value, using std::move in an
  // effort to avoid unnecessary copies. ReleaseString() must not be called
  // unless the string has been buffered (to avoid forcing a potentially
  // unnecessary copy). ReleaseString() also resets the instance so that it can
  // be used to collect another string.
  std::string ReleaseString();

  State state_for_testing() const { return state_; }
  Backing backing_for_testing() const { return backing_; }
  void OutputDebugStringTo(std::ostream& out) const;

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  // Storage for the string being buffered, if buffering is necessary
  // (e.g. if Huffman encoded, buffer_ is storage for the decoded string).
  std::string buffer_;

  // The QuicheStringPiece to be returned by HpackDecoderStringBuffer::str(). If
  // a string has been collected, but not buffered, value_ points to that
  // string.
  quiche::QuicheStringPiece value_;

  // The decoder to use if the string is Huffman encoded.
  HpackHuffmanDecoder decoder_;

  // Count of bytes not yet passed to OnData.
  size_t remaining_len_;

  // Is the HPACK string Huffman encoded?
  bool is_huffman_encoded_;

  // State of the string decoding process.
  State state_;

  // Where is the string stored?
  Backing backing_;
};

QUICHE_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& out,
    const HpackDecoderStringBuffer& v);

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_STRING_BUFFER_H_
