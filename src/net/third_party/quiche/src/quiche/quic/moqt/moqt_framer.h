// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
#define QUICHE_QUIC_MOQT_MOQT_FRAMER_H_

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
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

  // Serializes the header for an object, |previous_object_in_stream| is nullopt
  // if this is the first object in the stream, the object ID of the previous
  // one otherwise.
  quiche::QuicheBuffer SerializeObjectHeader(
      const MoqtObject& message, MoqtDataStreamType message_type,
      std::optional<uint64_t> previous_object_in_stream);
  // Serializes both OBJECT and OBJECT_STATUS datagrams.
  quiche::QuicheBuffer SerializeObjectDatagram(const MoqtObject& message,
                                               absl::string_view payload);
  quiche::QuicheBuffer SerializeClientSetup(const MoqtClientSetup& message);
  quiche::QuicheBuffer SerializeServerSetup(const MoqtServerSetup& message);
  // Returns an empty buffer if there is an illegal combination of locations.
  quiche::QuicheBuffer SerializeSubscribe(
      const MoqtSubscribe& message,
      MoqtMessageType message_type = MoqtMessageType::kSubscribe);
  quiche::QuicheBuffer SerializeSubscribeOk(
      const MoqtSubscribeOk& message,
      MoqtMessageType message_type = MoqtMessageType::kSubscribeOk);
  quiche::QuicheBuffer SerializeSubscribeError(
      const MoqtSubscribeError& message,
      MoqtMessageType message_type = MoqtMessageType::kSubscribeError);
  quiche::QuicheBuffer SerializeUnsubscribe(const MoqtUnsubscribe& message);
  quiche::QuicheBuffer SerializePublishDone(const MoqtPublishDone& message);
  quiche::QuicheBuffer SerializeSubscribeUpdate(
      const MoqtSubscribeUpdate& message);
  quiche::QuicheBuffer SerializePublishNamespace(
      const MoqtPublishNamespace& message);
  quiche::QuicheBuffer SerializePublishNamespaceOk(
      const MoqtPublishNamespaceOk& message);
  quiche::QuicheBuffer SerializePublishNamespaceError(
      const MoqtPublishNamespaceError& message);
  quiche::QuicheBuffer SerializePublishNamespaceDone(
      const MoqtPublishNamespaceDone& message);
  quiche::QuicheBuffer SerializePublishNamespaceCancel(
      const MoqtPublishNamespaceCancel& message);
  quiche::QuicheBuffer SerializeTrackStatus(const MoqtTrackStatus& message);
  quiche::QuicheBuffer SerializeTrackStatusOk(const MoqtTrackStatusOk& message);
  quiche::QuicheBuffer SerializeTrackStatusError(
      const MoqtTrackStatusError& message);
  quiche::QuicheBuffer SerializeGoAway(const MoqtGoAway& message);
  quiche::QuicheBuffer SerializeSubscribeNamespace(
      const MoqtSubscribeNamespace& message);
  quiche::QuicheBuffer SerializeSubscribeNamespaceOk(
      const MoqtSubscribeNamespaceOk& message);
  quiche::QuicheBuffer SerializeSubscribeNamespaceError(
      const MoqtSubscribeNamespaceError& message);
  quiche::QuicheBuffer SerializeUnsubscribeNamespace(
      const MoqtUnsubscribeNamespace& message);
  quiche::QuicheBuffer SerializeMaxRequestId(const MoqtMaxRequestId& message);
  quiche::QuicheBuffer SerializeFetch(const MoqtFetch& message);
  quiche::QuicheBuffer SerializeFetchCancel(const MoqtFetchCancel& message);
  quiche::QuicheBuffer SerializeFetchOk(const MoqtFetchOk& message);
  quiche::QuicheBuffer SerializeFetchError(const MoqtFetchError& message);
  quiche::QuicheBuffer SerializeRequestsBlocked(
      const MoqtRequestsBlocked& message);
  quiche::QuicheBuffer SerializePublish(const MoqtPublish& message);
  quiche::QuicheBuffer SerializePublishOk(const MoqtPublishOk& message);
  quiche::QuicheBuffer SerializePublishError(const MoqtPublishError& message);
  quiche::QuicheBuffer SerializeObjectAck(const MoqtObjectAck& message);

 private:
  // Returns true if the metadata is internally consistent.
  static bool ValidateObjectMetadata(const MoqtObject& object,
                                     bool is_datagram);

  quiche::QuicheBufferAllocator* allocator_;
  bool using_webtrans_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
