// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_SYMANTEC_CERTS_H_
#define NET_CERT_SYMANTEC_CERTS_H_

#include "net/base/hash_value.h"

namespace net {

// |kSymantecRoots| contains the set of known active and legacy root
// certificates operated by Symantec Corporation. These roots are subject to
// Certificate Transparency requirements and deprecation messages. See
// <https://security.googleblog.com/2015/10/sustaining-digital-certificate-security.html>
// and
// https://security.googleblog.com/2017/09/chromes-plan-to-distrust-symantec.html
// for details about why.
//
// Independently operated sub-CAs are exempt from these policies, and are
// listed in |kSymantecExceptions|.
extern const SHA256HashValue kSymantecRoots[];
extern const size_t kSymantecRootsLength;
extern const SHA256HashValue kSymantecExceptions[];
extern const size_t kSymantecExceptionsLength;

// Returns true if |public_key_hashes| contains a certificate issued from
// Symantec's "legacy" PKI. This constraint excludes certificates that were
// issued by independently-operated subordinate CAs or from any "Managed CAs"
// that comply with
// https://security.googleblog.com/2017/09/chromes-plan-to-distrust-symantec.html.
NET_EXPORT bool IsLegacySymantecCert(const HashValueVector& public_key_hashes);

}  // namespace net

#endif  // NET_CERT_SYMANTEC_CERTS_H_
