// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_KNOWN_ROOTS_NSS_H_
#define NET_CERT_KNOWN_ROOTS_NSS_H_

typedef struct CERTCertificateStr CERTCertificate;

namespace net {

// IsKnownRoot returns true if the given certificate is one that we believe
// is a standard (as opposed to user-installed) root.
bool IsKnownRoot(CERTCertificate* root);

}  // namespace net

#endif  // NET_CERT_KNOWN_ROOTS_NSS_H_
