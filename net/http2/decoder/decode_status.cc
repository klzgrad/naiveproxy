// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/decoder/decode_status.h"

#include "base/logging.h"
#include "net/http2/tools/http2_bug_tracker.h"

namespace net {

std::ostream& operator<<(std::ostream& out, DecodeStatus v) {
  switch (v) {
    case DecodeStatus::kDecodeDone:
      return out << "DecodeDone";
    case DecodeStatus::kDecodeInProgress:
      return out << "DecodeInProgress";
    case DecodeStatus::kDecodeError:
      return out << "DecodeError";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  HTTP2_BUG << "Unknown DecodeStatus " << unknown;
  return out << "DecodeStatus(" << unknown << ")";
}

}  // namespace net
