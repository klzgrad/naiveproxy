// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PROXY_RESOLUTION_PROXY_BYPASS_RULES_H_
#define NET_PROXY_RESOLUTION_PROXY_BYPASS_RULES_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "url/gurl.h"

namespace net {

// ProxyBypassRules describes the set of URLs that should bypass the proxy
// settings, as a list of rules. A URL is said to match the bypass rules
// if it matches any one of these rules.
class NET_EXPORT ProxyBypassRules {
 public:
  // Interface for an individual proxy bypass rule.
  class NET_EXPORT Rule {
   public:
    Rule();
    virtual ~Rule();

    // Returns true if |url| matches the rule.
    virtual bool Matches(const GURL& url) const = 0;

    // Returns a string representation of this rule. This is used both for
    // visualizing the rules, and also to test equality of a rules list.
    virtual std::string ToString() const = 0;

    // Creates a copy of this rule.
    virtual std::unique_ptr<Rule> Clone() const = 0;

    bool Equals(const Rule& rule) const;

   private:
    DISALLOW_COPY_AND_ASSIGN(Rule);
  };

  typedef std::vector<std::unique_ptr<Rule>> RuleList;

  // Note: This class supports copy constructor and assignment.
  ProxyBypassRules();
  ProxyBypassRules(const ProxyBypassRules& rhs);
  ~ProxyBypassRules();
  ProxyBypassRules& operator=(const ProxyBypassRules& rhs);

  // Returns the current list of rules. The rules list contains pointers
  // which are owned by this class, callers should NOT keep references
  // or delete them.
  const RuleList& rules() const { return rules_; }

  // Returns true if |url| matches any of the proxy bypass rules.
  bool Matches(const GURL& url) const;

  // Returns true if |*this| is equal to |other|; in other words, whether they
  // describe the same set of rules.
  bool Equals(const ProxyBypassRules& other) const;

  // Initializes the list of rules by parsing the string |raw|. |raw| is a
  // comma separated list of rules. See AddRuleFromString() to see the list
  // of supported formats.
  void ParseFromString(const std::string& raw);

  // This is a variant of ParseFromString, which interprets hostname patterns
  // as suffix tests rather than hostname tests (so "google.com" would actually
  // match "*google.com"). This is only currently used for the linux no_proxy
  // environment variable. It is less flexible, since with the suffix matching
  // format you can't match an individual host.
  // NOTE: Use ParseFromString() unless you truly need this behavior.
  void ParseFromStringUsingSuffixMatching(const std::string& raw);

  // Adds a rule that matches a URL when all of the following are true:
  //  (a) The URL's scheme matches |optional_scheme|, if
  //      |!optional_scheme.empty()|
  //  (b) The URL's hostname matches |hostname_pattern|.
  //  (c) The URL's (effective) port number matches |optional_port| if
  //      |optional_port != -1|
  // Returns true if the rule was successfully added.
  bool AddRuleForHostname(const std::string& optional_scheme,
                          const std::string& hostname_pattern,
                          int optional_port);

  // Adds a rule that bypasses all "local" hostnames.
  // This matches IE's interpretation of the
  // "Bypass proxy server for local addresses" settings checkbox. Fully
  // qualified domain names or IP addresses are considered non-local,
  // regardless of what they map to (except for the loopback addresses).
  void AddRuleToBypassLocal();

  // Adds a rule given by the string |raw|. The format of |raw| can be any of
  // the following:
  //
  // (1) [ URL_SCHEME "://" ] HOSTNAME_PATTERN [ ":" <port> ]
  //
  //   Match all hostnames that match the pattern HOSTNAME_PATTERN.
  //
  //   Examples:
  //     "foobar.com", "*foobar.com", "*.foobar.com", "*foobar.com:99",
  //     "https://x.*.y.com:99"
  //
  // (2) "." HOSTNAME_SUFFIX_PATTERN [ ":" PORT ]
  //
  //   Match a particular domain suffix.
  //
  //   Examples:
  //     ".google.com", ".com", "http://.google.com"
  //
  // (3) [ SCHEME "://" ] IP_LITERAL [ ":" PORT ]
  //
  //   Match URLs which are IP address literals.
  //
  //   Conceptually this is the similar to (1), but with special cases
  //   to handle IP literal canonicalization. For example matching
  //   on "[0:0:0::1]" would be the same as matching on "[::1]" since
  //   the IPv6 canonicalization is done internally.
  //
  //   Examples:
  //     "127.0.1", "[0:0::1]", "[::1]", "http://[::1]:99"
  //
  // (4)  IP_LITERAL "/" PREFIX_LENGTH_IN_BITS
  //
  //   Match any URL that is to an IP literal that falls between the
  //   given range. IP range is specified using CIDR notation.
  //
  //   Examples:
  //     "192.168.1.1/16", "fefe:13::abc/33".
  //
  // (5)  "<local>"
  //
  //   Match local addresses. The meaning of "<local>" is whether the
  //   host matches one of: "127.0.0.1", "::1", "localhost".
  //
  // See the unit-tests for more examples.
  //
  // Returns true if the rule was successfully added.
  bool AddRuleFromString(const std::string& raw);

  // This is a variant of AddFromString, which interprets hostname patterns as
  // suffix tests rather than hostname tests (so "google.com" would actually
  // match "*google.com"). This is used for KDE which interprets every rule as
  // a suffix test. It is less flexible, since with the suffix matching format
  // you can't match an individual host.
  //
  // Returns true if the rule was successfully added.
  //
  // NOTE: Use AddRuleFromString() unless you truly need this behavior.
  bool AddRuleFromStringUsingSuffixMatching(const std::string& raw);

  // Converts the rules to string representation. Inverse operation to
  // ParseFromString().
  std::string ToString() const;

  // Removes all the rules.
  void Clear();

  // Sets |*this| to |other|.
  void AssignFrom(const ProxyBypassRules& other);

 private:
  // The following are variants of ParseFromString() and AddRuleFromString(),
  // which additionally prefix hostname patterns with a wildcard if
  // |use_hostname_suffix_matching| was true.
  void ParseFromStringInternal(const std::string& raw,
                               bool use_hostname_suffix_matching);
  bool AddRuleFromStringInternal(const std::string& raw,
                                 bool use_hostname_suffix_matching);
  bool AddRuleFromStringInternalWithLogging(const std::string& raw,
                                            bool use_hostname_suffix_matching);

  RuleList rules_;
};

}  // namespace net

#endif  // NET_PROXY_RESOLUTION_PROXY_BYPASS_RULES_H_
