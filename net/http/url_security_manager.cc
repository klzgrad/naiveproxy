// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/url_security_manager.h"

#include <utility>

#include "net/http/http_auth_filter.h"

namespace net {

URLSecurityManagerWhitelist::URLSecurityManagerWhitelist() {}

URLSecurityManagerWhitelist::~URLSecurityManagerWhitelist() {}

bool URLSecurityManagerWhitelist::CanUseDefaultCredentials(
    const GURL& auth_origin) const  {
  if (whitelist_default_.get())
    return whitelist_default_->IsValid(auth_origin, HttpAuth::AUTH_SERVER);
  return false;
}

bool URLSecurityManagerWhitelist::CanDelegate(const GURL& auth_origin) const {
  if (whitelist_delegate_.get())
    return whitelist_delegate_->IsValid(auth_origin, HttpAuth::AUTH_SERVER);
  return false;
}

void URLSecurityManagerWhitelist::SetDefaultWhitelist(
    std::unique_ptr<HttpAuthFilter> whitelist_default) {
  whitelist_default_ = std::move(whitelist_default);
}

void URLSecurityManagerWhitelist::SetDelegateWhitelist(
    std::unique_ptr<HttpAuthFilter> whitelist_delegate) {
  whitelist_delegate_ = std::move(whitelist_delegate);
}

bool URLSecurityManagerWhitelist::HasDefaultWhitelist() const {
  return whitelist_default_.get() != nullptr;
}

}  //  namespace net
