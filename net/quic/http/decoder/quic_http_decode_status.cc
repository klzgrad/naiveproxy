// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_decode_status.h"

#include "base/logging.h"
#include "net/quic/platform/api/quic_bug_tracker.h"

namespace net {

std::ostream& operator<<(std::ostream& out, QuicHttpDecodeStatus v) {
  switch (v) {
    case QuicHttpDecodeStatus::kDecodeDone:
      return out << "DecodeDone";
    case QuicHttpDecodeStatus::kDecodeInProgress:
      return out << "DecodeInProgress";
    case QuicHttpDecodeStatus::kDecodeError:
      return out << "DecodeError";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  QUIC_BUG << "Unknown QuicHttpDecodeStatus " << unknown;
  return out << "QuicHttpDecodeStatus(" << unknown << ")";
}

}  // namespace net
