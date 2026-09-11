// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_REQUEST_UPDATE_QUEUE_H_
#define QUICHE_QUIC_MOQT_MOQT_REQUEST_UPDATE_QUEUE_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_circular_deque.h"

namespace moqt {

// MoqtRequestUpdateQueue manages pending outgoing REQUEST_UPDATE messages on a
// bidirectional MoQT stream, matching responses (REQUEST_OK and REQUEST_ERROR)
// to queued callbacks and parameter updates.
class QUICHE_EXPORT MoqtRequestUpdateQueue {
 public:
  MoqtRequestUpdateQueue() = default;

  // Enqueues parameters and callback for an outgoing REQUEST_UPDATE message.
  void Enqueue(const MessageParameters& parameters,
               MoqtResponseCallback callback);

  // Returns true if there are no pending update callbacks.
  bool empty() const { return pending_responses_.empty(); }

  // Returns the MessageParameters for the oldest pending update without
  // dequeuing, or NotFoundError if there are no pending updates.
  absl::StatusOr<MessageParameters> NextParameters() const;

  absl::Status OnControlMessage(const MoqtRequestOk& message);
  absl::Status OnControlMessage(const MoqtRequestError& message);

  // Clears all pending responses and updates without invoking callbacks.
  void Clear() {
    pending_responses_.clear();
    pending_updates_.clear();
  }

 private:
  quiche::QuicheCircularDeque<MoqtResponseCallback> pending_responses_;
  quiche::QuicheCircularDeque<MessageParameters> pending_updates_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_REQUEST_UPDATE_QUEUE_H_
