// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/site_for_cookies.h"

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace net {

namespace {

std::string RegistrableDomainOrHost(const std::string& host) {
  std::string domain = registry_controlled_domains::GetDomainAndRegistry(
      host, registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return domain.empty() ? host : domain;
}

}  // namespace

SiteForCookies::SiteForCookies() = default;

SiteForCookies::SiteForCookies(const SiteForCookies& other) = default;
SiteForCookies::SiteForCookies(SiteForCookies&& other) = default;

SiteForCookies::~SiteForCookies() = default;

SiteForCookies& SiteForCookies::operator=(const SiteForCookies& other) =
    default;
SiteForCookies& SiteForCookies::operator=(SiteForCookies&& site_for_cookies) =
    default;

// static
bool SiteForCookies::FromWire(const std::string& scheme,
                              const std::string& registrable_domain,
                              SiteForCookies* out) {
  // Make sure scheme meets precondition of methods like
  // GURL::SchemeIsCryptographic.
  if (!base::IsStringASCII(scheme) || base::ToLowerASCII(scheme) != scheme)
    return false;

  // registrable_domain_ should also be canonicalized.
  SiteForCookies candidate(scheme, registrable_domain);
  if (registrable_domain != candidate.registrable_domain_)
    return false;

  *out = std::move(candidate);
  return true;
}

// static
SiteForCookies SiteForCookies::FromOrigin(const url::Origin& origin) {
  // Opaque origins are not first-party to anything.
  if (origin.opaque())
    return SiteForCookies();

  return SiteForCookies(origin.scheme(), origin.host());
}

// static
SiteForCookies SiteForCookies::FromUrl(const GURL& url) {
  return SiteForCookies::FromOrigin(url::Origin::Create(url));
}

std::string SiteForCookies::ToDebugString() const {
  return base::StrCat({"SiteForCookies: {scheme=", scheme_,
                       "; registrable_domain=", registrable_domain_, "}"});
}

bool SiteForCookies::IsFirstParty(const GURL& url) const {
  if (scheme_.empty() || !url.is_valid())
    return false;

  std::string other_registrable_domain = RegistrableDomainOrHost(url.host());

  if (registrable_domain_.empty())
    return other_registrable_domain.empty() && (scheme_ == url.scheme());

  return registrable_domain_ == other_registrable_domain;
}

bool SiteForCookies::IsEquivalent(const SiteForCookies& other) const {
  if (scheme_.empty())
    return other.scheme_.empty();

  if (registrable_domain_.empty())
    return other.registrable_domain_.empty() && (scheme_ == other.scheme_);

  return registrable_domain_ == other.registrable_domain_;
}

GURL SiteForCookies::RepresentativeUrl() const {
  if (scheme_.empty())
    return GURL();
  GURL result(base::StrCat({scheme_, "://", registrable_domain_, "/"}));
  DCHECK(result.is_valid());
  return result;
}

SiteForCookies::SiteForCookies(const std::string& scheme,
                               const std::string& host)
    : scheme_(scheme), registrable_domain_(RegistrableDomainOrHost(host)) {}

}  // namespace net
