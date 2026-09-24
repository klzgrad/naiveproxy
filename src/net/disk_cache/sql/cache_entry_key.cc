// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/sql/cache_entry_key.h"

#include <utility>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/hash/hash.h"
#include "base/memory/scoped_refptr.h"
#include "net/base/features.h"
#include "net/http/http_cache.h"

namespace disk_cache {

// static
CacheEntryKey::Hash CacheEntryKey::HashFromString(const std::string_view str) {
  return Hash(static_cast<int32_t>(base::PersistentHash(str)));
}

CacheEntryKey::Data::Data(std::string key_str)
    : key(std::move(key_str)),
      hash(HashFromString(key)),
      resource_url(base::FeatureList::IsEnabled(
                       net::features::kRendererAccessibleHttpCache)
                       ? net::HttpCache::GetResourceURLFromHttpCacheKey(key)
                       : std::string_view()),
      resource_url_hash(base::FeatureList::IsEnabled(
                            net::features::kRendererAccessibleHttpCache)
                            ? SqlSharedCacheUrlHash(static_cast<int32_t>(
                                  base::PersistentHash(resource_url)))
                            : SqlSharedCacheUrlHash()) {}

CacheEntryKey::CacheEntryKey(std::string str)
    : data_(base::MakeRefCounted<base::RefCountedData<CacheEntryKey::Data>>(
          std::in_place,
          std::move(str))) {}
CacheEntryKey::~CacheEntryKey() = default;

CacheEntryKey::CacheEntryKey(const CacheEntryKey& other) = default;
CacheEntryKey::CacheEntryKey(CacheEntryKey&& other) = default;
CacheEntryKey& CacheEntryKey::operator=(const CacheEntryKey& other) = default;
CacheEntryKey& CacheEntryKey::operator=(CacheEntryKey&& other) = default;

bool CacheEntryKey::operator<(const CacheEntryKey& other) const {
  return data_ != other.data_ && string() < other.string();
}

bool CacheEntryKey::operator==(const CacheEntryKey& other) const {
  return data_ == other.data_ ||
         (hash() == other.hash() && string() == other.string());
}

const std::string& CacheEntryKey::string() const {
  CHECK(data_);
  return data_->data.key;
}

CacheEntryKey::Hash CacheEntryKey::hash() const {
  CHECK(data_);
  return data_->data.hash;
}

std::string_view CacheEntryKey::resource_url() const {
  CHECK(base::FeatureList::IsEnabled(
      net::features::kRendererAccessibleHttpCache));
  CHECK(data_);
  return data_->data.resource_url;
}

SqlSharedCacheUrlHash CacheEntryKey::resource_url_hash() const {
  CHECK(base::FeatureList::IsEnabled(
      net::features::kRendererAccessibleHttpCache));
  CHECK(data_);
  return data_->data.resource_url_hash;
}

}  // namespace disk_cache
