// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_cache.h"

#include <algorithm>

#include "base/bind.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/default_tick_clock.h"
#include "base/trace_event/trace_event.h"
#include "net/base/net_errors.h"
#include "net/base/trace_constants.h"
#include "net/dns/host_resolver.h"
#include "net/log/net_log.h"

namespace net {

namespace {

#define CACHE_HISTOGRAM_TIME(name, time) \
  UMA_HISTOGRAM_LONG_TIMES("DNS.HostCache." name, time)

#define CACHE_HISTOGRAM_COUNT(name, count) \
  UMA_HISTOGRAM_COUNTS_1000("DNS.HostCache." name, count)

#define CACHE_HISTOGRAM_ENUM(name, value, max) \
  UMA_HISTOGRAM_ENUMERATION("DNS.HostCache." name, value, max)

// String constants for dictionary keys.
const char kHostnameKey[] = "hostname";
const char kAddressFamilyKey[] = "address_family";
const char kDnsQueryTypeKey[] = "dns_query_type";
const char kFlagsKey[] = "flags";
const char kHostResolverSourceKey[] = "host_resolver_source";
const char kExpirationKey[] = "expiration";
const char kTtlKey[] = "ttl";
const char kNetworkChangesKey[] = "network_changes";
const char kErrorKey[] = "error";
const char kAddressesKey[] = "addresses";
const char kTextRecordsKey[] = "text_records";
const char kHostnameResultsKey[] = "hostname_results";
const char kHostPortsKey[] = "host_ports";

bool AddressListFromListValue(const base::ListValue* value,
                              base::Optional<AddressList>* out_list) {
  if (!value) {
    out_list->reset();
    return true;
  }

  out_list->emplace();
  for (auto it = value->begin(); it != value->end(); it++) {
    IPAddress address;
    std::string addr_string;
    if (!it->GetAsString(&addr_string) ||
        !address.AssignFromIPLiteral(addr_string)) {
      return false;
    }
    out_list->value().push_back(IPEndPoint(address, 0));
  }
  return true;
}

template <typename T>
void MergeLists(base::Optional<T>* target, const base::Optional<T>& source) {
  if (target->has_value() && source) {
    target->value().insert(target->value().end(), source.value().begin(),
                           source.value().end());
  } else if (source) {
    *target = source;
  }
}

}  // namespace

// Used in histograms; do not modify existing values.
enum HostCache::SetOutcome : int {
  SET_INSERT = 0,
  SET_UPDATE_VALID = 1,
  SET_UPDATE_STALE = 2,
  MAX_SET_OUTCOME
};

// Used in histograms; do not modify existing values.
enum HostCache::LookupOutcome : int {
  LOOKUP_MISS_ABSENT = 0,
  LOOKUP_MISS_STALE = 1,
  LOOKUP_HIT_VALID = 2,
  LOOKUP_HIT_STALE = 3,
  MAX_LOOKUP_OUTCOME
};

// Used in histograms; do not modify existing values.
enum HostCache::EraseReason : int {
  ERASE_EVICT = 0,
  ERASE_CLEAR = 1,
  ERASE_DESTRUCT = 2,
  MAX_ERASE_REASON
};

HostCache::Key::Key(const std::string& hostname,
                    DnsQueryType dns_query_type,
                    HostResolverFlags host_resolver_flags,
                    HostResolverSource host_resolver_source)
    : hostname(hostname),
      dns_query_type(dns_query_type),
      host_resolver_flags(host_resolver_flags),
      host_resolver_source(host_resolver_source) {}

HostCache::Key::Key(const std::string& hostname,
                    AddressFamily address_family,
                    HostResolverFlags host_resolver_flags)
    : Key(hostname,
          AddressFamilyToDnsQueryType(address_family),
          host_resolver_flags,
          HostResolverSource::ANY) {}

HostCache::Key::Key()
    : Key("", DnsQueryType::UNSPECIFIED, 0, HostResolverSource::ANY) {}

HostCache::Entry::Entry(int error, Source source, base::TimeDelta ttl)
    : error_(error), source_(source), ttl_(ttl) {
  DCHECK_GE(ttl_, base::TimeDelta());
  DCHECK_NE(OK, error_);
}

HostCache::Entry::Entry(int error, Source source)
    : error_(error), source_(source), ttl_(base::TimeDelta::FromSeconds(-1)) {
  DCHECK_NE(OK, error_);
}

HostCache::Entry::Entry(const Entry& entry) = default;

HostCache::Entry::Entry(Entry&& entry) = default;

HostCache::Entry::~Entry() = default;

base::Optional<base::TimeDelta> HostCache::Entry::GetOptionalTtl() const {
  if (has_ttl())
    return ttl();
  else
    return base::nullopt;
}

// static
HostCache::Entry HostCache::Entry::MergeEntries(Entry front, Entry back) {
  // Only expected to merge OK or ERR_NAME_NOT_RESOLVED results.
  DCHECK(front.error() == OK || front.error() == ERR_NAME_NOT_RESOLVED);
  DCHECK(back.error() == OK || back.error() == ERR_NAME_NOT_RESOLVED);

  // Build results in |front| to preserve unmerged fields.

  front.error_ =
      front.error() == OK || back.error() == OK ? OK : ERR_NAME_NOT_RESOLVED;

  MergeLists(&front.addresses_, back.addresses());
  MergeLists(&front.text_records_, back.text_records());
  MergeLists(&front.hostnames_, back.hostnames());

  // Use canonical name from |back| iff empty in |front|.
  if (front.addresses() && front.addresses().value().canonical_name().empty() &&
      back.addresses()) {
    front.addresses_.value().set_canonical_name(
        back.addresses().value().canonical_name());
  }

  // Only expected to merge entries from same source.
  DCHECK_EQ(front.source(), back.source());

  if (front.has_ttl() && back.has_ttl()) {
    front.ttl_ = std::min(front.ttl(), back.ttl());
  } else if (back.has_ttl()) {
    front.ttl_ = back.ttl();
  }

  front.expires_ = std::min(front.expires(), back.expires());
  front.network_changes_ =
      std::max(front.network_changes(), back.network_changes());
  front.total_hits_ = front.total_hits() + back.total_hits();
  front.stale_hits_ = front.stale_hits() + back.stale_hits();

  return front;
}

NetLogParametersCallback HostCache::Entry::CreateNetLogCallback() const {
  return base::BindRepeating(&HostCache::Entry::NetLogCallback,
                             base::Unretained(this));
}

HostCache::Entry& HostCache::Entry::operator=(const Entry& entry) = default;

HostCache::Entry& HostCache::Entry::operator=(Entry&& entry) = default;

HostCache::Entry::Entry(const HostCache::Entry& entry,
                        base::TimeTicks now,
                        base::TimeDelta ttl,
                        int network_changes)
    : error_(entry.error()),
      addresses_(entry.addresses()),
      text_records_(entry.text_records()),
      hostnames_(entry.hostnames()),
      source_(entry.source()),
      ttl_(entry.ttl()),
      expires_(now + ttl),
      network_changes_(network_changes),
      total_hits_(0),
      stale_hits_(0) {}

HostCache::Entry::Entry(int error,
                        const base::Optional<AddressList>& addresses,
                        base::Optional<std::vector<std::string>>&& text_records,
                        base::Optional<std::vector<HostPortPair>>&& hostnames,
                        Source source,
                        base::TimeTicks expires,
                        int network_changes)
    : error_(error),
      addresses_(addresses),
      text_records_(std::move(text_records)),
      hostnames_(std::move(hostnames)),
      source_(source),
      ttl_(base::TimeDelta::FromSeconds(-1)),
      expires_(expires),
      network_changes_(network_changes),
      total_hits_(0),
      stale_hits_(0) {}

bool HostCache::Entry::IsStale(base::TimeTicks now, int network_changes) const {
  EntryStaleness stale;
  stale.expired_by = now - expires_;
  stale.network_changes = network_changes - network_changes_;
  stale.stale_hits = stale_hits_;
  return stale.is_stale();
}

void HostCache::Entry::CountHit(bool hit_is_stale) {
  ++total_hits_;
  if (hit_is_stale)
    ++stale_hits_;
}

void HostCache::Entry::GetStaleness(base::TimeTicks now,
                                    int network_changes,
                                    EntryStaleness* out) const {
  DCHECK(out);
  out->expired_by = now - expires_;
  out->network_changes = network_changes - network_changes_;
  out->stale_hits = stale_hits_;
}

std::unique_ptr<base::Value> HostCache::Entry::NetLogCallback(
    NetLogCaptureMode capture_mode) const {
  return std::make_unique<base::Value>(
      GetAsValue(false /* include_staleness */));
}

base::DictionaryValue HostCache::Entry::GetAsValue(
    bool include_staleness) const {
  base::DictionaryValue entry_dict;

  if (include_staleness) {
    // The kExpirationKey value is using TimeTicks instead of Time used if
    // |include_staleness| is false, so it cannot be used to deserialize.
    // This is ok as it is used only for netlog.
    entry_dict.SetString(kExpirationKey, NetLog::TickCountToString(expires()));
    entry_dict.SetInteger(kTtlKey, ttl().InMilliseconds());
    entry_dict.SetInteger(kNetworkChangesKey, network_changes());
  } else {
    // Convert expiration time in TimeTicks to Time for serialization, using a
    // string because base::Value doesn't handle 64-bit integers.
    base::Time expiration_time =
        base::Time::Now() - (base::TimeTicks::Now() - expires());
    entry_dict.SetString(
        kExpirationKey, base::Int64ToString(expiration_time.ToInternalValue()));
  }

  if (error() != OK) {
    entry_dict.SetInteger(kErrorKey, error());
  } else {
    if (addresses()) {
      // Append all of the resolved addresses.
      base::ListValue addresses_value;
      for (const IPEndPoint& address : addresses().value()) {
        addresses_value.GetList().emplace_back(address.ToStringWithoutPort());
      }
      entry_dict.SetKey(kAddressesKey, std::move(addresses_value));
    }

    if (text_records()) {
      // Append all resolved text records.
      base::ListValue text_list_value;
      for (const std::string& text_record : text_records().value()) {
        text_list_value.GetList().emplace_back(text_record);
      }
      entry_dict.SetKey(kTextRecordsKey, std::move(text_list_value));
    }

    if (hostnames()) {
      // Append all the resolved hostnames.
      base::ListValue hostnames_value;
      base::ListValue host_ports_value;
      for (const HostPortPair& hostname : hostnames().value()) {
        hostnames_value.GetList().emplace_back(hostname.host());
        host_ports_value.GetList().emplace_back(hostname.port());
      }
      entry_dict.SetKey(kHostnameResultsKey, std::move(hostnames_value));
      entry_dict.SetKey(kHostPortsKey, std::move(host_ports_value));
    }
  }

  return entry_dict;
}

HostCache::HostCache(size_t max_entries)
    : max_entries_(max_entries),
      network_changes_(0),
      restore_size_(0),
      delegate_(nullptr),
      tick_clock_(base::DefaultTickClock::GetInstance()) {}

HostCache::~HostCache() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

const HostCache::Entry* HostCache::Lookup(const Key& key, base::TimeTicks now) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (caching_is_disabled())
    return nullptr;

