// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/crl_set.h"

#include "base/logging.h"
#include "base/time/time.h"

namespace net {

CRLSet::CRLSet()
    : sequence_(0),
      not_after_(0) {
}

CRLSet::~CRLSet() {
}

CRLSet::Result CRLSet::CheckSPKI(const base::StringPiece& spki_hash) const {
  for (std::vector<std::string>::const_iterator i = blocked_spkis_.begin();
       i != blocked_spkis_.end(); ++i) {
    if (spki_hash.size() == i->size() &&
        memcmp(spki_hash.data(), i->data(), i->size()) == 0) {
      return REVOKED;
    }
  }

  return GOOD;
}

CRLSet::Result CRLSet::CheckSerial(
    const base::StringPiece& serial_number,
    const base::StringPiece& issuer_spki_hash) const {
  base::StringPiece serial(serial_number);

  if (!serial.empty() && (serial[0] & 0x80) != 0) {
    // This serial number is negative but the process which generates CRL sets
    // will reject any certificates with negative serial numbers as invalid.
    return UNKNOWN;
  }

  // Remove any leading zero bytes.
  while (serial.size() > 1 && serial[0] == 0x00)
    serial.remove_prefix(1);

  std::unordered_map<std::string, size_t>::const_iterator crl_index =
      crls_index_by_issuer_.find(issuer_spki_hash.as_string());
  if (crl_index == crls_index_by_issuer_.end())
    return UNKNOWN;
  const std::vector<std::string>& serials = crls_[crl_index->second].second;

  for (std::vector<std::string>::const_iterator i = serials.begin();
       i != serials.end(); ++i) {
    if (base::StringPiece(*i) == serial)
      return REVOKED;
  }

  return GOOD;
}

bool CRLSet::IsExpired() const {
  if (not_after_ == 0)
    return false;

  uint64_t now = base::Time::Now().ToTimeT();
  return now > not_after_;
}

uint32_t CRLSet::sequence() const {
  return sequence_;
}

const CRLSet::CRLList& CRLSet::crls() const {
  return crls_;
}

// static
CRLSet* CRLSet::EmptyCRLSetForTesting() {
  return ForTesting(false, NULL, "");
}

CRLSet* CRLSet::ExpiredCRLSetForTesting() {
  return ForTesting(true, NULL, "");
}

// static
CRLSet* CRLSet::ForTesting(bool is_expired,
                           const SHA256HashValue* issuer_spki,
                           const std::string& serial_number) {
  CRLSet* crl_set = new CRLSet;
  if (is_expired)
    crl_set->not_after_ = 1;
  if (issuer_spki != NULL) {
    const std::string spki(reinterpret_cast<const char*>(issuer_spki->data),
                           sizeof(issuer_spki->data));
    crl_set->crls_.push_back(make_pair(spki, std::vector<std::string>()));
    crl_set->crls_index_by_issuer_[spki] = 0;
  }

  if (!serial_number.empty())
    crl_set->crls_[0].second.push_back(serial_number);

  return crl_set;
}

}  // namespace net
