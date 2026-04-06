// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
#define QUICHE_QUIC_MOQT_MOQT_FRAMER_H_

#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
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
  MoqtFramer(bool using_webtrans) : using_webtrans_(using_webtrans) {}

  // Serialize functions. Takes structured data and serializes it into a
  // QuicheBuffer for delivery to the stream.

  // Serializes the header for an object, |previous_object_in_stream| is nullopt
  // if this is the first object in the stream, the object ID of the previous
  // one otherwise.
  quiche::QuicheBuffer SerializeObjectHeader(
      const MoqtObject& message, MoqtDataStreamType message_type,
      std::optional<PublishedObjectMetadata>& previous_object_in_stream);
  // Serializes both OBJECT and OBJECT_STATUS datagrams.
  quiche::QuicheBuffer SerializeObjectDatagram(const MoqtObject& message,
                                               absl::string_view payload,
                                               MoqtPriority default_priority);
  quiche::QuicheBuffer SerializeClientSetup(const MoqtClientSetup& message);
  quiche::QuicheBuffer SerializeServerSetup(const MoqtServerSetup& message);
  quiche::QuicheBuffer SerializeRequestOk(const MoqtRequestOk& message);
  quiche::QuicheBuffer SerializeRequestError(const MoqtRequestError& message);
  // Returns an empty buffer if there is an illegal combination of locations.
  quiche::QuicheBuffer SerializeSubscribe(
      const MoqtSubscribe& message,
      MoqtMessageType message_type = MoqtMessageType::kSubscribe);
  quiche::QuicheBuffer SerializeSubscribeOk(
      const MoqtSubscribeOk& message,
      MoqtMessageType message_type = MoqtMessageType::kSubscribeOk);
  quiche::QuicheBuffer SerializeUnsubscribe(const MoqtUnsubscribe& message);
  quiche::QuicheBuffer SerializePublishDone(const MoqtPublishDone& message);
  quiche::QuicheBuffer SerializeRequestUpdate(const MoqtRequestUpdate& message);
  quiche::QuicheBuffer SerializePublishNamespace(
      const MoqtPublishNamespace& message);
  quiche::QuicheBuffer SerializePublishNamespaceDone(
      const MoqtPublishNamespaceDone& message);
  quiche::QuicheBuffer SerializeNamespace(const MoqtNamespace& message);
  quiche::QuicheBuffer SerializeNamespaceDone(const MoqtNamespaceDone& message);
  quiche::QuicheBuffer SerializePublishNamespaceCancel(
      const MoqtPublishNamespaceCancel& message);
  quiche::QuicheBuffer SerializeTrackStatus(const MoqtTrackStatus& message);
  quiche::QuicheBuffer SerializeGoAway(const MoqtGoAway& message);
  quiche::QuicheBuffer SerializeSubscribeNamespace(
      const MoqtSubscribeNamespace& message);
  quiche::QuicheBuffer SerializeMaxRequestId(const MoqtMaxRequestId& message);
  quiche::QuicheBuffer SerializeFetch(const MoqtFetch& message);
  quiche::QuicheBuffer SerializeFetchCancel(const MoqtFetchCancel& message);
  quiche::QuicheBuffer SerializeFetchOk(const MoqtFetchOk& message);
  quiche::QuicheBuffer SerializeRequestsBlocked(
      const MoqtRequestsBlocked& message);
  quiche::QuicheBuffer SerializePublish(const MoqtPublish& message);
  quiche::QuicheBuffer SerializePublishOk(const MoqtPublishOk& message);
  quiche::QuicheBuffer SerializeObjectAck(const MoqtObjectAck& message);

  bool using_webtrans() const { return using_webtrans_; }

 private:
  // Returns true if the parameters are valid for the message type.
  bool FillAndValidateSetupParameters(MoqtMessageType message_type,
                                      const SetupParameters& parameters,
                                      KeyValuePairList& out);
  // Returns true if the metadata is internally consistent.
  static bool ValidateObjectMetadata(const MoqtObject& object);
  const bool using_webtrans_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_FRAMER_H_
