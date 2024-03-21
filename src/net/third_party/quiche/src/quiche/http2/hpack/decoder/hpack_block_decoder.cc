// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_block_decoder.h"

#include <cstdint>

#include "absl/strings/str_cat.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

DecodeStatus HpackBlockDecoder::Decode(DecodeBuffer* db) {
  if (!before_entry_) {
    QUICHE_DVLOG(2) << "HpackBlockDecoder::Decode resume entry, db->Remaining="
                    << db->Remaining();
    DecodeStatus status = entry_decoder_.Resume(db, listener_);
    switch (status) {
      case DecodeStatus::kDecodeDone:
        before_entry_ = true;
        break;
      case DecodeStatus::kDecodeInProgress:
        QUICHE_DCHECK_EQ(0u, db->Remaining());
        return DecodeStatus::kDecodeInProgress;
      case DecodeStatus::kDecodeError:
        QUICHE_CODE_COUNT_N(decompress_failure_3, 1, 23);
        return DecodeStatus::kDecodeError;
    }
  }
  QUICHE_DCHECK(before_entry_);
  while (db->HasData()) {
    QUICHE_DVLOG(2) << "HpackBlockDecoder::Decode start entry, db->Remaining="
                    << db->Remaining();
    DecodeStatus status = entry_decoder_.Start(db, listener_);
    switch (status) {
      case DecodeStatus::kDecodeDone:
        continue;
      case DecodeStatus::kDecodeInProgress:
        QUICHE_DCHECK_EQ(0u, db->Remaining());
        before_entry_ = false;
        return DecodeStatus::kDecodeInProgress;
      case DecodeStatus::kDecodeError:
        QUICHE_CODE_COUNT_N(decompress_failure_3, 2, 23);
        return DecodeStatus::kDecodeError;
    }
    QUICHE_DCHECK(false);
  }
  QUICHE_DCHECK(before_entry_);
  return DecodeStatus::kDecodeDone;
}

std::string HpackBlockDecoder::DebugString() const {
  return absl::StrCat(
      "HpackBlockDecoder(", entry_decoder_.DebugString(), ", listener@",
      absl::Hex(reinterpret_cast<intptr_t>(listener_)),
      (before_entry_ ? ", between entries)" : ", in an entry)"));
}

std::ostream& operator<<(std::ostream& out, const HpackBlockDecoder& v) {
  return out << v.DebugString();
}

}  // namespace http2
