// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_util.h"

#include <errno.h>
#include <limits.h>

#include <cstring>

#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "net/base/address_list.h"
#include "net/base/url_util.h"
#include "net/dns/dns_protocol.h"
#include "url/url_canon.h"

namespace {

// RFC 1035, section 2.3.4: labels 63 octets or less.
// Section 3.1: Each label is represented as a one octet length field followed
// by that number of octets.
const int kMaxLabelLength = 63;

}  // namespace

#if defined(OS_POSIX)
#include <netinet/in.h>
#if !defined(OS_NACL)
#include <net/if.h>
#if !defined(OS_ANDROID)
#include <ifaddrs.h>
#endif  // !defined(OS_ANDROID)
#endif  // !defined(OS_NACL)
#endif  // defined(OS_POSIX)

#if defined(OS_ANDROID)
#include "net/android/network_library.h"
#endif

namespace net {

// Based on DJB's public domain code.
bool DNSDomainFromDot(const base::StringPiece& dotted, std::string* out) {
  const char* buf = dotted.data();
  size_t n = dotted.size();
  char label[kMaxLabelLength];
  size_t labellen = 0; /* <= sizeof label */
  char name[dns_protocol::kMaxNameLength];
  size_t namelen = 0; /* <= sizeof name */
  char ch;

  for (;;) {
    if (!n)
      break;
    ch = *buf++;
    --n;
    if (ch == '.') {
      // Don't allow empty labels per http://crbug.com/456391.
      if (!labellen)
        return false;
      if (namelen + labellen + 1 > sizeof name)
        return false;
      name[namelen++] = static_cast<char>(labellen);
      memcpy(name + namelen, label, labellen);
      namelen += labellen;
      labellen = 0;
      continue;
    }
    if (labellen >= sizeof label)
      return false;
    if (!IsValidHostLabelCharacter(ch, labellen == 0)) {
      return false;
    }
    label[labellen++] = ch;
  }

  // Allow empty label at end of name to disable suffix search.
  if (labellen) {
    if (namelen + labellen + 1 > sizeof name)
      return false;
    name[namelen++] = static_cast<char>(labellen);
    memcpy(name + namelen, label, labellen);
    namelen += labellen;
    labellen = 0;
  }

  if (namelen + 1 > sizeof name)
    return false;
  if (namelen == 0) // Empty names e.g. "", "." are not valid.
    return false;
  name[namelen++] = 0;  // This is the root label (of length 0).

  *out = std::string(name, namelen);
  return true;
}

bool IsValidDNSDomain(const base::StringPiece& dotted) {
  std::string dns_formatted;
  return DNSDomainFromDot(dotted, &dns_formatted);
}

bool IsValidHostLabelCharacter(char c, bool is_first_char) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || (!is_first_char && c == '-') || c == '_';
}

std::string DNSDomainToString(const base::StringPiece& domain) {
  std::string ret;

  for (unsigned i = 0; i < domain.size() && domain[i]; i += domain[i] + 1) {
#if CHAR_MIN < 0
    if (domain[i] < 0)
      return std::string();
#endif
    if (domain[i] > kMaxLabelLength)
      return std::string();

    if (i)
      ret += ".";

    if (static_cast<unsigned>(domain[i]) + i + 1 > domain.size())
      return std::string();

    domain.substr(i + 1, domain[i]).AppendToString(&ret);
  }
  return ret;
}

#if !defined(OS_NACL)
namespace {

bool GetTimeDeltaForConnectionTypeFromFieldTrial(
    const char* field_trial,
    NetworkChangeNotifier::ConnectionType type,
    base::TimeDelta* out) {
  std::string group = base::FieldTrialList::FindFullName(field_trial);
  if (group.empty())
    return false;
  std::vector<base::StringPiece> group_parts = base::SplitStringPiece(
      group, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (type < 0)
    return false;
  size_t type_size = static_cast<size_t>(type);
  if (type_size >= group_parts.size())
    return false;
  int64_t ms;
  if (!base::StringToInt64(group_parts[type_size], &ms))
    return false;
  *out = base::TimeDelta::FromMilliseconds(ms);
  return true;
}

}  // namespace

base::TimeDelta GetTimeDeltaForConnectionTypeFromFieldTrialOrDefault(
    const char* field_trial,
    base::TimeDelta default_delta,
    NetworkChangeNotifier::ConnectionType type) {
  base::TimeDelta out;
  if (!GetTimeDeltaForConnectionTypeFromFieldTrial(field_trial, type, &out))
    out = default_delta;
  return out;
}
#endif  // !defined(OS_NACL)

AddressListDeltaType FindAddressListDeltaType(const AddressList& a,
                                              const AddressList& b) {
  bool pairwise_mismatch = false;
  bool any_match = false;
  bool any_missing = false;
  bool same_size = a.size() == b.size();

  for (size_t i = 0; i < a.size(); ++i) {
    bool this_match = false;
    for (size_t j = 0; j < b.size(); ++j) {
      if (a[i] == b[j]) {
        any_match = true;
        this_match = true;
      } else if (i == j) {
        pairwise_mismatch = true;
      }
    }
    if (!this_match)
      any_missing = true;
  }

  if (same_size && !pairwise_mismatch)
    return DELTA_IDENTICAL;
  else if (same_size && !any_missing)
    return DELTA_REORDERED;
  else if (any_match)
    return DELTA_OVERLAP;
  else
    return DELTA_DISJOINT;
}

}  // namespace net
