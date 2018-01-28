// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/decoder/hpack_block_decoder.h"

#include <stdint.h>

#include <sstream>

#include "base/logging.h"
#include "net/http2/platform/api/http2_string_utils.h"

namespace net {

DecodeStatus HpackBlockDecoder::Decode(DecodeBuffer* db) {
  if (!before_entry_) {
    DVLOG(2) << "HpackBlockDecoder::Decode resume entry, db->Remaining="
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
    DVLOG(2) << "HpackBlockDecoder::Decode start entry, db->Remaining="
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

Http2String HpackBlockDecoder::DebugString() const {
  return Http2StrCat("HpackBlockDecoder(", entry_decoder_.DebugString(),
                     ", listener@", std::hex,
                     reinterpret_cast<intptr_t>(listener_),
                     (before_entry_ ? ", between entries)" : ", in an entry)"));
}

std::ostream& operator<<(std::ostream& out, const HpackBlockDecoder& v) {
  return out << v.DebugString();
}

}  // namespace net
