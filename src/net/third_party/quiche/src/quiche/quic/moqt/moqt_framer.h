// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
#define QUICHE_QUIC_MOQT_MOQT_FRAMER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace moqt {

// Serialize structured message data into a wire image. When the message format
// is different per |perspective| or |using_webtrans|, it will omit unnecessary
// fields. However, it does not enforce the presence of parameters that are
// required for a particular mode.
//
// There can be one instance of this per session. This framer does not enforce
// that these Serialize() calls are made in a logical order, as they can be on
// different streams.
class QUICHE_EXPORT MoqtFramer {
 public:
  MoqtFramer(quiche::QuicheBufferAllocator* allocator,
             quic::Perspective perspective, bool using_webtrans)
      : allocator_(allocator),
        perspective_(perspective),
        using_webtrans_(using_webtrans) {}

  // Serialize functions. Takes structured data and serializes it into a
  // QuicheBuffer for delivery to the stream.

  // SerializeObject also takes a payload. |known_payload_size| is used in
  // encoding the message length. If zero, the message length as also encoded as
  // zero to indicate the message ends with the stream. If nonzero, and too
  // small to fit the varints and the provided payload, returns an empty buffer.
  quiche::QuicheBuffer SerializeObject(const MoqtObject& message,
                                       absl::string_view payload,
                                       size_t known_payload_size);
  // Build a buffer for additional payload data.
  quiche::QuicheBuffer SerializeObjectPayload(absl::string_view payload);
  quiche::QuicheBuffer SerializeSetup(const MoqtSetup& message);
  quiche::QuicheBuffer SerializeSubscribeRequest(
      const MoqtSubscribeRequest& message);
  quiche::QuicheBuffer SerializeSubscribeOk(const MoqtSubscribeOk& message);
  quiche::QuicheBuffer SerializeSubscribeError(
      const MoqtSubscribeError& message);
  quiche::QuicheBuffer SerializeUnsubscribe(const MoqtUnsubscribe& message);
  quiche::QuicheBuffer SerializeAnnounce(const MoqtAnnounce& message);
  quiche::QuicheBuffer SerializeAnnounceOk(const MoqtAnnounceOk& message);
  quiche::QuicheBuffer SerializeAnnounceError(const MoqtAnnounceError& message);
  quiche::QuicheBuffer SerializeUnannounce(const MoqtUnannounce& message);
  quiche::QuicheBuffer SerializeGoAway();

 private:
  quiche::QuicheBufferAllocator* allocator_;
  quic::Perspective perspective_;
  bool using_webtrans_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
