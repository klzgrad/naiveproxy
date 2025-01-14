// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_CACHED_OBJECT_H_
#define QUICHE_QUIC_MOQT_MOQT_CACHED_OBJECT_H_

#include <memory>

#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace moqt {

// CachedObject is a version of PublishedObject with a reference counted
// payload.
struct CachedObject {
  FullSequence sequence;
  MoqtObjectStatus status;
  MoqtPriority publisher_priority;
  std::shared_ptr<quiche::QuicheMemSlice> payload;
};

// Transforms a CachedObject into a PublishedObject.
PublishedObject CachedObjectToPublishedObject(const CachedObject& object);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_CACHED_OBJECT_H_
