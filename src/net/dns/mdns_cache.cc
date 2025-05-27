// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mdns_cache.h"

#include <stdint.h>

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/containers/contains.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "net/dns/public/dns_protocol.h"
#include "net/dns/record_parsed.h"
#include "net/dns/record_rdata.h"

// TODO(noamsml): Recursive CNAME closure (backwards and forwards).

namespace net {

namespace {
constexpr size_t kDefaultEntryLimit = 100'000;
}  // namespace

// The effective TTL given to records with a nominal zero TTL.
// Allows time for hosts to send updated records, as detailed in RFC 6762
// Section 10.1.
static constexpr uint32_t kZeroTTLSeconds = 1;

MDnsCache::Key::Key(uint32_t type,
                    const std::string& name,
                    const std::string& optional)
    : type_(type),
      name_lowercase_(base::ToLowerASCII(name)),
      optional_(optional) {}

MDnsCache::Key::Key(const MDnsCache::Key& other) = default;

MDnsCache::Key& MDnsCache::Key::operator=(const MDnsCache::Key& other) =
    default;

MDnsCache::Key::~Key() = default;

bool MDnsCache::Key::operator<(const MDnsCache::Key& other) const {
  return std::tie(name_lowercase_, type_, optional_) <
         std::tie(other.name_lowercase_, other.type_, other.optional_);
}

bool MDnsCache::Key::operator==(const MDnsCache::Key& key) const {
  return type_ == key.type_ && name_lowercase_ == key.name_lowercase_ &&
         optional_ == key.optional_;
}

// static
MDnsCache::Key MDnsCache::Key::CreateFor(const RecordParsed* record) {
  return Key(record->type(),
             record->name(),
             GetOptionalFieldForRecord(record));
}

MDnsCache::MDnsCache() : entry_limit_(kDefaultEntryLimit) {}

MDnsCache::~MDnsCache() = default;

const RecordParsed* MDnsCache::LookupKey(const Key& key) {
  auto found = mdns_cache_.find(key);
  if (found != mdns_cache_.end()) {
    return found->second.get();
  }
  return nullptr;
}

MDnsCache::UpdateType MDnsCache::UpdateDnsRecord(
    std::unique_ptr<const RecordParsed> record) {
  Key cache_key = Key::CreateFor(record.get());

  // Ignore "goodbye" packets for records not in cache.
  if (record->ttl() == 0 && !base::Contains(mdns_cache_, cache_key)) {
    return NoChange;
  }

  base::Time new_expiration = GetEffectiveExpiration(record.get());
  if (next_expiration_ != base::Time())
    new_expiration = std::min(new_expiration, next_expiration_);

  std::pair<RecordMap::iterator, bool> insert_result =
      mdns_cache_.emplace(cache_key, nullptr);
  UpdateType type = NoChange;
  if (insert_result.second) {
    type = RecordAdded;
  } else {
    if (record->ttl() != 0 &&
        !record->IsEqual(insert_result.first->second.get(), true)) {
      type = RecordChanged;
    }
  }

  insert_result.first->second = std::move(record);
  next_expiration_ = new_expiration;
  return type;
}

void MDnsCache::CleanupRecords(
    base::Time now,
    const RecordRemovedCallback& record_removed_callback) {
  base::Time next_expiration;

  // TODO(crbug.com/41449550): Make overfill pruning more intelligent than a
  // bulk clearing of everything.
  bool clear_cache = IsCacheOverfilled();

  // We are guaranteed that |next_expiration_| will be at or before the next
  // expiration. This allows clients to eagrely call CleanupRecords with
  // impunity.
  if (now < next_expiration_ && !clear_cache)
    return;

  for (auto i = mdns_cache_.begin(); i != mdns_cache_.end();) {
    base::Time expiration = GetEffectiveExpiration(i->second.get());
    if (clear_cache || now >= expiration) {
      record_removed_callback.Run(i->second.get());
      i = mdns_cache_.erase(i);
    } else {
      if (next_expiration == base::Time() ||  expiration < next_expiration) {
        next_expiration = expiration;
      }
      ++i;
    }
  }

  next_expiration_ = next_expiration;
}

void MDnsCache::FindDnsRecords(uint32_t type,
                               const std::string& name,
                               std::vector<const RecordParsed*>* results,
                               base::Time now) const {
  DCHECK(results);
  results->clear();

  const std::string name_lowercase = base::ToLowerASCII(name);
  auto i = mdns_cache_.lower_bound(Key(type, name, ""));
  for (; i != mdns_cache_.end(); ++i) {
    if (i->first.name_lowercase() != name_lowercase ||
        (type != 0 && i->first.type() != type)) {
      break;
    }

    const RecordParsed* record = i->second.get();

    // Records are deleted only upon request.
    if (now >= GetEffectiveExpiration(record)) continue;

    results->push_back(record);
  }
}

std::unique_ptr<const RecordParsed> MDnsCache::RemoveRecord(
    const RecordParsed* record) {
  Key key = Key::CreateFor(record);
  auto found = mdns_cache_.find(key);

  if (found != mdns_cache_.end() && found->second.get() == record) {
    std::unique_ptr<const RecordParsed> result = std::move(found->second);
    mdns_cache_.erase(key);
    return result;
  }

  return nullptr;
}

bool MDnsCache::IsCacheOverfilled() const {
  return mdns_cache_.size() > entry_limit_;
}

// static
std::string MDnsCache::GetOptionalFieldForRecord(const RecordParsed* record) {
  switch (record->type()) {
    case PtrRecordRdata::kType: {
      const PtrRecordRdata* rdata = record->rdata<PtrRecordRdata>();
      return rdata->ptrdomain();
    }
    default:  // Most records are considered unique for our purposes
      return "";
  }
}

// static
base::Time MDnsCache::GetEffectiveExpiration(const RecordParsed* record) {
  base::TimeDelta ttl;

  if (record->ttl()) {
    ttl = base::Seconds(record->ttl());
  } else {
    ttl = base::Seconds(kZeroTTLSeconds);
  }

  return record->time_created() + ttl;
}

}  // namespace net