  HostCache::Entry* entry = LookupInternal(key);
  if (!entry)
    return nullptr;
  if (entry->IsStale(now, network_changes_))
    return nullptr;

  entry->CountHit(/* hit_is_stale= */ false);
  return entry;
}

const HostCache::Entry* HostCache::LookupStale(
    const Key& key,
    base::TimeTicks now,
    HostCache::EntryStaleness* stale_out) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (caching_is_disabled())
    return nullptr;

  HostCache::Entry* entry = LookupInternal(key);
  if (!entry)
    return nullptr;

  bool is_stale = entry->IsStale(now, network_changes_);
  entry->CountHit(/* hit_is_stale= */ is_stale);

  if (stale_out)
    entry->GetStaleness(now, network_changes_, stale_out);
  return entry;
}

HostCache::Entry* HostCache::LookupInternal(const Key& key) {
  auto it = entries_.find(key);
  return (it != entries_.end()) ? &it->second : nullptr;
}

void HostCache::Set(const Key& key,
                    const Entry& entry,
                    base::TimeTicks now,
                    base::TimeDelta ttl) {
  TRACE_EVENT0(NetTracingCategory(), "HostCache::Set");
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (caching_is_disabled())
    return;

  bool result_changed = false;
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    base::Optional<AddressListDeltaType> addresses_delta;
    if (entry.addresses() || it->second.addresses()) {
      if (entry.addresses() && it->second.addresses()) {
        addresses_delta = FindAddressListDeltaType(
            it->second.addresses().value(), entry.addresses().value());
      } else {
        addresses_delta = DELTA_DISJOINT;
      }
    }  // Else no addresses in old or new, so nullopt delta.

    // For non-address results, delta is only considered for whole-list
    // equality. The meaning of partial list equality varies too much depending
    // on the context of a DNS record.
    base::Optional<AddressListDeltaType> nonaddress_delta;
    if (entry.text_records() || it->second.text_records() ||
        entry.hostnames() || it->second.hostnames()) {
      if (entry.text_records() == it->second.text_records() &&
          entry.hostnames() == it->second.hostnames()) {
        nonaddress_delta = DELTA_IDENTICAL;
      } else if (entry.text_records() == it->second.text_records() ||
                 entry.hostnames() == it->second.hostnames()) {
        nonaddress_delta = DELTA_OVERLAP;
      } else {
        nonaddress_delta = DELTA_DISJOINT;
      }
    }  // Else no nonaddress results in old or new, so nullopt delta.

    AddressListDeltaType overall_delta;
    if (!addresses_delta && !nonaddress_delta) {
      // No results in old or new is IDENTICAL.
      overall_delta = DELTA_IDENTICAL;
    } else if (!addresses_delta) {
      overall_delta = nonaddress_delta.value();
    } else if (!nonaddress_delta) {
      overall_delta = addresses_delta.value();
    } else if (addresses_delta == DELTA_DISJOINT &&
               nonaddress_delta == DELTA_DISJOINT) {
      overall_delta = DELTA_DISJOINT;
    } else if (addresses_delta == DELTA_DISJOINT ||
               nonaddress_delta == DELTA_DISJOINT) {
      // If only some result types are DISJOINT, some match and we have OVERLAP.
      overall_delta = DELTA_OVERLAP;
    } else {
      // No DISJOINT result types, so we have at least partial match.  Take the
      // least matching amount (highest enum value).
      overall_delta =
          std::max(addresses_delta.value(), nonaddress_delta.value());
    }

    // TODO(juliatuttle): Remember some old metadata (hit count or frequency or
    // something like that) if it's useful for better eviction algorithms?
    result_changed =
        entry.error() == OK && (it->second.error() != entry.error() ||
                                overall_delta != DELTA_IDENTICAL);
    entries_.erase(it);
  } else {
    result_changed = true;
    if (size() == max_entries_)
      EvictOneEntry(now);
  }

  AddEntry(Key(key), Entry(entry, now, ttl, network_changes_));

  if (delegate_ && result_changed)
    delegate_->ScheduleWrite();
}

