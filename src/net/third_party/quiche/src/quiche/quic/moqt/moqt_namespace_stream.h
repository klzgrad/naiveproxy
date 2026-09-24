// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_NAMESPACE_STREAM_H_
#define QUICHE_QUIC_MOQT_MOQT_NAMESPACE_STREAM_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
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
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

using AddPrefixCallback =
    quiche::SingleUseCallback<bool(const TrackNamespace&)>;
using RemovePrefixCallback =
    quiche::SingleUseCallback<void(const TrackNamespace&)>;

// This class will be owned by the webtransport stream.
class MoqtSubscribeNamespaceRequestStream : public MoqtBidiStreamBase {
 public:
  // Assumes the caller will send or queue the SUBSCRIBE_NAMESPACE.
  MoqtSubscribeNamespaceRequestStream(
      MoqtFramer* framer, const MoqtControlMessageParser& message_parser,
      uint64_t request_id, RemovePrefixCallback remove_callback,
      SessionErrorCallback session_error_callback,
      MoqtResponseCallback response_callback)
      : MoqtBidiStreamBase(framer, message_parser,
                           std::move(session_error_callback)),
        request_id_(request_id),
        remove_callback_(std::move(remove_callback)),
        response_callback_(std::move(response_callback)) {}
  ~MoqtSubscribeNamespaceRequestStream();

  // MoqtBidiStreamBase overrides.
  void OnStreamBound() override;
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;
  absl::Status OnControlMessage(const MoqtRequestOk& message);
  absl::Status OnControlMessage(const MoqtRequestError& message);
  absl::Status OnControlMessage(const MoqtNamespace& message);
  absl::Status OnControlMessage(const MoqtNamespaceDone& message);

  // Send the prefix now so it is only stored in one place (the task).
  std::unique_ptr<MoqtNamespaceTask> CreateTask(const TrackNamespace& prefix);

  void Detach() override {
    if (remove_callback_ == nullptr) {
      return;
    }
    NamespaceTask* task = task_.GetIfAvailable();
    // CreateTask() should be called before Detach() can be. If the task is
    // then destroyed, the destructor should indirectly call this. Either way,
    // the task should not be null.
    QUICHE_DCHECK(task != nullptr);
    if (task != nullptr) {
      RemovePrefixCallback callback = std::move(remove_callback_);
      remove_callback_ = nullptr;
      std::move(callback)(task->prefix());
    }
  }

 private:
  // The class that will be passed to the application to consume namespace
  // information. Owned by the application.
  class NamespaceTask : public MoqtNamespaceTask {
   public:
    NamespaceTask(MoqtSubscribeNamespaceRequestStream* absl_nonnull state,
                  const TrackNamespace& prefix)
        : MoqtNamespaceTask(),
          prefix_(prefix),
          state_(state),
          next_request_id_(state->request_id_ + 2),
          weak_ptr_factory_(this) {}
    ~NamespaceTask() override;

    void SetObjectsAvailableCallback(ObjectsAvailableCallback
                                     absl_nullable callback) override;

    // MoqtNamespaceTask methods. A return value of kEof implies
    // NAMESPACE_DONE for all outstanding namespaces.
    GetNextResult GetNextSuffix(TrackNamespace& suffix,
                                TransactionType& type) override;
    std::optional<webtransport::StreamErrorCode> GetStatus() override {
      return error_;
    }
    const TrackNamespace& prefix() override { return prefix_; }
    void Update(const MessageParameters& parameters,
                MoqtResponseCallback response_callback) override;

    // Queues a suffix corresponding to a NAMESPACE (if |type| is kAdd) or a
    // NAMESPACE_DONE (if |type| is kDelete).
    void AddPendingSuffix(TrackNamespace suffix, TransactionType type);
    // The stream is closed, so no more NAMESPACE messages are forthcoming.
    // This is an implicit NAMESPACE_DONE for all published namespaces.
    void DeclareEof();
    MoqtResponseCallback GetResponseCallback(uint64_t request_id);
    quiche::QuicheWeakPtr<NamespaceTask> GetWeakPtr() {
      return weak_ptr_factory_.Create();
    }

   private:
    struct PendingSuffix {
      TrackNamespace suffix;
      TransactionType type;
    };

    static constexpr size_t kMaxPendingSuffixes = 100;
    const TrackNamespace prefix_;
    // Must be nonnull initially, will be nullptr if the stream is closed.
    MoqtSubscribeNamespaceRequestStream* state_;
    quiche::QuicheCircularDeque<PendingSuffix> pending_suffixes_;
    ObjectsAvailableCallback absl_nullable callback_ = nullptr;
    std::optional<webtransport::StreamErrorCode> error_;
    bool eof_ = false;
    uint64_t next_request_id_;
    absl::flat_hash_map<uint64_t, MoqtResponseCallback> pending_updates_;
    // Must be last.
    quiche::QuicheWeakPtrFactory<NamespaceTask> weak_ptr_factory_;
  };

  const uint64_t request_id_;
  RemovePrefixCallback remove_callback_;
  MoqtResponseCallback response_callback_;
  absl::flat_hash_set<TrackNamespace> published_suffixes_;
  quiche::QuicheWeakPtr<NamespaceTask> task_;
};

class MoqtSubscribeNamespaceResponseStream : public MoqtBidiStreamBase {
 public:
  // Constructor for the publisher side.
  MoqtSubscribeNamespaceResponseStream(
      MoqtFramer* framer, const MoqtControlMessageParser& message_parser,
      AddPrefixCallback add_callback, RemovePrefixCallback remove_callback,
      SessionErrorCallback session_error_callback,
      MoqtIncomingSubscribeNamespaceCallback& application);
  ~MoqtSubscribeNamespaceResponseStream() { Detach(); }

  void OnStreamBound() override {
    // TODO(martinduke): Set the priority for this stream.
  }
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;
  absl::Status OnControlMessage(const MoqtSubscribeNamespace& message);
  absl::Status OnControlMessage(const MoqtRequestUpdate& message);

  void Detach() override;

 private:
  void ProcessNamespaces();
  MoqtResponseCallback ResponseCallback(uint64_t request_id);

  uint64_t request_id_;
  TrackNamespace prefix_;
  AddPrefixCallback add_callback_;
  RemovePrefixCallback remove_callback_;
  MoqtIncomingSubscribeNamespaceCallback& application_;
  std::unique_ptr<MoqtNamespaceTask> task_;
  absl::flat_hash_set<TrackNamespace> published_suffixes_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_NAMESPACE_STREAM_H_
