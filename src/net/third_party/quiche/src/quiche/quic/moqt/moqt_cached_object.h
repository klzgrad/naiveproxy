// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_CACHED_OBJECT_H_
#define QUICHE_QUIC_MOQT_MOQT_CACHED_OBJECT_H_

#include <memory>

#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/common/quiche_mem_slice.h"

namespace moqt {

// CachedObject is a version of PublishedObject with a reference counted
// payload.
struct CachedObject {
  PublishedObjectMetadata metadata;
  std::shared_ptr<quiche::QuicheMemSlice> payload;
  bool fin_after_this;  // This is the last object before FIN.
};

// Transforms a CachedObject into a PublishedObject.
PublishedObject CachedObjectToPublishedObject(const CachedObject& object);

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_CACHED_OBJECT_H_