void HostCache::AddEntry(const Key& key, Entry&& entry) {
  DCHECK_GT(max_entries_, size());
  DCHECK_EQ(0u, entries_.count(key));
  entries_.emplace(key, std::move(entry));
  DCHECK_GE(max_entries_, size());
}

void HostCache::OnNetworkChange() {
  ++network_changes_;
}

void HostCache::set_persistence_delegate(PersistenceDelegate* delegate) {
  // A PersistenceDelegate shouldn't be added if there already was one, and
  // shouldn't be removed (by setting to nullptr) if it wasn't previously there.
  DCHECK_NE(delegate == nullptr, delegate_ == nullptr);
  delegate_ = delegate;
}

void HostCache::clear() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  // Don't bother scheduling a write if there's nothing to clear.
  if (size() == 0)
    return;

  entries_.clear();
  if (delegate_)
    delegate_->ScheduleWrite();
}

void HostCache::ClearForHosts(
    const base::Callback<bool(const std::string&)>& host_filter) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  if (host_filter.is_null()) {
    clear();
    return;
  }

  bool changed = false;
  for (auto it = entries_.begin(); it != entries_.end();) {
    auto next_it = std::next(it);

    if (host_filter.Run(it->first.hostname)) {
      entries_.erase(it);
      changed = true;
    }

    it = next_it;
  }

  if (delegate_ && changed)
    delegate_->ScheduleWrite();
}

