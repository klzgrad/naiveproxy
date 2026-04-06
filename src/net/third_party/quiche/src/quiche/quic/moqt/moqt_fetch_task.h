// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_FETCH_TASK_H_
#define QUICHE_QUIC_MOQT_MOQT_FETCH_TASK_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// The callback we'll use for all request types going forward. Can only be used
// once; if the argument is nullopt, an OK response was received. Otherwise, an
// ERROR response was received.
using MoqtResponseCallback =
    quiche::SingleUseCallback<void(std::optional<MoqtRequestErrorInfo>)>;

// TODO(martinduke): There are will be multiple instances of flow-controlled
// "pull" data retrieval tasks. It might be worthwhile to extract some common
// features into a base class.

using ObjectsAvailableCallback = quiche::MultiUseCallback<void()>;
// Potential results of a GetNextObject/GetNextMessage() call.
enum GetNextResult {
  // The next object or message is available, and is placed into the reference
  // specified by the caller.
  kSuccess,
  // The next object or message is not yet available (equivalent of EAGAIN).
  kPending,
  // The end of the response has been reached.
  kEof,
  // The request has failed; the error is available.
  kError,
};

enum class TransactionType : uint8_t { kAdd, kDelete };

// A handle representing a fetch in progress.  The fetch in question can be
// cancelled by deleting the object.
class MoqtFetchTask {
 public:
  // The request_id field will be ignored.
  using FetchResponseCallback = quiche::SingleUseCallback<void(
      std::variant<MoqtFetchOk, MoqtRequestError>)>;

  virtual ~MoqtFetchTask() = default;

  // TODO(martinduke): Replace with GetNextResult above.
  // Potential results of a GetNextObject() call.
  enum GetNextObjectResult {
    // The next object is available, and is placed into the reference specified
    // by the caller.
    kSuccess,
    // The next object is not yet available (equivalent of EAGAIN).
    kPending,
    // The end of fetch has been reached.
    kEof,
    // The fetch has failed; the error is available via GetStatus().
    kError,
  };

  // Returns the next object received via the fetch, if available. MUST NOT
  // return an object with status kObjectDoesNotExist.
  virtual GetNextObjectResult GetNextObject(PublishedObject& output) = 0;

  // Sets the callback that is called when GetNextObject() has previously
  // returned kPending, but now a new object (or potentially an error or an
  // end-of-fetch) is available. The application is responsible for calling
  // GetNextObject() until it gets kPending; no further callback will occur
  // until then.
  // If an object is available immediately, the callback will be called
  // immediately.
  virtual void SetObjectAvailableCallback(
      ObjectsAvailableCallback callback) = 0;
  // One of these callbacks is called as soon as the data publisher has enough
  // information for either FETCH_OK or FETCH_ERROR.
  // If the appropriate response is already available, the callback will be
  // called immediately.
  virtual void SetFetchResponseCallback(FetchResponseCallback callback) = 0;

  // Returns the error if fetch has completely failed, and OK otherwise.
  virtual absl::Status GetStatus() = 0;
};

class MoqtNamespaceTask {
 public:
  virtual ~MoqtNamespaceTask() = default;

  // The provided callback may be immediately invoked.
  virtual void SetObjectsAvailableCallback(ObjectsAvailableCallback
                                           absl_nullable callback) = 0;

  // Returns the state of the message queue. If available, writes the suffix
  // into |suffix|. If |type| is kAdd, it is from a NAMESPACE message. If |type|
  // is kDelete, it is from a NAMESPACE_DONE message.
  virtual GetNextResult GetNextSuffix(TrackNamespace& suffix,
                                      TransactionType& type) = 0;

  // Returns the error if request has completely failed, and nullopt otherwise.
  virtual std::optional<webtransport::StreamErrorCode> GetStatus() = 0;

  // Handle a REQUEST_UPDATE message.
  virtual void Update(const MessageParameters& parameters,
                      MoqtResponseCallback response_callback) = 0;

  // Returns the prefix for this task.
  virtual const TrackNamespace& prefix() = 0;
};

// A fetch that starts out in the failed state.
class MoqtFailedFetch : public MoqtFetchTask {
 public:
  explicit MoqtFailedFetch(absl::Status status) : status_(std::move(status)) {}

  GetNextObjectResult GetNextObject(PublishedObject&) override {
    return kError;
  }
  absl::Status GetStatus() override { return status_; }
  void SetObjectAvailableCallback(
      ObjectsAvailableCallback /*callback*/) override {}
  void SetFetchResponseCallback(FetchResponseCallback callback) override {
    MoqtRequestError error{/*request_id=*/0, StatusToRequestErrorCode(status_),
                           std::nullopt, std::string(status_.message())};
    std::move(callback)(error);
  }

 private:
  absl::Status status_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_FETCH_TASK_H_
