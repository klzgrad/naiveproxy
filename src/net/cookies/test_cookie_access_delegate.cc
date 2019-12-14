// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/test_cookie_access_delegate.h"

#include "net/cookies/cookie_util.h"

namespace net {

TestCookieAccessDelegate::TestCookieAccessDelegate() = default;

TestCookieAccessDelegate::~TestCookieAccessDelegate() = default;

CookieAccessSemantics TestCookieAccessDelegate::GetAccessSemantics(
    const CanonicalCookie& cookie) const {
  auto it = expectations_.find(GetKeyForDomainValue(cookie.Domain()));
  if (it != expectations_.end())
    return it->second;
  return CookieAccessSemantics::UNKNOWN;
}

void TestCookieAccessDelegate::SetExpectationForCookieDomain(
    const std::string& cookie_domain,
    CookieAccessSemantics access_semantics) {
  expectations_[GetKeyForDomainValue(cookie_domain)] = access_semantics;
}

std::string TestCookieAccessDelegate::GetKeyForDomainValue(
    const std::string& domain) const {
  DCHECK(!domain.empty());
  return domain[0] == '.' ? domain.substr(1) : domain;
}

}  // namespace net
