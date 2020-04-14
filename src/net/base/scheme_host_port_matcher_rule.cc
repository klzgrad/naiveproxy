// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/scheme_host_port_matcher_rule.h"

#include "base/strings/pattern.h"
#include "base/strings/stringprintf.h"
#include "url/url_util.h"

namespace net {

namespace {

std::string AddBracketsIfIPv6(const IPAddress& ip_address) {
  std::string ip_host = ip_address.ToString();
  if (ip_address.IsIPv6())
    return base::StringPrintf("[%s]", ip_host.c_str());
  return ip_host;
}

}  // namespace

bool SchemeHostPortMatcherRule::IsHostnamePatternRule() const {
  return false;
}

SchemeHostPortMatcherHostnamePatternRule::
    SchemeHostPortMatcherHostnamePatternRule(
        const std::string& optional_scheme,
        const std::string& hostname_pattern,
        int optional_port)
    : optional_scheme_(base::ToLowerASCII(optional_scheme)),
      hostname_pattern_(base::ToLowerASCII(hostname_pattern)),
      optional_port_(optional_port) {
  // |hostname_pattern| shouldn't be an IP address.
  DCHECK(!url::HostIsIPAddress(hostname_pattern));
}

SchemeHostPortMatcherResult SchemeHostPortMatcherHostnamePatternRule::Evaluate(
    const GURL& url) const {
  if (optional_port_ != -1 && url.EffectiveIntPort() != optional_port_) {
    // Didn't match port expectation.
    return SchemeHostPortMatcherResult::kNoMatch;
  }

  if (!optional_scheme_.empty() && url.scheme() != optional_scheme_) {
    // Didn't match scheme expectation.
    return SchemeHostPortMatcherResult::kNoMatch;
  }

  // Note it is necessary to lower-case the host, since GURL uses capital
  // letters for percent-escaped characters.
  return base::MatchPattern(url.host(), hostname_pattern_)
             ? SchemeHostPortMatcherResult::kInclude
             : SchemeHostPortMatcherResult::kNoMatch;
}

std::string SchemeHostPortMatcherHostnamePatternRule::ToString() const {
  std::string str;
  if (!optional_scheme_.empty())
    base::StringAppendF(&str, "%s://", optional_scheme_.c_str());
  str += hostname_pattern_;
  if (optional_port_ != -1)
    base::StringAppendF(&str, ":%d", optional_port_);
  return str;
}

bool SchemeHostPortMatcherHostnamePatternRule::IsHostnamePatternRule() const {
  return true;
}

std::unique_ptr<SchemeHostPortMatcherHostnamePatternRule>
SchemeHostPortMatcherHostnamePatternRule::GenerateSuffixMatchingRule() const {
  if (!base::StartsWith(hostname_pattern_, "*", base::CompareCase::SENSITIVE)) {
    return std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
        optional_scheme_, "*" + hostname_pattern_, optional_port_);
  }
  // return a new SchemeHostPortMatcherHostNamePatternRule with the same data.
  return std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
      optional_scheme_, hostname_pattern_, optional_port_);
}

SchemeHostPortMatcherIPHostRule::SchemeHostPortMatcherIPHostRule(
    const std::string& optional_scheme,
    const IPEndPoint& ip_end_point)
    : optional_scheme_(base::ToLowerASCII(optional_scheme)),
      ip_host_(AddBracketsIfIPv6(ip_end_point.address())),
      optional_port_(ip_end_point.port()) {}

SchemeHostPortMatcherResult SchemeHostPortMatcherIPHostRule::Evaluate(
    const GURL& url) const {
  if (optional_port_ != 0 && url.EffectiveIntPort() != optional_port_) {
    // Didn't match port expectation.
    return SchemeHostPortMatcherResult::kNoMatch;
  }

  if (!optional_scheme_.empty() && url.scheme() != optional_scheme_) {
    // Didn't match scheme expectation.
    return SchemeHostPortMatcherResult::kNoMatch;
  }

  // Note it is necessary to lower-case the host, since GURL uses capital
  // letters for percent-escaped characters.
  return base::MatchPattern(url.host(), ip_host_)
             ? SchemeHostPortMatcherResult::kInclude
             : SchemeHostPortMatcherResult::kNoMatch;
}

std::string SchemeHostPortMatcherIPHostRule::ToString() const {
  std::string str;
  if (!optional_scheme_.empty())
    base::StringAppendF(&str, "%s://", optional_scheme_.c_str());
  str += ip_host_;
  if (optional_port_ != 0)
    base::StringAppendF(&str, ":%d", optional_port_);
  return str;
}

SchemeHostPortMatcherIPBlockRule::SchemeHostPortMatcherIPBlockRule(
    const std::string& description,
    const std::string& optional_scheme,
    const IPAddress& ip_prefix,
    size_t prefix_length_in_bits)
    : description_(description),
      optional_scheme_(optional_scheme),
      ip_prefix_(ip_prefix),
      prefix_length_in_bits_(prefix_length_in_bits) {}

SchemeHostPortMatcherResult SchemeHostPortMatcherIPBlockRule::Evaluate(
    const GURL& url) const {
  if (!url.HostIsIPAddress())
    return SchemeHostPortMatcherResult::kNoMatch;

  if (!optional_scheme_.empty() && url.scheme() != optional_scheme_) {
    // Didn't match scheme expectation.
    return SchemeHostPortMatcherResult::kNoMatch;
  }

  // Parse the input IP literal to a number.
  IPAddress ip_address;
  if (!ip_address.AssignFromIPLiteral(url.HostNoBracketsPiece()))
    return SchemeHostPortMatcherResult::kNoMatch;

  // Test if it has the expected prefix.
  return IPAddressMatchesPrefix(ip_address, ip_prefix_, prefix_length_in_bits_)
             ? SchemeHostPortMatcherResult::kInclude
             : SchemeHostPortMatcherResult::kNoMatch;
}

std::string SchemeHostPortMatcherIPBlockRule::ToString() const {
  return description_;
}

}  // namespace net
