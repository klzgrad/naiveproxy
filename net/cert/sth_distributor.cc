// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/sth_distributor.h"

#include "base/metrics/histogram_macros.h"
#include "base/time/time.h"
#include "net/cert/signed_tree_head.h"

namespace {
const uint8_t kPilotLogID[] = {0xa4, 0xb9, 0x09, 0x90, 0xb4, 0x18, 0x58, 0x14,
                               0x87, 0xbb, 0x13, 0xa2, 0xcc, 0x67, 0x70, 0x0a,
                               0x3c, 0x35, 0x98, 0x04, 0xf9, 0x1b, 0xdf, 0xb8,
                               0xe3, 0x77, 0xcd, 0x0e, 0xc8, 0x0d, 0xdc, 0x10};
}

namespace net {

namespace ct {

STHDistributor::STHDistributor()
    : observer_list_(base::ObserverListPolicy::EXISTING_ONLY) {}

STHDistributor::~STHDistributor() = default;

void STHDistributor::NewSTHObserved(const SignedTreeHead& sth) {
  auto it = std::find_if(observed_sths_.begin(), observed_sths_.end(),
                         [&sth](const SignedTreeHead& other) {
                           return sth.log_id == other.log_id;
                         });

  if (it == observed_sths_.end())
    observed_sths_.push_back(sth);
  else
    *it = sth;

  for (auto& observer : observer_list_)
    observer.NewSTHObserved(sth);

  if (sth.log_id.compare(0, sth.log_id.size(),
                         reinterpret_cast<const char*>(kPilotLogID),
                         sizeof(kPilotLogID)) != 0)
    return;

  const base::TimeDelta sth_age = base::Time::Now() - sth.timestamp;
  UMA_HISTOGRAM_CUSTOM_TIMES("Net.CertificateTransparency.PilotSTHAge", sth_age,
                             base::TimeDelta::FromHours(1),
                             base::TimeDelta::FromDays(4), 100);
}

void STHDistributor::RegisterObserver(STHObserver* observer) {
  observer_list_.AddObserver(observer);
  // Make a local copy, because notifying the |observer| of a
  // new STH may result in this class being notified of a
  // (different) new STH, thus invalidating the iterator.
  std::vector<SignedTreeHead> local_sths(observed_sths_);

  for (const auto& sth : local_sths)
    observer->NewSTHObserved(sth);
}

void STHDistributor::UnregisterObserver(STHObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

}  // namespace ct

}  // namespace net
