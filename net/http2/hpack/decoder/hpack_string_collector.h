// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_HPACK_DECODER_HPACK_STRING_COLLECTOR_H_
#define NET_HTTP2_HPACK_DECODER_HPACK_STRING_COLLECTOR_H_

// Supports tests of decoding HPACK strings.

#include <stddef.h>

#include <iosfwd>

#include "net/http2/hpack/decoder/hpack_string_decoder_listener.h"
#include "net/http2/platform/api/http2_string.h"
#include "net/http2/platform/api/http2_string_piece.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

// Records the callbacks associated with a decoding a string; must
// call Clear() between decoding successive strings.
struct HpackStringCollector : public HpackStringDecoderListener {
  enum CollectorState {
    kGenesis,
    kStarted,
    kEnded,
  };

  HpackStringCollector();
  HpackStringCollector(const Http2String& str, bool huffman);

  void Clear();
  bool IsClear() const;
  bool IsInProgress() const;
  bool HasEnded() const;

  void OnStringStart(bool huffman, size_t length) override;
  void OnStringData(const char* data, size_t length) override;
  void OnStringEnd() override;

  ::testing::AssertionResult Collected(Http2StringPiece str,
                                       bool is_huffman_encoded) const;

  Http2String ToString() const;

  Http2String s;
  size_t len;
  bool huffman_encoded;
  CollectorState state;
};

bool operator==(const HpackStringCollector& a, const HpackStringCollector& b);

bool operator!=(const HpackStringCollector& a, const HpackStringCollector& b);

std::ostream& operator<<(std::ostream& out, const HpackStringCollector& v);

}  // namespace test
}  // namespace net

#endif  // NET_HTTP2_HPACK_DECODER_HPACK_STRING_COLLECTOR_H_
