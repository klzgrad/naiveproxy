// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "quiche/balsa/balsa_fuzz_util.h"

#include <optional>
#include <tuple>
#include <type_traits>

#include "quiche/balsa/http_validation_policy.h"
#include "quiche/common/platform/api/quiche_fuzztest.h"

namespace quiche {

fuzztest::Domain<HttpValidationPolicy> ArbitraryHttpValidationPolicy() {
  return fuzztest::StructOf<HttpValidationPolicy>(
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), ArbitraryFirstLineValidationOption(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), ArbitraryFirstLineValidationOption(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
      fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>());
}

fuzztest::Domain<HttpValidationPolicy::FirstLineValidationOption>
ArbitraryFirstLineValidationOption() {
  using EnumType = HttpValidationPolicy::FirstLineValidationOption;
  using UnderlyingType =
      std::underlying_type_t<HttpValidationPolicy::FirstLineValidationOption>;
  return fuzztest::ReversibleMap(
      [](UnderlyingType x) -> EnumType {
        return static_cast<HttpValidationPolicy::FirstLineValidationOption>(x);
      },
      [](EnumType x) -> std::optional<std::tuple<UnderlyingType>> {
        return {static_cast<UnderlyingType>(x)};
      },
      fuzztest::InRange<UnderlyingType>(
          static_cast<UnderlyingType>(
              HttpValidationPolicy::FirstLineValidationOption::kMinValue),
          static_cast<UnderlyingType>(
              HttpValidationPolicy::FirstLineValidationOption::kMaxValue)));
}

}  // namespace quiche
