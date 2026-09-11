// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_PUBLISH_NAMESPACE_STREAM_H_
#define QUICHE_QUIC_MOQT_MOQT_PUBLISH_NAMESPACE_STREAM_H_

#include <cstdint>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

using AddPublishNamespaceCallback =
    quiche::SingleUseCallback<bool(const TrackNamespace&, MoqtBidiStreamBase*)>;
using RemovePublishNamespaceCallback =
    quiche::SingleUseCallback<void(const TrackNamespace&)>;

// This class will be owned by the webtransport stream.
class MoqtPublishNamespaceRequestStream : public MoqtBidiStreamBase {
 public:
  // Assumes the caller will send or queue the PUBLISH_NAMESPACE.
  MoqtPublishNamespaceRequestStream(
      const TrackNamespace& prefix, const MessageParameters& parameters,
      MoqtFramer* framer, const MoqtControlMessageParser& message_parser,
      uint64_t request_id, RemovePublishNamespaceCallback remove_callback,
      SessionErrorCallback session_error_callback,
      MoqtResponseCallback response_callback)
      : MoqtBidiStreamBase(framer, message_parser,
                           std::move(session_error_callback)),
        request_id_(request_id),
        remove_callback_(std::move(remove_callback)),
        response_callback_(std::move(response_callback)),
        prefix_(prefix),
        parameters_(parameters) {}
  ~MoqtPublishNamespaceRequestStream() { Detach(); }

  // MoqtBidiStreamBase overrides.
  void OnStreamBound() override;
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;
  absl::Status OnControlMessage(const MoqtRequestOk& message);
  absl::Status OnControlMessage(const MoqtRequestError& message);

  void Detach() override;

 private:
  const uint64_t request_id_;
  RemovePublishNamespaceCallback remove_callback_;
  MoqtResponseCallback response_callback_;
  const TrackNamespace prefix_;
  MessageParameters parameters_;
};

class MoqtPublishNamespaceResponseStream : public MoqtBidiStreamBase {
 public:
  // Constructor for the publisher side.
  MoqtPublishNamespaceResponseStream(
      MoqtFramer* framer, const MoqtControlMessageParser& message_parser,
      AddPublishNamespaceCallback add_callback,
      RemovePublishNamespaceCallback remove_callback,
      SessionErrorCallback session_error_callback,
      MoqtIncomingPublishNamespaceCallback application)
      : MoqtBidiStreamBase(framer, message_parser,
                           std::move(session_error_callback)),
        add_callback_(std::move(add_callback)),
        remove_callback_(std::move(remove_callback)),
        application_(std::move(application)),
        weak_ptr_factory_(this) {}
  ~MoqtPublishNamespaceResponseStream() { Detach(); }

  void OnStreamBound() override {
    // TODO(martinduke): Set the priority for this stream.
  }
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;
  absl::Status OnControlMessage(const MoqtPublishNamespace& message);
  absl::Status OnControlMessage(const MoqtRequestUpdate& message);

  void Detach() override;

 private:
  uint64_t request_id_;
  std::optional<TrackNamespace> prefix_;
  AddPublishNamespaceCallback add_callback_;
  RemovePublishNamespaceCallback remove_callback_;
  MoqtIncomingPublishNamespaceCallback application_;
  quiche::QuicheWeakPtrFactory<MoqtPublishNamespaceResponseStream>
      weak_ptr_factory_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_PUBLISH_NAMESPACE_STREAM_H_
