// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HUFFMAN_HPACK_HUFFMAN_ENCODER_H_
#define QUICHE_HTTP2_HPACK_HUFFMAN_HPACK_HUFFMAN_ENCODER_H_

// Functions supporting the encoding of strings using the HPACK-defined Huffman
// table.

#include <cstddef>  // For size_t
#include <string>

#include "net/third_party/quiche/src/http2/platform/api/http2_export.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_piece.h"

namespace http2 {

// Returns the size of the Huffman encoding of |plain|, which may be greater
// than plain.size(). Mostly present for testing.
HTTP2_EXPORT_PRIVATE size_t ExactHuffmanSize(Http2StringPiece plain);

// Returns the size of the Huffman encoding of |plain|, unless it is greater
// than or equal to plain.size(), in which case a value greater than or equal to
// plain.size() is returned. The advantage of this over ExactHuffmanSize is that
// it doesn't read as much of the input string in the event that the string is
// not compressible by HuffmanEncode (i.e. when the encoding is longer than the
// original string, it stops reading the input string as soon as it knows that).
HTTP2_EXPORT_PRIVATE size_t BoundedHuffmanSize(Http2StringPiece plain);

// Encode the plain text string |plain| with the Huffman encoding defined in
// the HPACK RFC, 7541.  |*huffman| does not have to be empty, it is cleared at
// the beginning of this function.  This allows reusing the same string object
// across multiple invocations.
HTTP2_EXPORT_PRIVATE void HuffmanEncode(Http2StringPiece plain,
                                        std::string* huffman);

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_HUFFMAN_HPACK_HUFFMAN_ENCODER_H_