void HostCache::GetAsListValue(base::ListValue* entry_list,
                               bool include_staleness) const {
  DCHECK(entry_list);
  entry_list->Clear();

  for (const auto& pair : entries_) {
    const Key& key = pair.first;
    const Entry& entry = pair.second;

    auto entry_dict = std::make_unique<base::DictionaryValue>(
        entry.GetAsValue(include_staleness));

    entry_dict->SetString(kHostnameKey, key.hostname);
    entry_dict->SetInteger(kDnsQueryTypeKey,
                           static_cast<int>(key.dns_query_type));
    entry_dict->SetInteger(kFlagsKey, key.host_resolver_flags);
    entry_dict->SetInteger(kHostResolverSourceKey,
                           static_cast<int>(key.host_resolver_source));

    entry_list->Append(std::move(entry_dict));
  }
}

bool HostCache::RestoreFromListValue(const base::ListValue& old_cache) {
  for (auto it = old_cache.begin(); it != old_cache.end(); it++) {
    const base::DictionaryValue* entry_dict;
    if (!it->GetAsDictionary(&entry_dict))
      return false;

    std::string hostname;
    HostResolverFlags flags;
    std::string expiration;
    if (!entry_dict->GetString(kHostnameKey, &hostname) ||
        !entry_dict->GetInteger(kFlagsKey, &flags) ||
        !entry_dict->GetString(kExpirationKey, &expiration)) {
      return false;
    }

    // If there is no DnsQueryType, look for an AddressFamily.
    //
    // TODO(crbug.com/846423): Remove kAddressFamilyKey support after a enough
    // time has passed to minimize loss-of-persistence impact from backwards
    // incompatibility.
    int dns_query_type_val;
    DnsQueryType dns_query_type;
    if (entry_dict->GetInteger(kDnsQueryTypeKey, &dns_query_type_val)) {
      dns_query_type = static_cast<DnsQueryType>(dns_query_type_val);
    } else {
      int address_family;
      if (!entry_dict->GetInteger(kAddressFamilyKey, &address_family)) {
        return false;
      }
      dns_query_type = AddressFamilyToDnsQueryType(
          static_cast<AddressFamily>(address_family));
    }

    // HostResolverSource is optional.
    int host_resolver_source;
    if (!entry_dict->GetInteger(kHostResolverSourceKey,
                                &host_resolver_source)) {
      host_resolver_source = static_cast<int>(HostResolverSource::ANY);
    }

    int error = OK;
    const base::ListValue* addresses_value = nullptr;
    const base::ListValue* text_records_value = nullptr;
    const base::ListValue* hostname_records_value = nullptr;
    const base::ListValue* host_ports_value = nullptr;
    if (!entry_dict->GetInteger(kErrorKey, &error)) {
      entry_dict->GetList(kAddressesKey, &addresses_value);
      entry_dict->GetList(kTextRecordsKey, &text_records_value);

      if (entry_dict->GetList(kHostnameResultsKey, &hostname_records_value) !=
          entry_dict->GetList(kHostPortsKey, &host_ports_value)) {
        return false;
      }
    }

    int64_t time_internal;
    if (!base::StringToInt64(expiration, &time_internal))
      return false;

    base::TimeTicks expiration_time =
        tick_clock_->NowTicks() -
        (base::Time::Now() - base::Time::FromInternalValue(time_internal));

    base::Optional<AddressList> address_list;
    if (!AddressListFromListValue(addresses_value, &address_list)) {
      return false;
    }

    base::Optional<std::vector<std::string>> text_records;
    if (text_records_value) {
      text_records.emplace();
      for (const base::Value& value : text_records_value->GetList()) {
        if (!value.is_string())
          return false;
        text_records.value().push_back(value.GetString());
      }
    }

    base::Optional<std::vector<HostPortPair>> hostname_records;
    if (hostname_records_value) {
      DCHECK(host_ports_value);
      if (hostname_records_value->GetList().size() !=
          host_ports_value->GetList().size()) {
        return false;
      }

      hostname_records.emplace();
      for (size_t i = 0; i < hostname_records_value->GetList().size(); ++i) {
        if (!hostname_records_value->GetList()[i].is_string() ||
            !host_ports_value->GetList()[i].is_int() ||
            !base::IsValueInRangeForNumericType<uint16_t>(
                host_ports_value->GetList()[i].GetInt())) {
          return false;
        }
        hostname_records.value().push_back(
            HostPortPair(hostname_records_value->GetList()[i].GetString(),
                         base::checked_cast<uint16_t>(
                             host_ports_value->GetList()[i].GetInt())));
      }
    }

    // Assume an empty address list if we have an address type and no results.
    if (IsAddressType(dns_query_type) && !address_list && !text_records &&
        !hostname_records) {
      address_list.emplace();
    }

    Key key(hostname, dns_query_type, flags,
            static_cast<HostResolverSource>(host_resolver_source));

    // If the key is already in the cache, assume it's more recent and don't
    // replace the entry. If the cache is already full, don't bother
    // prioritizing what to evict, just stop restoring.
    auto found = entries_.find(key);
    if (found == entries_.end() && size() < max_entries_) {
      AddEntry(key, Entry(error, address_list, std::move(text_records),
                          std::move(hostname_records), Entry::SOURCE_UNKNOWN,
                          expiration_time, network_changes_ - 1));
    }
  }
  restore_size_ = old_cache.GetSize();
  return true;
}

