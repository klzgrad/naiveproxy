// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

inline constexpr absl::string_view kDraft16 = "moqt-16";
inline constexpr absl::string_view kDefaultMoqtVersion = kDraft16;
inline constexpr absl::string_view kUnrecognizedVersionForTests = "moqt-15";

inline constexpr absl::string_view kImplementationName =
    "Google QUICHE MOQT draft 16";
inline constexpr uint64_t kDefaultInitialMaxRequestId = 100;
struct QUICHE_EXPORT MoqtSessionParameters {
  // TODO: support multiple versions.
  MoqtSessionParameters() = default;
  explicit MoqtSessionParameters(quic::Perspective perspective)
      : perspective(perspective), using_webtrans(true) {}
  explicit MoqtSessionParameters(bool deliver_partial_objects)
      : deliver_partial_objects(deliver_partial_objects) {}
  MoqtSessionParameters(quic::Perspective perspective, std::string path,
                        std::string authority)
      : perspective(perspective),
        using_webtrans(false),
        path(std::move(path)),
        authority(std::move(authority)) {}
  MoqtSessionParameters(quic::Perspective perspective, std::string path,
                        std::string authority, uint64_t max_request_id)
      : perspective(perspective),
        using_webtrans(true),
        path(std::move(path)),
        max_request_id(max_request_id),
        authority(std::move(authority)) {}
  MoqtSessionParameters(quic::Perspective perspective, uint64_t max_request_id)
      : perspective(perspective), max_request_id(max_request_id) {}
  bool operator==(const MoqtSessionParameters& other) const = default;

  std::string version = std::string(kDefaultMoqtVersion);
  bool deliver_partial_objects = false;
  quic::Perspective perspective = quic::Perspective::IS_SERVER;
  bool using_webtrans = true;
  std::string path;
  uint64_t max_request_id = kDefaultInitialMaxRequestId;
  uint64_t max_auth_token_cache_size = kDefaultMaxAuthTokenCacheSize;
  bool support_object_acks = false;
  // TODO(martinduke): Turn authorization_token into structured data.
  std::vector<AuthToken> authorization_token;
  std::string authority;
  std::string moqt_implementation;

  // Takes the relevant fields from this object and populates |out| if not the
  // protocol default value.
  void ToSetupParameters(SetupParameters& out) const;
};


// MoqtSession calls this when a FETCH_OK or REQUEST_ERROR is received. The
// destination of the callback owns |fetch_task| and MoqtSession will react
// safely if the owner destroys it.
using FetchResponseCallback =
    quiche::SingleUseCallback<void(std::unique_ptr<MoqtFetchTask> fetch_task)>;

class MoqtSessionInterface {
 public:
  virtual ~MoqtSessionInterface() = default;

  // TODO: move PUBLISH_NAMESPACE logic here.

  // Callbacks for session-level events.
  virtual MoqtSessionCallbacks& callbacks() = 0;

  // Close the session with a fatal error.
  virtual void Error(MoqtError code, absl::string_view error) = 0;

  // Many of these functions initiate a request and take MoqtResponseCallback to
  // report the peer's response. These functions return false if there is an
  // immediate problem that prevents sending the request, in which case the
  // callback will not be invoked. For example, there might not be stream credit
  // to open a request stream, or the request is a duplicate, or the session
  // is in a GOAWAY state.
  // Return true if SUBSCRIBE was actually sent.
  virtual bool Subscribe(const FullTrackName& name,
                         SubscribeVisitor* absl_nonnull visitor,
                         const MessageParameters& parameters) = 0;
  // If a parameter is nullopt, there is no change to the current value.
  // Returns false if the subscription is not found. Used by the subscriber for
  // a SUBSCRIBE or PUBLISH.
  virtual bool SubscribeUpdate(const FullTrackName& name,
                               const MessageParameters& parameters,
                               MoqtResponseCallback response_callback) = 0;
  // Used by the publisher of a PUBLISH message.
  virtual bool PublishUpdate(const FullTrackName& name,
                             const MessageParameters& parameters,
                             MoqtResponseCallback response_callback) = 0;

  // Sends an UNSUBSCRIBE message and removes all of the state related to the
  // subscription.  Returns false if the subscription is not found.
  virtual void Unsubscribe(const FullTrackName& name) = 0;

