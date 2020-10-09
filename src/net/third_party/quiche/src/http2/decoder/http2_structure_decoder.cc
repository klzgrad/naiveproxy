// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/http2_structure_decoder.h"

#include <algorithm>
#include <cstring>

#include "net/third_party/quiche/src/http2/platform/api/http2_bug_tracker.h"

namespace http2 {

// Below we have some defensive coding: if we somehow run off the end, don't
// overwrite lots of memory. Note that most of this decoder is not defensive
// against bugs in the decoder, only against malicious encoders, but since
// we're copying memory into a buffer here, let's make sure we don't allow a
// small mistake to grow larger. The decoder will get stuck if we hit the
// HTTP2_BUG conditions, but shouldn't corrupt memory.

uint32_t Http2StructureDecoder::IncompleteStart(DecodeBuffer* db,
                                                uint32_t target_size) {
  if (target_size > sizeof buffer_) {
    HTTP2_BUG << "target_size too large for buffer: " << target_size;
    return 0;
  }
  const uint32_t num_to_copy = db->MinLengthRemaining(target_size);
  memcpy(buffer_, db->cursor(), num_to_copy);
  offset_ = num_to_copy;
  db->AdvanceCursor(num_to_copy);
  return num_to_copy;
}

DecodeStatus Http2StructureDecoder::IncompleteStart(DecodeBuffer* db,
                                                    uint32_t* remaining_payload,
                                                    uint32_t target_size) {
  HTTP2_DVLOG(1) << "IncompleteStart@" << this
                 << ": *remaining_payload=" << *remaining_payload
                 << "; target_size=" << target_size
                 << "; db->Remaining=" << db->Remaining();
  *remaining_payload -=
      IncompleteStart(db, std::min(target_size, *remaining_payload));
  if (*remaining_payload > 0 && db->Empty()) {
    return DecodeStatus::kDecodeInProgress;
  }
  HTTP2_DVLOG(1) << "IncompleteStart: kDecodeError";
  return DecodeStatus::kDecodeError;
}

bool Http2StructureDecoder::ResumeFillingBuffer(DecodeBuffer* db,
                                                uint32_t target_size) {
  HTTP2_DVLOG(2) << "ResumeFillingBuffer@" << this
                 << ": target_size=" << target_size << "; offset_=" << offset_
                 << "; db->Remaining=" << db->Remaining();
  if (target_size < offset_) {
    HTTP2_BUG << "Already filled buffer_! target_size=" << target_size
              << "    offset_=" << offset_;
    return false;
  }
  const uint32_t needed = target_size - offset_;
  const uint32_t num_to_copy = db->MinLengthRemaining(needed);
  HTTP2_DVLOG(2) << "ResumeFillingBuffer num_to_copy=" << num_to_copy;
  memcpy(&buffer_[offset_], db->cursor(), num_to_copy);
  db->AdvanceCursor(num_to_copy);
  offset_ += num_to_copy;
  return needed == num_to_copy;
}

bool Http2StructureDecoder::ResumeFillingBuffer(DecodeBuffer* db,
                                                uint32_t* remaining_payload,
                                                uint32_t target_size) {
  HTTP2_DVLOG(2) << "ResumeFillingBuffer@" << this
                 << ": target_size=" << target_size << "; offset_=" << offset_
                 << "; *remaining_payload=" << *remaining_payload
                 << "; db->Remaining=" << db->Remaining();
  if (target_size < offset_) {
    HTTP2_BUG << "Already filled buffer_! target_size=" << target_size
              << "    offset_=" << offset_;
    return false;
  }
  const uint32_t needed = target_size - offset_;
  const uint32_t num_to_copy =
      db->MinLengthRemaining(std::min(needed, *remaining_payload));
  HTTP2_DVLOG(2) << "ResumeFillingBuffer num_to_copy=" << num_to_copy;
  memcpy(&buffer_[offset_], db->cursor(), num_to_copy);
  db->AdvanceCursor(num_to_copy);
  offset_ += num_to_copy;
  *remaining_payload -= num_to_copy;
  return needed == num_to_copy;
}

}  // namespace http2
