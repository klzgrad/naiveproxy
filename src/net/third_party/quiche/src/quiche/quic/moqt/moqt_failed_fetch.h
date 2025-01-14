// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_FAILED_FETCH_H_
#define QUICHE_QUIC_MOQT_MOQT_FAILED_FETCH_H_

#include <utility>

#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_publisher.h"

namespace moqt {

// A fetch that starts out in the failed state.
class MoqtFailedFetch : public MoqtFetchTask {
 public:
  explicit MoqtFailedFetch(absl::Status status) : status_(std::move(status)) {}

  GetNextObjectResult GetNextObject(PublishedObject&) override {
    return kError;
  }
  absl::Status GetStatus() override { return status_; }

 private:
  absl::Status status_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_FAILED_FETCH_H_
