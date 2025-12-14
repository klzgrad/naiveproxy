// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_OBJECT_H_
#define QUICHE_QUIC_MOQT_MOQT_OBJECT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/quiche_mem_slice.h"

namespace moqt {

struct PublishedObjectMetadata {
  Location location;
  uint64_t subgroup;  // Equal to object_id for datagrams.
  std::string extensions;
  MoqtObjectStatus status;
  MoqtPriority publisher_priority;
  quic::QuicTime arrival_time = quic::QuicTime::Zero();
};

// PublishedObject is a description of an object that is sufficient to publish
// it on a given track.
struct PublishedObject {
  PublishedObjectMetadata metadata;
  quiche::QuicheMemSlice payload;
  bool fin_after_this = false;
};

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

#endif  // QUICHE_QUIC_MOQT_MOQT_OBJECT_H_
