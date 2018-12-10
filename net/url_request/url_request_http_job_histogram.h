// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_HTTP_JOB_HISTOGRAM_H_
#define NET_URL_REQUEST_URL_REQUEST_HTTP_JOB_HISTOGRAM_H_

namespace net {

// Degree of protection against cookie theft in decreasing order (split by 1st
// party and 3rd party cookies).
//
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. First-party entries need to reside at
// even values and the corresponding third-party entry needs to be at
// [first-party] + 1 to allow bit manipulation.
enum class CookieNetworkSecurity {
  k1pSecureAttribute = 0,                 // Secure attribute
  k3pSecureAttribute = 1,                 // "
  k1pHSTSHostCookie = 2,                  // HSTS covering cookie lifetime
  k3pHSTSHostCookie = 3,                  //   host cookie
  k1pHSTSSubdomainsIncluded = 4,          // HSTS covering cookie lifetime
  k3pHSTSSubdomainsIncluded = 5,          //   subdomains included
  k1pExpiringHSTSHostCookie = 6,          // HSTS not covering cookie lifetime
  k3pExpiringHSTSHostCookie = 7,          //   host cookie
  k1pExpiringHSTSSubdomainsIncluded = 8,  // HSTS not covering cookie lifetime
  k3pExpiringHSTSSubdomainsIncluded = 9,  //   subdomains included
  k1pHSTSSpoofable = 10,                  // HSTS and neither host cookie nor
  k3pHSTSSpoofable = 11,                  //   subdomains included
  k1pSecureConnection = 12,               // Secure connection but no HSTS
  k3pSecureConnection = 13,               // "
  k1pNonsecureConnection = 14,            // Nonsecure connection
  k3pNonsecureConnection = 15,            // "
  kCount
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_HTTP_JOB_HISTOGRAM_H_