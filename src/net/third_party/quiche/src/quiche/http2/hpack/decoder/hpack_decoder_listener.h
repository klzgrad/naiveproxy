// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines HpackDecoderListener, the base class of listeners for HTTP header
// lists decoded from an HPACK block.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_LISTENER_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_LISTENER_H_

#include "absl/strings/string_view.h"
#include "quiche/http2/hpack/http2_hpack_constants.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {

class QUICHE_EXPORT HpackDecoderListener {
 public:
  HpackDecoderListener();
  virtual ~HpackDecoderListener();

  // OnHeaderListStart is called at the start of decoding an HPACK block into
  // an HTTP/2 header list. Will only be called once per block, even if it
  // extends into CONTINUATION frames.
  virtual void OnHeaderListStart() = 0;

  // Called for each header name-value pair that is decoded, in the order they
  // appear in the HPACK block. Multiple values for a given key will be emitted
  // as multiple calls to OnHeader.
  virtual void OnHeader(absl::string_view name, absl::string_view value) = 0;

  // OnHeaderListEnd is called after successfully decoding an HPACK block into
  // an HTTP/2 header list. Will only be called once per block, even if it
  // extends into CONTINUATION frames.
  virtual void OnHeaderListEnd() = 0;

  // OnHeaderErrorDetected is called if an error is detected while decoding.
  // error_message may be used in a GOAWAY frame as the Opaque Data.
  virtual void OnHeaderErrorDetected(absl::string_view error_message) = 0;
};

// A no-op implementation of HpackDecoderListener, useful for ignoring
// callbacks once an error is detected.
class QUICHE_EXPORT HpackDecoderNoOpListener : public HpackDecoderListener {
 public:
  HpackDecoderNoOpListener();
  ~HpackDecoderNoOpListener() override;

  void OnHeaderListStart() override;
  void OnHeader(absl::string_view name, absl::string_view value) override;
  void OnHeaderListEnd() override;
  void OnHeaderErrorDetected(absl::string_view error_message) override;

  // Returns a listener that ignores all the calls.
  static HpackDecoderNoOpListener* NoOpListener();
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODER_LISTENER_H_
