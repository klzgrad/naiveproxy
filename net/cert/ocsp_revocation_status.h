// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_OCSP_REVOCATION_STATUS_H_
#define NET_CERT_OCSP_REVOCATION_STATUS_H_

namespace net {

enum class OCSPRevocationStatus {
  GOOD,
  REVOKED,
  UNKNOWN,
};

}  // namespace net

#endif  // NET_CERT_OCSP_REVOCATION_STATUS_H_
