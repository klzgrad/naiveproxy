// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_FETCH_TASK_H_
#define QUICHE_QUIC_MOQT_MOQT_FETCH_TASK_H_

#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/common/quiche_callbacks.h"

namespace moqt {

// A handle representing a fetch in progress.  The fetch in question can be
// cancelled by deleting the object.
class MoqtFetchTask {
 public:
  using ObjectsAvailableCallback = quiche::MultiUseCallback<void()>;
  // The request_id field will be ignored.
  using FetchResponseCallback = quiche::SingleUseCallback<void(
      std::variant<MoqtFetchOk, MoqtFetchError>)>;

  virtual ~MoqtFetchTask() = default;

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
  void SetFetchResponseCallback(FetchResponseCallback callback) {
    MoqtFetchError error;
    error.request_id = 0;
    error.error_code = StatusToRequestErrorCode(status_);
    error.error_reason = status_.message();
    std::move(callback)(error);
  }

 private:
  absl::Status status_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_FETCH_TASK_H_
