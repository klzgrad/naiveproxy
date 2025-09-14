/*
 * NSS utility functions
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NET_THIRD_PARTY_NSS_SSL_CMPCERT_H_
#define NET_THIRD_PARTY_NSS_SSL_CMPCERT_H_

#include <cert.h>

#include <string>
#include <vector>

#include "net/cert/scoped_nss_types.h"

namespace net {

class X509Certificate;

// Checks if |cert| matches |cert_authorities|. If so, it sets |*intermediates|
// to a list of intermediates to send and returns true. Otherwise, it returns
// false.
bool MatchClientCertificateIssuers(
    X509Certificate* cert,
    const std::vector<std::string>& cert_authorities,
    ScopedCERTCertificateList* intermediates);

}  // namespace net

#endif  // NET_THIRD_PARTY_NSS_SSL_CMPCERT_H_
