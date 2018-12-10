// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_IP_PATTERN_H_
#define NET_BASE_IP_PATTERN_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/strings/string_piece.h"
#include "net/base/net_export.h"

namespace net {

class IPAddress;

// IPPatterns are used to match IP address resolutions for possible augmentation
// by a MappedIPResolver, which uses IPMappingRules.
class NET_EXPORT IPPattern {
 public:
  IPPattern();
  ~IPPattern();

  // Parse a textual pattern  (IP_PATTERN as defined in ip_mapping_rules.h) into
  // |this| and return true if the parsing was successful.
  bool ParsePattern(const std::string& ip_pattern);
  // Test to see if the current pattern in |this| matches the given |address|
  // and return true if it matches.
  bool Match(const IPAddress& address) const;

  bool is_ipv4() const { return is_ipv4_; }

 private:
  class ComponentPattern;
  using Strings = std::vector<std::string>;
  using ComponentPatternList = std::vector<std::unique_ptr<ComponentPattern>>;

  // IPv6 addresses have 8 components, while IPv4 addresses have 4 components.
  // ComponentPattern is used to define patterns to match individual components.
  bool ParseComponentPattern(const base::StringPiece& text,
                             ComponentPattern* pattern) const;
  // Convert IP component to an int.  Assume hex vs decimal for IPV6 vs V4.
  bool ValueTextToInt(const base::StringPiece& input, uint32_t* output) const;

  bool is_ipv4_;
  // The |ip_mask_| indicates, for each component, if this pattern requires an
  // exact match (OCTET in IPv4, or 4 hex digits in IPv6).
  // For each true element there is an entry in |component_values_|, and false
  // means that an entry from our list of ComponentPattern instances must be
  // applied.
  std::vector<bool> ip_mask_;
  // The vector of fixed values that are requried.
  // Other values may be restricted by the component_patterns_;
  // The class invariant is:
  // ip_mask_.size() == component_patterns_.size()
  //                    + size(our ComponentPattern list)
  std::vector<uint32_t> component_values_;
  // If only one component position was specified using a range, then this
  // list will only have 1 element (i.e., we only have patterns for each element
  // of ip_mask_ that is false.)
  // We own these elements, and need to destroy them all when we are destroyed.
  ComponentPatternList component_patterns_;

  DISALLOW_COPY_AND_ASSIGN(IPPattern);
};

}  // namespace net

#endif  // NET_BASE_IP_PATTERN_H_
