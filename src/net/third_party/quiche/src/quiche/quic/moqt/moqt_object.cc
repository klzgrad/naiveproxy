// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_object.h"

#include <cstdint>

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_cord_utils.h"

namespace moqt {

bool CachedObject::Append(uint64_t offset, absl::string_view payload) {
  absl::MutexLock lock(mutex_);
  uint64_t total_length = payload_received_locked();
  if (offset > total_length) {
    QUICHE_BUG(cached_object_gap) << "Gap in bytes in CachedObject::Append";
    return false;
  }
  if (offset + payload.length() > metadata_.payload_length) {
    // This object is larger than the declared size.
    QUICHE_BUG(cached_object_too_large)
        << "Object is larger than the declared size";
    return false;
  }
  if (offset + payload.length() <= total_length) {
    return false;  // No new data.
  }
  payload_.Append(payload.substr(total_length - offset));
  return true;
}

PublishedObject CachedObject::ToPublishedObject(uint64_t offset) const {
  PublishedObject result;
  result.metadata = metadata();
  absl::MutexLock lock(mutex_);
  uint64_t total_length = payload_received_locked();
  quiche::CordToMemSlicesTo(payload_.Subcord(offset, total_length - offset),
                            result.payload);
  result.fin_after_this = fin_after_this_;
  return result;
}

bool CachedObject::OverlapIsEqual(uint64_t offset,
                                  absl::string_view payload) const {
  absl::MutexLock lock(mutex_);
  uint64_t total_length = payload_received_locked();
  if (offset >= total_length) {
    return true;
  }
  if (offset + payload.length() <= total_length) {
    return payload_.Subcord(offset, payload.length()) == payload;
  }
  return payload_.Subcord(offset, total_length - offset) ==
         payload.substr(0, total_length - offset);
}

}  // namespace moqt
