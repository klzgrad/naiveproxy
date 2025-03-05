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

#include "ocsp_verify_result.h"

BSSL_NAMESPACE_BEGIN

OCSPVerifyResult::OCSPVerifyResult() = default;
OCSPVerifyResult::OCSPVerifyResult(const OCSPVerifyResult &) = default;
OCSPVerifyResult::~OCSPVerifyResult() = default;

bool OCSPVerifyResult::operator==(const OCSPVerifyResult &other) const {
  if (response_status != other.response_status) {
    return false;
  }

  if (response_status == PROVIDED) {
    // |revocation_status| is only defined when |response_status| is PROVIDED.
    return revocation_status == other.revocation_status;
  }
  return true;
}

BSSL_NAMESPACE_END
