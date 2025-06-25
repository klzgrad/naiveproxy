// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/structured_headers.h"

#include <optional>
#include <string_view>

#include "base/feature.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"

namespace net::structured_headers {

namespace {

constexpr char kTimeMetricItem[] = "Net.StructuredHeaders.ParseItem.Time";
constexpr char kTimeMetricList[] = "Net.StructuredHeaders.ParseList.Time";
constexpr char kTimeMetricDictionary[] =
    "Net.StructuredHeaders.ParseDictionary.Time";

constexpr char kSuccessMetricItem[] = "Net.StructuredHeaders.ParseItem.Success";
constexpr char kSuccessMetricList[] = "Net.StructuredHeaders.ParseList.Success";
constexpr char kSuccessMetricDictionary[] =
    "Net.StructuredHeaders.ParseDictionary.Success";

template <typename Parse>
auto ParseAndRecordMetrics(std::string_view time_metric,
                           std::string_view success_metric,
                           Parse&& parse) {
  const base::TimeTicks start = base::TimeTicks::Now();
  auto result = parse();
  base::UmaHistogramMicrosecondsTimes(time_metric,
                                      base::TimeTicks::Now() - start);
  base::UmaHistogramBoolean(success_metric, !!result);
  return result;
}

}  // namespace

BASE_FEATURE(kStructuredHeadersInRust, base::FEATURE_DISABLED_BY_DEFAULT);

std::optional<ParameterizedItem> ParseItem(std::string_view str) {
  return ParseAndRecordMetrics(kTimeMetricItem, kSuccessMetricItem, [&]() {
    return quiche::structured_headers::ParseItem(str);
  });
}

std::optional<List> ParseList(std::string_view str) {
  return ParseAndRecordMetrics(kTimeMetricList, kSuccessMetricList, [&]() {
    return quiche::structured_headers::ParseList(str);
  });
}

std::optional<Dictionary> ParseDictionary(std::string_view str) {
  return ParseAndRecordMetrics(
      kTimeMetricDictionary, kSuccessMetricDictionary,
      [&]() { return quiche::structured_headers::ParseDictionary(str); });
}

}  // namespace net::structured_headers
