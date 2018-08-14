// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "url/origin.h"

#include <stdint.h>
#include <string.h>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "url/gurl.h"
#include "url/url_canon.h"
#include "url/url_canon_stdstring.h"
#include "url/url_constants.h"
#include "url/url_util.h"

namespace url {

Origin::Origin() : unique_(true) {}

Origin Origin::Create(const GURL& url) {
  if (!url.is_valid() || (!url.IsStandard() && !url.SchemeIsBlob()))
    return Origin();

  SchemeHostPort tuple;

  if (url.SchemeIsFileSystem()) {
    tuple = SchemeHostPort(*url.inner_url());
  } else if (url.SchemeIsBlob()) {
    // If we're dealing with a 'blob:' URL, https://url.spec.whatwg.org/#origin
    // defines the origin as the origin of the URL which results from parsing
    // the "path", which boils down to everything after the scheme. GURL's
    // 'GetContent()' gives us exactly that.
    tuple = SchemeHostPort(GURL(url.GetContent()));
  } else {
    tuple = SchemeHostPort(url);
  }

  if (tuple.IsInvalid())
    return Origin();

  return Origin(std::move(tuple));
}

Origin::Origin(SchemeHostPort tuple)
    : tuple_(std::move(tuple)), unique_(false) {
  DCHECK(!tuple_.IsInvalid());
}

Origin::Origin(const Origin&) = default;
Origin& Origin::operator=(const Origin&) = default;
Origin::Origin(Origin&&) = default;
Origin& Origin::operator=(Origin&&) = default;

Origin::~Origin() = default;

// static
Origin Origin::UnsafelyCreateOriginWithoutNormalization(
    base::StringPiece scheme,
    base::StringPiece host,
    uint16_t port) {
  SchemeHostPort tuple(scheme.as_string(), host.as_string(), port,
                       SchemeHostPort::CHECK_CANONICALIZATION);
  if (tuple.IsInvalid())
    return Origin();
  return Origin(std::move(tuple));
}

Origin Origin::CreateFromNormalizedTuple(std::string scheme,
                                         std::string host,
                                         uint16_t port) {
  SchemeHostPort tuple(std::move(scheme), std::move(host), port,
                       SchemeHostPort::ALREADY_CANONICALIZED);
  if (tuple.IsInvalid())
    return Origin();
  return Origin(std::move(tuple));
}

std::string Origin::Serialize() const {
  if (unique())
    return "null";

  if (scheme() == kFileScheme)
    return "file://";

  return tuple_.Serialize();
}

GURL Origin::GetURL() const {
  if (unique())
    return GURL();

  if (scheme() == kFileScheme)
    return GURL("file:///");

  GURL tuple_url(tuple_.GetURL());

  return tuple_url;
}

bool Origin::IsSameOriginWith(const Origin& other) const {
  if (unique_ || other.unique_)
    return false;

  return tuple_.Equals(other.tuple_);
}

bool Origin::DomainIs(base::StringPiece canonical_domain) const {
  return !unique_ && url::DomainIs(tuple_.host(), canonical_domain);
}

bool Origin::operator<(const Origin& other) const {
  return tuple_ < other.tuple_;
}

std::ostream& operator<<(std::ostream& out, const url::Origin& origin) {
  return out << origin.Serialize();
}

bool IsSameOriginWith(const GURL& a, const GURL& b) {
  return Origin::Create(a).IsSameOriginWith(Origin::Create(b));
}

}  // namespace url
