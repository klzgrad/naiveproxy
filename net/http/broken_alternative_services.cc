// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/broken_alternative_services.h"

#include "base/memory/singleton.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "net/http/http_server_properties_impl.h"

namespace net {

namespace {

// Initial delay for broken alternative services.
const uint64_t kBrokenAlternativeProtocolDelaySecs = 300;
// Subsequent failures result in exponential (base 2) backoff.
// Limit binary shift to limit delay to approximately 2 days.
const int kBrokenDelayMaxShift = 9;

base::TimeDelta ComputeBrokenAlternativeServiceExpirationDelay(
    int broken_count) {
  DCHECK_GE(broken_count, 0);
  if (broken_count > kBrokenDelayMaxShift)
    broken_count = kBrokenDelayMaxShift;
  return base::TimeDelta::FromSeconds(kBrokenAlternativeProtocolDelaySecs) *
         (1 << broken_count);
}

}  // namespace

BrokenAlternativeServices::BrokenAlternativeServices(Delegate* delegate,
                                                     base::TickClock* clock)
    : delegate_(delegate),
      clock_(clock),
      weak_ptr_factory_(this) {
  DCHECK(delegate_);
  DCHECK(clock_);
}

BrokenAlternativeServices::~BrokenAlternativeServices() = default;

void BrokenAlternativeServices::Clear() {
  expiration_timer_.Stop();
  broken_alternative_service_list_.clear();
  broken_alternative_service_map_.clear();
  recently_broken_alternative_services_.Clear();
}

void BrokenAlternativeServices::MarkAlternativeServiceBroken(
    const AlternativeService& alternative_service) {
  // Empty host means use host of origin, callers are supposed to substitute.
  DCHECK(!alternative_service.host.empty());
  DCHECK_NE(kProtoUnknown, alternative_service.protocol);

  auto it = recently_broken_alternative_services_.Get(alternative_service);
  int broken_count = 0;
  if (it == recently_broken_alternative_services_.end()) {
    recently_broken_alternative_services_.Put(alternative_service, 1);
  } else {
    broken_count = it->second++;
  }
  base::TimeTicks expiration =
      clock_->NowTicks() +
      ComputeBrokenAlternativeServiceExpirationDelay(broken_count);
  // Return if alternative service is already in expiration queue.
  BrokenAlternativeServiceList::iterator list_it;
  if (!AddToBrokenAlternativeServiceListAndMap(alternative_service, expiration,
                                               &list_it)) {
    return;
  }

  // If this is now the first entry in the list (i.e. |alternative_service| is
  // the next alt svc to expire), schedule an expiration task for it.
  if (list_it == broken_alternative_service_list_.begin()) {
    ScheduleBrokenAlternateProtocolMappingsExpiration();
  }
}

void BrokenAlternativeServices::MarkAlternativeServiceRecentlyBroken(
    const AlternativeService& alternative_service) {
  DCHECK_NE(kProtoUnknown, alternative_service.protocol);
  if (recently_broken_alternative_services_.Get(alternative_service) ==
      recently_broken_alternative_services_.end()) {
    recently_broken_alternative_services_.Put(alternative_service, 1);
  }
}

bool BrokenAlternativeServices::IsAlternativeServiceBroken(
    const AlternativeService& alternative_service) const {
  // Empty host means use host of origin, callers are supposed to substitute.
  DCHECK(!alternative_service.host.empty());
  return broken_alternative_service_map_.find(alternative_service) !=
         broken_alternative_service_map_.end();
}

bool BrokenAlternativeServices::WasAlternativeServiceRecentlyBroken(
    const AlternativeService& alternative_service) {
  DCHECK(!alternative_service.host.empty());
  return recently_broken_alternative_services_.Get(alternative_service) !=
             recently_broken_alternative_services_.end() ||
         broken_alternative_service_map_.find(alternative_service) !=
             broken_alternative_service_map_.end();
}

void BrokenAlternativeServices::ConfirmAlternativeService(
    const AlternativeService& alternative_service) {
  DCHECK_NE(kProtoUnknown, alternative_service.protocol);

  // Remove |alternative_service| from |alternative_service_list_| and
  // |alternative_service_map_|.
  auto map_it = broken_alternative_service_map_.find(alternative_service);
  if (map_it != broken_alternative_service_map_.end()) {
    broken_alternative_service_list_.erase(map_it->second);
    broken_alternative_service_map_.erase(map_it);
  }

  auto it = recently_broken_alternative_services_.Get(alternative_service);
  if (it != recently_broken_alternative_services_.end()) {
    recently_broken_alternative_services_.Erase(it);
  }
}

void BrokenAlternativeServices::SetBrokenAndRecentlyBrokenAlternativeServices(
    std::unique_ptr<BrokenAlternativeServiceList>
        broken_alternative_service_list,
    std::unique_ptr<RecentlyBrokenAlternativeServices>
        recently_broken_alternative_services) {
  DCHECK(broken_alternative_service_list);
  DCHECK(recently_broken_alternative_services);

  base::TimeTicks next_expiration =
      broken_alternative_service_list_.empty()
          ? base::TimeTicks::Max()
          : broken_alternative_service_list_.front().second;

  // Add |recently_broken_alternative_services| to
  // |recently_broken_alternative_services_|.
  // If an alt-svc already exists, overwrite its broken-count to the one in
  // |recently_broken_alternative_services|.

  recently_broken_alternative_services_.Swap(
      *recently_broken_alternative_services);
  // Add back all existing recently broken alt svcs to cache so they're at
  // front of recency list (MRUCache::Get() does this automatically).
  for (auto it = recently_broken_alternative_services->rbegin();
       it != recently_broken_alternative_services->rend(); ++it) {
    if (recently_broken_alternative_services_.Get(it->first) ==
        recently_broken_alternative_services_.end()) {
      recently_broken_alternative_services_.Put(it->first, it->second);
    }
  }

  // Append |broken_alternative_service_list| to
  // |broken_alternative_service_list_|
  size_t num_broken_alt_svcs_added = broken_alternative_service_list->size();
  broken_alternative_service_list_.splice(
      broken_alternative_service_list_.begin(),
      *broken_alternative_service_list);
  // For each newly-appended alt svc in |broken_alternative_service_list_|,
  // add an entry to |broken_alternative_service_map_| that points to its
  // list iterator. Also, add an entry for that alt svc in
  // |recently_broken_alternative_services_| if one doesn't exist.
  auto list_it = broken_alternative_service_list_.begin();
  for (size_t i = 0; i < num_broken_alt_svcs_added; ++i) {
    const AlternativeService& alternative_service = list_it->first;
    auto map_it = broken_alternative_service_map_.find(alternative_service);
    if (map_it != broken_alternative_service_map_.end()) {
      // Implies this entry already exists somewhere else in
      // |broken_alternative_service_list_|. Remove the existing entry from
      // |broken_alternative_service_list_|, and update the
      // |broken_alternative_service_map_| entry to point to this list entry
      // instead.
      auto list_existing_entry_it = map_it->second;
      broken_alternative_service_list_.erase(list_existing_entry_it);
      map_it->second = list_it;
    } else {
      broken_alternative_service_map_.insert(
          std::make_pair(alternative_service, list_it));
    }

    if (recently_broken_alternative_services_.Peek(alternative_service) ==
        recently_broken_alternative_services_.end()) {
      recently_broken_alternative_services_.Put(alternative_service, 1);
    }

    ++list_it;
  }

  // Sort |broken_alternative_service_list_| by expiration time. This operation
  // does not invalidate list iterators, so |broken_alternative_service_map_|
  // does not need to be updated.
  broken_alternative_service_list_.sort(
      [](const std::pair<AlternativeService, base::TimeTicks>& lhs,
         const std::pair<AlternativeService, base::TimeTicks>& rhs) -> bool {
        return lhs.second < rhs.second;
      });

  base::TimeTicks new_next_expiration =
      broken_alternative_service_list_.empty()
          ? base::TimeTicks::Max()
          : broken_alternative_service_list_.front().second;

  if (new_next_expiration != next_expiration)
    ScheduleBrokenAlternateProtocolMappingsExpiration();
}

const BrokenAlternativeServiceList&
BrokenAlternativeServices::broken_alternative_service_list() const {
  return broken_alternative_service_list_;
}

const RecentlyBrokenAlternativeServices&
BrokenAlternativeServices::recently_broken_alternative_services() const {
  return recently_broken_alternative_services_;
}

bool BrokenAlternativeServices::AddToBrokenAlternativeServiceListAndMap(
    const AlternativeService& alternative_service,
    base::TimeTicks expiration,
    BrokenAlternativeServiceList::iterator* it) {
  DCHECK(it);

  auto map_it = broken_alternative_service_map_.find(alternative_service);
  if (map_it != broken_alternative_service_map_.end())
    return false;

  // Iterate from end of |broken_alternative_service_list_| to find where to
  // insert it to keep the list sorted by expiration time.
  auto list_it = broken_alternative_service_list_.end();
  while (list_it != broken_alternative_service_list_.begin()) {
    --list_it;
    if (list_it->second <= expiration) {
      ++list_it;
      break;
    }
  }

  // Insert |alternative_service| into the list and the map
  list_it = broken_alternative_service_list_.insert(
      list_it, std::make_pair(alternative_service, expiration));
  broken_alternative_service_map_.insert(
      std::make_pair(alternative_service, list_it));

  *it = list_it;
  return true;
}

void BrokenAlternativeServices::ExpireBrokenAlternateProtocolMappings() {
  base::TimeTicks now = clock_->NowTicks();

  while (!broken_alternative_service_list_.empty()) {
    auto it = broken_alternative_service_list_.begin();
    if (now < it->second) {
      break;
    }

    delegate_->OnExpireBrokenAlternativeService(it->first);

    broken_alternative_service_map_.erase(it->first);
    broken_alternative_service_list_.erase(it);
  }

  if (!broken_alternative_service_list_.empty())
    ScheduleBrokenAlternateProtocolMappingsExpiration();
}

void BrokenAlternativeServices ::
    ScheduleBrokenAlternateProtocolMappingsExpiration() {
  DCHECK(!broken_alternative_service_list_.empty());
  base::TimeTicks now = clock_->NowTicks();
  base::TimeTicks next_expiration =
      broken_alternative_service_list_.front().second;
  base::TimeDelta delay =
      next_expiration > now ? next_expiration - now : base::TimeDelta();
  expiration_timer_.Stop();
  expiration_timer_.Start(
      FROM_HERE, delay,
      base::Bind(
          &BrokenAlternativeServices ::ExpireBrokenAlternateProtocolMappings,
          weak_ptr_factory_.GetWeakPtr()));
}

}  // namespace net