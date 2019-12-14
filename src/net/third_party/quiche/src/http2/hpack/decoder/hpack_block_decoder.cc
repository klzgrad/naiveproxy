// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_block_decoder.h"

#include <cstdint>

#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"

namespace http2 {

DecodeStatus HpackBlockDecoder::Decode(DecodeBuffer* db) {
  if (!before_entry_) {
    HTTP2_DVLOG(2) << "HpackBlockDecoder::Decode resume entry, db->Remaining="
                   << db->Remaining();
    DecodeStatus status = entry_decoder_.Resume(db, listener_);
    switch (status) {
      case DecodeStatus::kDecodeDone:
        before_entry_ = true;
        break;
      case DecodeStatus::kDecodeInProgress:
        DCHECK_EQ(0u, db->Remaining());
        return DecodeStatus::kDecodeInProgress;
      case DecodeStatus::kDecodeError:
        return DecodeStatus::kDecodeError;
    }
  }
  DCHECK(before_entry_);
  while (db->HasData()) {
    HTTP2_DVLOG(2) << "HpackBlockDecoder::Decode start entry, db->Remaining="
                   << db->Remaining();
    DecodeStatus status = entry_decoder_.Start(db, listener_);
    switch (status) {
      case DecodeStatus::kDecodeDone:
        continue;
      case DecodeStatus::kDecodeInProgress:
        DCHECK_EQ(0u, db->Remaining());
        before_entry_ = false;
        return DecodeStatus::kDecodeInProgress;
      case DecodeStatus::kDecodeError:
        return DecodeStatus::kDecodeError;
    }
    DCHECK(false);
  }
  DCHECK(before_entry_);
  return DecodeStatus::kDecodeDone;
}

std::string HpackBlockDecoder::DebugString() const {
  return Http2StrCat("HpackBlockDecoder(", entry_decoder_.DebugString(),
                     ", listener@",
                     Http2Hex(reinterpret_cast<intptr_t>(listener_)),
                     (before_entry_ ? ", between entries)" : ", in an entry)"));
}

std::ostream& operator<<(std::ostream& out, const HpackBlockDecoder& v) {
  return out << v.DebugString();
}

}  // namespace http2
