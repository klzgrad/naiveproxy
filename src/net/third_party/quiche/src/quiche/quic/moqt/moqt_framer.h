// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
#define QUICHE_QUIC_MOQT_MOQT_FRAMER_H_

#include <cstddef>
#include <optional>

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
  MoqtFramer(quiche::QuicheBufferAllocator* allocator, bool using_webtrans)
      : allocator_(allocator), using_webtrans_(using_webtrans) {}

  // Serialize functions. Takes structured data and serializes it into a
  // QuicheBuffer for delivery to the stream.

  // Serializes the header for an object, including the appropriate stream
  // header if `is_first_in_stream` is set to true.
  quiche::QuicheBuffer SerializeObjectHeader(const MoqtObject& message,
                                             bool is_first_in_stream);
  quiche::QuicheBuffer SerializeObjectDatagram(const MoqtObject& message,
                                               absl::string_view payload);
  quiche::QuicheBuffer SerializeClientSetup(const MoqtClientSetup& message);
  quiche::QuicheBuffer SerializeServerSetup(const MoqtServerSetup& message);
  // Returns an empty buffer if there is an illegal combination of locations.
  quiche::QuicheBuffer SerializeSubscribe(const MoqtSubscribe& message);
  quiche::QuicheBuffer SerializeSubscribeOk(const MoqtSubscribeOk& message);
  quiche::QuicheBuffer SerializeSubscribeError(
      const MoqtSubscribeError& message);
  quiche::QuicheBuffer SerializeUnsubscribe(const MoqtUnsubscribe& message);
  quiche::QuicheBuffer SerializeSubscribeDone(const MoqtSubscribeDone& message);
  quiche::QuicheBuffer SerializeAnnounce(const MoqtAnnounce& message);
  quiche::QuicheBuffer SerializeAnnounceOk(const MoqtAnnounceOk& message);
  quiche::QuicheBuffer SerializeAnnounceError(const MoqtAnnounceError& message);
  quiche::QuicheBuffer SerializeUnannounce(const MoqtUnannounce& message);
  quiche::QuicheBuffer SerializeGoAway(const MoqtGoAway& message);

 private:
  quiche::QuicheBufferAllocator* allocator_;
  bool using_webtrans_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