  // Returns false if the PUBLISH cannot be sent due stream flow control
  // limitations (which spawns PUBLISH_BLOCKED in namespace streams). Any other
  // failure will be covered by |response_callback|.
  virtual bool Publish(
      std::shared_ptr<MoqtTrackPublisher> absl_nonnull publisher,
      const MessageParameters& parameters, const TrackExtensions& extensions,
      MoqtResponseCallback response_callback) = 0;

  // Sends a FETCH for a pre-specified object range.  Once a FETCH_OK or a
  // FETCH_ERROR is received, `callback` is called with a MoqtFetchTask that can
  // be used to process the FETCH further.  To cancel a FETCH, simply destroy
  // the MoqtFetchTask.
  virtual bool Fetch(const FullTrackName& name, FetchResponseCallback callback,
                     Location start, uint64_t end_group,
                     std::optional<uint64_t> end_object,
                     MessageParameters parameters) = 0;

  // Sends both a SUBSCRIBE and a joining FETCH, beginning `num_previous_groups`
  // groups before the current group. The Fetch will not be flow controlled,
  // instead using |visitor| to deliver fetched objects when they arrive. Gaps
  // in the FETCH will not be filled by with ObjectDoesNotExist. If the FETCH
  // fails for any reason, the application will not receive a notification; it
  // will just appear to be missing objects.
  virtual bool RelativeJoiningFetch(const FullTrackName& name,
                                    SubscribeVisitor* visitor,
                                    uint64_t num_previous_groups,
                                    MessageParameters parameters) = 0;

  // Sends both a SUBSCRIBE and a joining FETCH, beginning `num_previous_groups`
  // groups before the current group.  `callback` acts the same way as the
  // callback for the regular Fetch() call.
  virtual bool RelativeJoiningFetch(const FullTrackName& name,
                                    SubscribeVisitor* visitor,
                                    FetchResponseCallback callback,
                                    uint64_t num_previous_groups,
                                    MessageParameters parameters) = 0;
  // Send a PUBLISH_NAMESPACE message for |track_namespace|, and call
  // |response_callback| when the response arrives. Will fail
  // immediately if there is already an unresolved PUBLISH_NAMESPACE for that
  // namespace. Calls |cancel_callback| if the peer closes the stream. Returns
  // true if the message was sent.
  virtual bool PublishNamespace(
      const TrackNamespace& track_namespace,
      const MessageParameters& parameters,
      MoqtResponseCallback response_callback,
      quiche::SingleUseCallback<void()> cancel_callback) = 0;
  virtual bool PublishNamespaceUpdate(
      const TrackNamespace& track_namespace, MessageParameters& parameters,
      MoqtResponseCallback response_callback) = 0;
  // Returns true if message was sent, false if there is no PUBLISH_NAMESPACE
  // that relates.
  virtual bool PublishNamespaceDone(const TrackNamespace& track_namespace) = 0;
  virtual bool PublishNamespaceCancel(
      const TrackNamespace& track_namespace,
      webtransport::StreamErrorCode error_code) = 0;

  // Sends a SUBSCRIBE_NAMESPACE message for |prefix| and returns a
  // MoqtNamespaceTask that can be used to process the response.
  // Returns nullptr if the message cannot be sent.
  // To unsubscribe, simply destroy the returned MoqtNamespaceTask.
  virtual std::unique_ptr<MoqtNamespaceTask> SubscribeNamespace(
      TrackNamespace& prefix, const MessageParameters& parameters,
      MoqtResponseCallback response_callback) = 0;
  virtual bool SubscribeTracks(TrackNamespace& prefix,
                               const MessageParameters& parameters,
                               MoqtResponseCallback response_callback) = 0;
  virtual void UnsubscribeTracks(TrackNamespace& prefix) = 0;

  // TODO(martinduke): Add an API for absolute joining fetch.

  // Sends TRACK_STATUS request to the peer. Returns `false` if the request
  // immediately fails (usually due to flow control), and `true` otherwise;
  // `response_callback` will be eventually invoked if true.
  virtual bool TrackStatus(const FullTrackName& name,
                           const MessageParameters& parameters,
                           MoqtResponseCallback response_callback) = 0;
  // TODO: Add RequestUpdate, PublishDone method.
  virtual quiche::QuicheWeakPtr<MoqtSessionInterface> GetWeakPtr() = 0;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_INTERFACE_H_
