// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/public/resolve_error_info.h"

namespace net {

ResolveErrorInfo::ResolveErrorInfo() {}

ResolveErrorInfo::ResolveErrorInfo(int resolve_error) {
  error = resolve_error;
}

ResolveErrorInfo::ResolveErrorInfo(const ResolveErrorInfo& resolve_error_info) =
    default;

ResolveErrorInfo::ResolveErrorInfo(ResolveErrorInfo&& other) = default;

ResolveErrorInfo& ResolveErrorInfo::operator=(const ResolveErrorInfo& other) =
    default;

ResolveErrorInfo& ResolveErrorInfo::operator=(ResolveErrorInfo&& other) =
    default;

bool ResolveErrorInfo::operator==(const ResolveErrorInfo& other) const {
  return error == other.error;
}

bool ResolveErrorInfo::operator!=(const ResolveErrorInfo& other) const {
  return !(*this == other);
}

}  // namespace net
