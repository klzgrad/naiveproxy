// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {
namespace test {

class MockMoqtSession : public MoqtSessionInterface {
 public:
  MOCK_METHOD(MoqtSessionCallbacks&, callbacks, (), (override));
  MOCK_METHOD(void, Error, (MoqtError code, absl::string_view error),
              (override));
  MOCK_METHOD(bool, Subscribe,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               const MessageParameters& parameters),
              (override));
  MOCK_METHOD(bool, SubscribeUpdate,
              (const FullTrackName&, const MessageParameters&,
               MoqtResponseCallback),
              (override));
  MOCK_METHOD(void, Unsubscribe, (const FullTrackName& name), (override));
  MOCK_METHOD(bool, Fetch,
              (const FullTrackName& name, FetchResponseCallback callback,
               Location start, uint64_t end_group,
               std::optional<uint64_t> end_object,
               MessageParameters parameters),
              (override));
  MOCK_METHOD(bool, RelativeJoiningFetch,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               uint64_t num_previous_groups, MessageParameters parameters),
              (override));
  MOCK_METHOD(bool, RelativeJoiningFetch,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               FetchResponseCallback callback, uint64_t num_previous_groups,
               MessageParameters parameters),
              (override));
  MOCK_METHOD(
      bool, PublishNamespace,
      (const TrackNamespace& track_namespace,
       const MessageParameters& parameters,
       MoqtResponseCallback response_callback,
       quiche::SingleUseCallback<void(MoqtRequestErrorInfo)> cancel_callback),
      (override));
  MOCK_METHOD(bool, PublishNamespaceUpdate,
              (const TrackNamespace& track_namespace,
               MessageParameters& parameters,
               MoqtResponseCallback response_callback),
              (override));
  MOCK_METHOD(bool, PublishNamespaceDone,
              (const TrackNamespace& track_namespace), (override));
  MOCK_METHOD(bool, PublishNamespaceCancel,
              (const TrackNamespace& track_namespace,
               RequestErrorCode error_code, absl::string_view error_reason),
              (override));
  MOCK_METHOD(std::unique_ptr<MoqtNamespaceTask>, SubscribeNamespace,
              (TrackNamespace&, SubscribeNamespaceOption,
               const MessageParameters&, MoqtResponseCallback),
              (override));

  quiche::QuicheWeakPtr<MoqtSessionInterface> GetWeakPtr() override {
    return weak_factory_.Create();
  }
  quiche::QuicheWeakPtrFactory<MoqtSessionInterface> weak_factory_{this};
};

}  // namespace test
}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_
