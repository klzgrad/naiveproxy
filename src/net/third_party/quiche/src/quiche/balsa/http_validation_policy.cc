// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/balsa/http_validation_policy.h"

#include <tuple>

#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

HttpValidationPolicy::HttpValidationPolicy(bool enforce_all)
    : enforce_all_(enforce_all) {}

HttpValidationPolicy HttpValidationPolicy::CreateDefault() {
  return HttpValidationPolicy(false);
}

bool HttpValidationPolicy::operator==(const HttpValidationPolicy& other) const {
  return enforce_all_ == other.enforce_all_;
}

}  // namespace quiche