size_t HostCache::size() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return entries_.size();
}

size_t HostCache::max_entries() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return max_entries_;
}

// static
std::unique_ptr<HostCache> HostCache::CreateDefaultCache() {
#if defined(ENABLE_BUILT_IN_DNS)
  const size_t kDefaultMaxEntries = 1000;
#else
  const size_t kDefaultMaxEntries = 100;
#endif
  return std::make_unique<HostCache>(kDefaultMaxEntries);
}

void HostCache::EvictOneEntry(base::TimeTicks now) {
  DCHECK_LT(0u, entries_.size());

  auto oldest_it = entries_.begin();
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if ((it->second.expires() < oldest_it->second.expires()) &&
        (it->second.IsStale(now, network_changes_) ||
         !oldest_it->second.IsStale(now, network_changes_))) {
      oldest_it = it;
    }
  }

  entries_.erase(oldest_it);
}

bool HostCache::HasEntry(base::StringPiece hostname,
                         HostCache::Entry::Source* source_out,
                         HostCache::EntryStaleness* stale_out) {
  net::HostCache::Key cache_key;
  hostname.CopyToString(&cache_key.hostname);

  const HostCache::Entry* entry =
      LookupStale(cache_key, tick_clock_->NowTicks(), stale_out);
  if (!entry && IsAddressType(cache_key.dns_query_type)) {
    // Might not have found the cache entry because the address_family or
    // host_resolver_flags in cache_key do not match those used for the
    // original DNS lookup. Try another common combination of address_family
    // and host_resolver_flags in an attempt to find a matching cache entry.
    cache_key.dns_query_type = DnsQueryType::A;
    cache_key.host_resolver_flags =
        net::HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6;
    entry = LookupStale(cache_key, tick_clock_->NowTicks(), stale_out);
    if (!entry)
      return false;
  }

  if (source_out != nullptr)
    *source_out = entry->source();

  return true;
}

}  // namespace net
