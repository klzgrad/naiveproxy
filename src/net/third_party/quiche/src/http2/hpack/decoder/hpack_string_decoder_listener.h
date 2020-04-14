// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_STRING_DECODER_LISTENER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_STRING_DECODER_LISTENER_H_

// Defines HpackStringDecoderListener which defines the methods required by an
// HpackStringDecoder. Also defines HpackStringDecoderVLoggingListener which
// logs before calling another HpackStringDecoderListener implementation.
// For now these are only used by tests, so placed in the test namespace.

#include <stddef.h>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {

// HpackStringDecoder methods require a listener that implements the methods
// below, but it is NOT necessary to extend this class because the methods
// are templates.
class QUICHE_EXPORT_PRIVATE HpackStringDecoderListener {
 public:
  virtual ~HpackStringDecoderListener() {}

  // Called at the start of decoding an HPACK string. The encoded length of the
  // string is |len| bytes, which may be zero. The string is Huffman encoded
  // if huffman_encoded is true, else it is plain text (i.e. the encoded length
  // is then the plain text length).
  virtual void OnStringStart(bool huffman_encoded, size_t len) = 0;

  // Called when some data is available, or once when the string length is zero
  // (to simplify the decoder, it doesn't have a special case for len==0).
  virtual void OnStringData(const char* data, size_t len) = 0;

  // Called after OnStringData has provided all of the encoded bytes of the
  // string.
  virtual void OnStringEnd() = 0;
};

class QUICHE_EXPORT_PRIVATE HpackStringDecoderVLoggingListener
    : public HpackStringDecoderListener {
 public:
  HpackStringDecoderVLoggingListener() : wrapped_(nullptr) {}
  explicit HpackStringDecoderVLoggingListener(
      HpackStringDecoderListener* wrapped)
      : wrapped_(wrapped) {}
  ~HpackStringDecoderVLoggingListener() override {}

  void OnStringStart(bool huffman_encoded, size_t len) override;
  void OnStringData(const char* data, size_t len) override;
  void OnStringEnd() override;

 private:
  HpackStringDecoderListener* const wrapped_;
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_STRING_DECODER_LISTENER_H_
