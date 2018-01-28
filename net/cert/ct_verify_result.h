// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CT_VERIFY_RESULT_H_
#define NET_CERT_CT_VERIFY_RESULT_H_

#include <vector>

#include "net/base/net_export.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/signed_certificate_timestamp_and_status.h"

namespace net {

namespace ct {

enum class CertPolicyCompliance;

typedef std::vector<scoped_refptr<SignedCertificateTimestamp> > SCTList;

// Holds Signed Certificate Timestamps, depending on their verification
// results, and information about CT policies that were applied on the
// connection.
struct NET_EXPORT CTVerifyResult {
  CTVerifyResult();
  CTVerifyResult(const CTVerifyResult& other);
  ~CTVerifyResult();

  // All SCTs and their statuses
  SignedCertificateTimestampAndStatusList scts;

  // True if any CT policies were applied on this connection.
  bool ct_policies_applied;
  // The result of evaluating whether the connection complies with the
  // CT certificate policy.
  CertPolicyCompliance cert_policy_compliance;
};

// Returns a list of SCTs from |sct_and_status_list| whose status matches
// |match_status|.
SCTList NET_EXPORT SCTsMatchingStatus(
    const SignedCertificateTimestampAndStatusList& sct_and_status_list,
    SCTVerifyStatus match_status);

}  // namespace ct

}  // namespace net

#endif  // NET_CERT_CT_VERIFY_RESULT_H_
