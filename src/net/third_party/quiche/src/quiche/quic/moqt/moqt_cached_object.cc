// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_cached_object.h"

#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace moqt {

moqt::PublishedObject CachedObjectToPublishedObject(
    const CachedObject& object) {
  PublishedObject result;
  result.sequence = object.sequence;
  result.status = object.status;
  if (object.payload != nullptr && !object.payload->empty()) {
    result.payload = quiche::QuicheMemSlice(
        object.payload->data(), object.payload->length(),
        [retained_pointer = object.payload](const char*) {});
  }
  return result;
}

}  // namespace moqt
