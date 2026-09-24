// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_cache_invalidation_pickle_traits.h"

#include <string>

#include "base/containers/flat_set.h"
#include "base/time/time.h"
#include "net/base/pickle.h"
#include "net/base/pickle_base_types.h"
#include "net/base/pickle_url_types.h"
#include "url/origin.h"

namespace net {

// static
void PickleTraits<HttpCache::InvalidationFilter>::Serialize(
    base::Pickle& pickle,
    const HttpCache::InvalidationFilter& value) {
  WriteToPickle(pickle, value.begin_time, value.end_time,
                static_cast<int>(value.filter_type), value.origins,
                value.domains);
}

// static
std::optional<HttpCache::InvalidationFilter> PickleTraits<
    HttpCache::InvalidationFilter>::Deserialize(base::PickleIterator& iter) {
  HttpCache::InvalidationFilter filter;

  auto maybe_begin = ReadValueFromPickle<base::Time>(iter);
  auto maybe_end = ReadValueFromPickle<base::Time>(iter);
  if (!maybe_begin || !maybe_end || *maybe_begin > *maybe_end) {
    return std::nullopt;
  }
  filter.begin_time = *maybe_begin;
  filter.end_time = *maybe_end;

  auto maybe_filter_type_int = ReadValueFromPickle<int>(iter);
  if (!maybe_filter_type_int ||
      (*maybe_filter_type_int !=
           static_cast<int>(UrlFilterType::kTrueIfMatches) &&
       *maybe_filter_type_int !=
           static_cast<int>(UrlFilterType::kFalseIfMatches))) {
    return std::nullopt;
  }
  filter.filter_type = static_cast<UrlFilterType>(*maybe_filter_type_int);

  auto maybe_origins = ReadValueFromPickle<base::flat_set<url::Origin>>(iter);
  if (!maybe_origins || maybe_origins->size() > kMaxCollectionSize) {
    return std::nullopt;
  }
  filter.origins = std::move(*maybe_origins);

  auto maybe_domains = ReadValueFromPickle<base::flat_set<std::string>>(iter);
  if (!maybe_domains || maybe_domains->size() > kMaxCollectionSize) {
    return std::nullopt;
  }
  filter.domains = std::move(*maybe_domains);

  return filter;
}

// static
size_t PickleTraits<HttpCache::InvalidationFilter>::PickleSize(
    const HttpCache::InvalidationFilter& value) {
  return EstimatePickleSize(value.begin_time, value.end_time,
                            static_cast<int>(value.filter_type), value.origins,
                            value.domains);
}

}  // namespace net
