// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CT_POLICY_ENFORCER_H_
#define NET_CERT_CT_POLICY_ENFORCER_H_

#include <stddef.h>
#include <vector>

#include "net/base/net_export.h"
#include "net/cert/signed_certificate_timestamp.h"

namespace net {

class NetLogWithSource;

namespace ct {

enum class CTPolicyCompliance;

}  // namespace ct

class X509Certificate;

using SCTList = std::vector<scoped_refptr<ct::SignedCertificateTimestamp>>;

// Class for checking that a given certificate conforms to
// Certificate Transparency-related policies.
class NET_EXPORT CTPolicyEnforcer {
 public:
  CTPolicyEnforcer() {}
  virtual ~CTPolicyEnforcer() {}

  // Returns the CT certificate policy compliance status for a given
  // certificate and collection of SCTs.
  // |cert| is the certificate for which to check compliance, and
  // ||verified_scts| contains any/all SCTs associated with |cert| that
  // |have been verified (well-formed, issued by known logs, and
  // |applying to |cert|).
  virtual ct::CTPolicyCompliance CheckCompliance(
      X509Certificate* cert,
      const SCTList& verified_scts,
      const NetLogWithSource& net_log);
};

}  // namespace net

#endif  // NET_CERT_CT_POLICY_ENFORCER_H_
