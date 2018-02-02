// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_TRANSPORT_SECURITY_STATE_GENERATOR_TRANSPORT_SECURITY_STATE_ENTRY_H_
#define NET_TOOLS_TRANSPORT_SECURITY_STATE_GENERATOR_TRANSPORT_SECURITY_STATE_ENTRY_H_

#include <memory>
#include <string>
#include <vector>

namespace net {

namespace transport_security_state {

// TransportSecurityStateEntry represents a preloaded entry.
struct TransportSecurityStateEntry {
  TransportSecurityStateEntry();
  ~TransportSecurityStateEntry();

  std::string hostname;

  bool include_subdomains = false;
  bool force_https = false;

  bool hpkp_include_subdomains = false;
  std::string pinset;

  bool expect_ct = false;
  std::string expect_ct_report_uri;

  bool expect_staple = false;
  bool expect_staple_include_subdomains = false;
  std::string expect_staple_report_uri;
};

using TransportSecurityStateEntries =
    std::vector<std::unique_ptr<TransportSecurityStateEntry>>;

// ReversedEntry points to a TransportSecurityStateEntry and contains the
// reversed hostname for that entry. This is used to construct the trie.
struct ReversedEntry {
  ReversedEntry(std::vector<uint8_t> reversed_name,
                const TransportSecurityStateEntry* entry);
  ~ReversedEntry();

  std::vector<uint8_t> reversed_name;
  const TransportSecurityStateEntry* entry;
};

using ReversedEntries = std::vector<std::unique_ptr<ReversedEntry>>;

}  // namespace transport_security_state

}  // namespace net

#endif  // NET_TOOLS_TRANSPORT_SECURITY_STATE_GENERATOR_TRANSPORT_SECURITY_STATE_ENTRY_H_
