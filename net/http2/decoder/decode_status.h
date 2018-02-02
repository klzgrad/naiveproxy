// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_DECODE_STATUS_H_
#define NET_HTTP2_DECODER_DECODE_STATUS_H_

// Enum DecodeStatus is used to report the status of decoding of many
// types of HTTP/2 and HPACK objects.

#include <ostream>

#include "net/http2/platform/api/http2_export.h"

namespace net {

enum class DecodeStatus {
  // Decoding is done.
  kDecodeDone,

  // Decoder needs more input to be able to make progress.
  kDecodeInProgress,

  // Decoding failed (e.g. HPACK variable length integer is too large, or
  // an HTTP/2 frame has padding declared to be larger than the payload).
  kDecodeError,
};
HTTP2_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                              DecodeStatus v);

}  // namespace net

#endif  // NET_HTTP2_DECODER_DECODE_STATUS_H_
