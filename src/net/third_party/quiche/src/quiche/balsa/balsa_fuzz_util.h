// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_BALSA_FUZZ_UTIL_H_
#define QUICHE_BALSA_BALSA_FUZZ_UTIL_H_

#include "quiche/balsa/http_validation_policy.h"
#include "quiche/common/platform/api/quiche_fuzztest.h"

namespace quiche {

fuzztest::Domain<HttpValidationPolicy> ArbitraryHttpValidationPolicy();
fuzztest::Domain<HttpValidationPolicy::FirstLineValidationOption>
ArbitraryFirstLineValidationOption();

}  // namespace quiche

#endif  // QUICHE_BALSA_BALSA_FUZZ_UTIL_H_
