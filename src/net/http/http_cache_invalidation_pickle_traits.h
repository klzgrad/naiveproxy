// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_CACHE_INVALIDATION_PICKLE_TRAITS_H_
#define NET_HTTP_HTTP_CACHE_INVALIDATION_PICKLE_TRAITS_H_

#include <optional>

#include "net/base/net_export.h"
#include "net/base/pickle_traits.h"
#include "net/http/http_cache.h"

namespace net {

template <>
struct NET_EXPORT PickleTraits<HttpCache::InvalidationFilter> {
  // Maximum number of elements allowed in collection fields (origins, domains).
  static constexpr size_t kMaxCollectionSize = 10000;

  static void Serialize(base::Pickle& pickle,
                        const HttpCache::InvalidationFilter& value);
  static std::optional<HttpCache::InvalidationFilter> Deserialize(
      base::PickleIterator& iter);
  static size_t PickleSize(const HttpCache::InvalidationFilter& value);
};

}  // namespace net

#endif  // NET_HTTP_HTTP_CACHE_INVALIDATION_PICKLE_TRAITS_H_
