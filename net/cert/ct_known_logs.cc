// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/ct_known_logs.h"

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <iterator>

#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "crypto/sha2.h"

#if !defined(OS_NACL)
#include "net/cert/ct_log_verifier.h"
#endif

namespace net {

namespace ct {

namespace {

#include "net/data/ssl/certificate_transparency/log_list-inc.cc"

}  // namespace

#if !defined(OS_NACL)
std::vector<scoped_refptr<const CTLogVerifier>>
CreateLogVerifiersForKnownLogs() {
  std::vector<scoped_refptr<const CTLogVerifier>> verifiers;

  // Add all qualified logs.
  for (const auto& log : kCTLogList) {
    base::StringPiece key(log.log_key, log.log_key_length);
    verifiers.push_back(CTLogVerifier::Create(key, log.log_name, log.log_url,
                                              log.log_dns_domain));
    // Make sure no null logs enter verifiers. Parsing of all known logs should
    // succeed.
    CHECK(verifiers.back().get());
  }

  // Add all disqualified logs. Callers are expected to filter verified SCTs
  // via IsLogQualified().
  for (const auto& disqualified_log : kDisqualifiedCTLogList) {
    const CTLogInfo& log = disqualified_log.log_info;
    base::StringPiece key(log.log_key, log.log_key_length);
    verifiers.push_back(CTLogVerifier::Create(key, log.log_name, log.log_url,
                                              log.log_dns_domain));
    // Make sure no null logs enter verifiers. Parsing of all known logs should
    // succeed.
    CHECK(verifiers.back().get());
  }

  return verifiers;
}
#endif

bool IsLogOperatedByGoogle(base::StringPiece log_id) {
  CHECK_EQ(log_id.size(), crypto::kSHA256Length);

  return std::binary_search(std::begin(kGoogleLogIDs), std::end(kGoogleLogIDs),
                            log_id.data(), [](const char* a, const char* b) {
                              return memcmp(a, b, crypto::kSHA256Length) < 0;
                            });
}

bool IsLogDisqualified(base::StringPiece log_id,
                       base::Time* disqualification_date) {
  CHECK_EQ(log_id.size(), arraysize(kDisqualifiedCTLogList[0].log_id) - 1);

  auto* p = std::lower_bound(
      std::begin(kDisqualifiedCTLogList), std::end(kDisqualifiedCTLogList),
      log_id.data(),
      [](const DisqualifiedCTLogInfo& disqualified_log, const char* log_id) {
        return memcmp(disqualified_log.log_id, log_id, crypto::kSHA256Length) <
               0;
      });
  if (p == std::end(kDisqualifiedCTLogList) ||
      memcmp(p->log_id, log_id.data(), crypto::kSHA256Length) != 0) {
    return false;
  }

  *disqualification_date = base::Time::UnixEpoch() + p->disqualification_date;
  return true;
}

}  // namespace ct

}  // namespace net
