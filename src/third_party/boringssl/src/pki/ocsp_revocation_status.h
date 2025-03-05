// Copyright 2016 The Chromium Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BSSL_PKI_OCSP_REVOCATION_STATUS_H_
#define BSSL_PKI_OCSP_REVOCATION_STATUS_H_

BSSL_NAMESPACE_BEGIN

// This value is histogrammed, so do not re-order or change values, and add
// new values at the end.
enum class OCSPRevocationStatus {
  GOOD = 0,
  REVOKED = 1,
  UNKNOWN = 2,
  MAX_VALUE = UNKNOWN
};

BSSL_NAMESPACE_END

#endif  // BSSL_PKI_OCSP_REVOCATION_STATUS_H_
