// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_OCSP_VERIFY_RESULT_H_
#define NET_CERT_OCSP_VERIFY_RESULT_H_

#include <string>

#include "net/base/net_export.h"
#include "net/cert/ocsp_revocation_status.h"

namespace net {

// The result of OCSP verification. This always contains a ResponseStatus, which
// describes whether or not an OCSP response was provided, and response level
// errors. It optionally contains an OCSPRevocationStatus when |response_status
// = PROVIDED|. For example, a stapled OCSP response matching the certificate,
// and indicating a non-revoked status, will have |response_status = PROVIDED|
// and |revocation_status = GOOD|. This is populated as part of the certificate
// verification process, and should not be modified at other layers.
struct NET_EXPORT OCSPVerifyResult {
  OCSPVerifyResult();
  OCSPVerifyResult(const OCSPVerifyResult&);
  ~OCSPVerifyResult();

  bool operator==(const OCSPVerifyResult& other) const;

  enum ResponseStatus {
    // OCSP verification was not checked on this connection.
    NOT_CHECKED,

    // No OCSPResponse was stapled.
    MISSING,

    // An up-to-date OCSP response was stapled and matched the certificate.
    PROVIDED,

    // The stapled OCSP response did not have a SUCCESSFUL status.
    ERROR_RESPONSE,

    // The OCSPResponseData field producedAt was outside the certificate
    // validity period.
    BAD_PRODUCED_AT,

    // At least one OCSPSingleResponse was stapled, but none matched the
    // certificate.
    NO_MATCHING_RESPONSE,

    // A matching OCSPSingleResponse was stapled, but was either expired or not
    // yet valid.
    INVALID_DATE,

    // The OCSPResponse structure could not be parsed.
    PARSE_RESPONSE_ERROR,

    // The OCSPResponseData structure could not be parsed.
    PARSE_RESPONSE_DATA_ERROR,

  };

  ResponseStatus response_status = NOT_CHECKED;

  // The strictest CertStatus matching the certificate (REVOKED > UNKNOWN >
  // GOOD). Only valid if |response_status| = PROVIDED.
  OCSPRevocationStatus revocation_status = OCSPRevocationStatus::UNKNOWN;
};

}  // namespace net

#endif  // NET_CERT_OCSP_VERIFY_RESULT_H_
