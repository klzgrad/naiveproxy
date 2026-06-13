// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_NOOP_BALSA_VISITOR_H_
#define QUICHE_BALSA_NOOP_BALSA_VISITOR_H_

#include <cstddef>
#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/balsa/balsa_visitor_interface.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

class BalsaHeaders;

// Provides empty BalsaVisitorInterface overrides for convenience.
// Intended to be used as a base class for BalsaVisitorInterface subclasses that
// only need to override a small number of methods.
class QUICHE_EXPORT NoOpBalsaVisitor : public BalsaVisitorInterface {
 public:
  NoOpBalsaVisitor() = default;

  NoOpBalsaVisitor(const NoOpBalsaVisitor&) = delete;
  NoOpBalsaVisitor& operator=(const NoOpBalsaVisitor&) = delete;

  ~NoOpBalsaVisitor() override {}

  void OnRawBodyInput(absl::string_view /*input*/) override {}
  void OnBodyChunkInput(absl::string_view /*input*/) override {}
  void OnHeaderInput(absl::string_view /*input*/) override {}
  void OnTrailerInput(absl::string_view /*input*/) override {}
  void ProcessHeaders(const BalsaHeaders& /*headers*/) override {}
  void OnTrailers(std::unique_ptr<BalsaHeaders> /*trailers*/) override {}

  void OnRequestFirstLineInput(absl::string_view /*line_input*/,
                               absl::string_view /*method_input*/,
                               absl::string_view /*request_uri_input*/,
                               absl::string_view /*version_input*/) override {}
  void OnResponseFirstLineInput(absl::string_view /*line_input*/,
                                absl::string_view /*version_input*/,
                                absl::string_view /*status_input*/,
                                absl::string_view /*reason_input*/) override {}
  void OnChunkLength(size_t /*chunk_length*/) override {}
  void OnChunkExtensionInput(absl::string_view /*input*/) override {}
  void OnInterimHeaders(std::unique_ptr<BalsaHeaders> /*headers*/) override {}
  void ContinueHeaderDone() override {}
  void HeaderDone() override {}
  void MessageDone() override {}
  void HandleError(BalsaFrameEnums::ErrorCode /*error_code*/) override {}
  void HandleWarning(BalsaFrameEnums::ErrorCode /*error_code*/) override {}
};

}  // namespace quiche

#endif  // QUICHE_BALSA_NOOP_BALSA_VISITOR_H_
