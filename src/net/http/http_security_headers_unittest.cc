// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "base/base64.h"
#include "base/stl_util.h"
#include "base/strings/string_piece.h"
#include "crypto/sha2.h"
#include "net/base/host_port_pair.h"
#include "net/base/test_completion_callback.h"
#include "net/http/http_security_headers.h"
#include "net/http/http_util.h"
#include "net/http/transport_security_state.h"
#include "net/ssl/ssl_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

namespace test_default {
#include "net/http/transport_security_state_static_unittest_default.h"
}

HashValue GetTestHashValue(uint8_t label, HashValueTag tag) {
  HashValue hash_value(tag);
  memset(hash_value.data(), label, hash_value.size());
  return hash_value;
}

std::string GetTestPinImpl(uint8_t label, HashValueTag tag, bool quoted) {
  HashValue hash_value = GetTestHashValue(label, tag);
  std::string base64;
  base::Base64Encode(base::StringPiece(
      reinterpret_cast<char*>(hash_value.data()), hash_value.size()), &base64);

  std::string ret;
  switch (hash_value.tag()) {
    case HASH_VALUE_SHA256:
      ret = "pin-sha256=";
      break;
    default:
      NOTREACHED() << "Unknown HashValueTag " << hash_value.tag();
      return std::string("ERROR");
  }
  if (quoted)
    ret += '\"';
  ret += base64;
  if (quoted)
    ret += '\"';
  return ret;
}

std::string GetTestPin(uint8_t label, HashValueTag tag) {
  return GetTestPinImpl(label, tag, true);
}

std::string GetTestPinUnquoted(uint8_t label, HashValueTag tag) {
  return GetTestPinImpl(label, tag, false);
}

}  // anonymous namespace

// Parses the given header |value| as both a Public-Key-Pins-Report-Only
// and Public-Key-Pins header. Returns true if the value parses
// successfully for both header types, and if the parsed hashes and
// report_uri match for both header types.
bool ParseAsHPKPHeader(const std::string& value,
                       const HashValueVector& chain_hashes,
                       base::TimeDelta* max_age,
                       bool* include_subdomains,
                       HashValueVector* hashes,
                       GURL* report_uri) {
  GURL report_only_uri;
  bool report_only_include_subdomains;
  HashValueVector report_only_hashes;
  if (!ParseHPKPReportOnlyHeader(value, &report_only_include_subdomains,
                                 &report_only_hashes, &report_only_uri)) {
    return false;
  }

  bool result = ParseHPKPHeader(value, chain_hashes, max_age,
                                include_subdomains, hashes, report_uri);
  if (!result || report_only_include_subdomains != *include_subdomains ||
      report_only_uri != *report_uri || report_only_hashes != *hashes) {
    return false;
  }

  return true;
}

class HttpSecurityHeadersTest : public testing::Test {
 public:
  ~HttpSecurityHeadersTest() override {
    SetTransportSecurityStateSourceForTesting(nullptr);
  }
};


TEST_F(HttpSecurityHeadersTest, BogusHeaders) {
  base::TimeDelta max_age;
  bool include_subdomains = false;

  EXPECT_FALSE(
      ParseHSTSHeader(std::string(), &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("    ", &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("abc", &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("  abc", &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("  abc   ", &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age", &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("  max-age", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("  max-age  ", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=", &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("   max-age=", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("   max-age  =", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("   max-age=   ", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("   max-age  =     ", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("   max-age  =     xy", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("   max-age  =     3488a923", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488a923  ", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-ag=3488923", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-aged=3488923", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age==3488923", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("amax-age=3488923", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=-3488923", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(
      ParseHSTSHeader("max-age=+3488923", &max_age, &include_subdomains));
  EXPECT_FALSE(
      ParseHSTSHeader("max-age=13####", &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=9223372036854775807#####", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=18446744073709551615####", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=999999999999999999999999$.&#!",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923     e", &max_age,
                               &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923     includesubdomain",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923includesubdomains",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923=includesubdomains",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923 includesubdomainx",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923 includesubdomain=",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923 includesubdomain=true",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923 includesubdomainsx",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=3488923 includesubdomains x",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=34889.23 includesubdomains",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age=34889 includesubdomains",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader(";;;; ;;;",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader(";;;; includeSubDomains;;;",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("   includeSubDomains;  ",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader(";",
                               &max_age, &include_subdomains));
  EXPECT_FALSE(ParseHSTSHeader("max-age; ;",
                               &max_age, &include_subdomains));

  // Check the out args were not updated by checking the default
  // values for its predictable fields.
  EXPECT_EQ(0, max_age.InSeconds());
  EXPECT_FALSE(include_subdomains);
}

static void TestBogusPinsHeaders(HashValueTag tag) {
  base::TimeDelta max_age;
  bool include_subdomains;
  HashValueVector hashes;
  HashValueVector chain_hashes;
  GURL report_uri;

  // Set some fake "chain" hashes
  chain_hashes.push_back(GetTestHashValue(1, tag));
  chain_hashes.push_back(GetTestHashValue(2, tag));
  chain_hashes.push_back(GetTestHashValue(3, tag));

  // The good pin must be in the chain, the backup pin must not be
  std::string good_pin = GetTestPin(2, tag);
  std::string good_pin_unquoted = GetTestPinUnquoted(2, tag);
  std::string backup_pin = GetTestPin(4, tag);

  EXPECT_FALSE(ParseAsHPKPHeader(std::string(), chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("    ", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("abc", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("  abc", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("  abc   ", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("  max-age", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("  max-age  ", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("   max-age=", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("   max-age  =", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("   max-age=   ", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("   max-age  =     ", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("   max-age  =     xy", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("   max-age  =     3488a923", chain_hashes,
                                 &max_age, &include_subdomains, &hashes,
                                 &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=3488a923  ", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-ag=3488923pins=" + good_pin + "," + backup_pin, chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-age=3488923;pins=" + good_pin + "," + backup_pin +
          "report-uri=\"http://foo.com\"",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-aged=3488923" + backup_pin, chain_hashes,
                                 &max_age, &include_subdomains, &hashes,
                                 &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-aged=3488923; " + backup_pin,
                                 chain_hashes, &max_age, &include_subdomains,
                                 &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-aged=3488923; " + backup_pin + ";" + backup_pin, chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-aged=3488923; " + good_pin + ";" + good_pin, chain_hashes, &max_age,
      &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-aged=3488923; " + good_pin, chain_hashes,
                                 &max_age, &include_subdomains, &hashes,
                                 &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age==3488923", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("amax-age=3488923", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=-3488923", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=3488923;", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=3488923     e", chain_hashes,
                                 &max_age, &include_subdomains, &hashes,
                                 &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=3488923     includesubdomain",
                                 chain_hashes, &max_age, &include_subdomains,
                                 &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-age=3488923     report-uri=\"http://foo.com\"", chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=34889.23", chain_hashes, &max_age,
                                 &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-age=243; " + good_pin_unquoted + ";" + backup_pin, chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-age=243; " + good_pin + ";" + backup_pin + ";report-uri=;",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=243; " + good_pin + ";" + backup_pin +
                                     ";report-uri=http://foo.com;",
                                 chain_hashes, &max_age, &include_subdomains,
                                 &hashes, &report_uri));
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-age=243; " + good_pin + ";" + backup_pin + ";report-uri=''",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));

  // Test that the parser rejects misquoted strings.
  EXPECT_FALSE(ParseAsHPKPHeader(
      "max-age=999; " + backup_pin + "; " + good_pin +
          "; report-uri=\"http://foo;bar\'",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));

  // Test that the parser rejects invalid report-uris.
  EXPECT_FALSE(ParseAsHPKPHeader("max-age=999; " + backup_pin + "; " +
                                     good_pin + "; report-uri=\"foo;bar\'",
                                 chain_hashes, &max_age, &include_subdomains,
                                 &hashes, &report_uri));

  // Check the out args were not updated by checking the default
  // values for its predictable fields.
  EXPECT_EQ(0, max_age.InSeconds());
  EXPECT_EQ(hashes.size(), (size_t)0);
}

TEST_F(HttpSecurityHeadersTest, ValidSTSHeaders) {
  base::TimeDelta max_age;
  base::TimeDelta expect_max_age;
  bool include_subdomains = false;

  EXPECT_TRUE(ParseHSTSHeader("max-age=243", &max_age,
                              &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(243);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader("max-age=3488923;", &max_age,
                              &include_subdomains));

  EXPECT_TRUE(ParseHSTSHeader("  Max-agE    = 567", &max_age,
                              &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(567);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader("  mAx-aGe    = 890      ", &max_age,
                              &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(890);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader("max-age=123;incLudesUbdOmains", &max_age,
                              &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader("incLudesUbdOmains; max-age=123", &max_age,
                              &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader("   incLudesUbdOmains; max-age=123",
                              &max_age, &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "   incLudesUbdOmains; max-age=123; pumpkin=kitten", &max_age,
                                   &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "   pumpkin=894; incLudesUbdOmains; max-age=123  ", &max_age,
                                   &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "   pumpkin; incLudesUbdOmains; max-age=123  ", &max_age,
                                   &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "   pumpkin; incLudesUbdOmains; max-age=\"123\"  ", &max_age,
                                   &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "animal=\"squirrel; distinguished\"; incLudesUbdOmains; max-age=123",
                                   &max_age, &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader("max-age=394082;  incLudesUbdOmains",
                              &max_age, &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(394082);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "max-age=39408299  ;incLudesUbdOmains", &max_age,
      &include_subdomains));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHSTSAgeSecs, 39408299u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "max-age=394082038  ; incLudesUbdOmains", &max_age,
      &include_subdomains));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHSTSAgeSecs, 394082038u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "max-age=394082038  ; incLudesUbdOmains;", &max_age,
      &include_subdomains));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHSTSAgeSecs, 394082038u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      ";; max-age=394082038  ; incLudesUbdOmains; ;", &max_age,
      &include_subdomains));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHSTSAgeSecs, 394082038u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      ";; max-age=394082038  ;", &max_age,
      &include_subdomains));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHSTSAgeSecs, 394082038u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      ";;    ; ; max-age=394082038;;; includeSubdomains     ;;  ;", &max_age,
      &include_subdomains));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHSTSAgeSecs, 394082038u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "incLudesUbdOmains   ; max-age=394082038 ;;", &max_age,
      &include_subdomains));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHSTSAgeSecs, 394082038u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "  max-age=0  ;  incLudesUbdOmains   ", &max_age,
      &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(0);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseHSTSHeader(
      "  max-age=999999999999999999999999999999999999999999999  ;"
      "  incLudesUbdOmains   ", &max_age, &include_subdomains));
  expect_max_age = base::TimeDelta::FromSeconds(
      kMaxHSTSAgeSecs);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);
}

static void TestValidPKPHeaders(HashValueTag tag) {
  base::TimeDelta max_age;
  base::TimeDelta expect_max_age;
  bool include_subdomains;
  HashValueVector hashes;
  HashValueVector chain_hashes;
  GURL expect_report_uri;
  GURL report_uri;

  // Set some fake "chain" hashes into chain_hashes
  chain_hashes.push_back(GetTestHashValue(1, tag));
  chain_hashes.push_back(GetTestHashValue(2, tag));
  chain_hashes.push_back(GetTestHashValue(3, tag));

  // The good pin must be in the chain, the backup pin must not be
  std::string good_pin = GetTestPin(2, tag);
  std::string good_pin2 = GetTestPin(3, tag);
  std::string backup_pin = GetTestPin(4, tag);

  EXPECT_TRUE(ParseAsHPKPHeader("max-age=243; " + good_pin + ";" + backup_pin,
                                chain_hashes, &max_age, &include_subdomains,
                                &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(243);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);
  EXPECT_TRUE(report_uri.is_empty());

  EXPECT_TRUE(ParseAsHPKPHeader("max-age=243; " + good_pin + ";" + backup_pin +
                                    "; report-uri= \"http://example.test/foo\"",
                                chain_hashes, &max_age, &include_subdomains,
                                &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(243);
  expect_report_uri = GURL("http://example.test/foo");
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);
  EXPECT_EQ(expect_report_uri, report_uri);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "   " + good_pin + "; " + backup_pin +
          "  ; Max-agE    = 567; repOrT-URi = \"http://example.test/foo\"",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(567);
  expect_report_uri = GURL("http://example.test/foo");
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);
  EXPECT_EQ(expect_report_uri, report_uri);

  EXPECT_TRUE(ParseAsHPKPHeader("includeSubDOMAINS;" + good_pin + ";" +
                                    backup_pin + "  ; mAx-aGe    = 890      ",
                                chain_hashes, &max_age, &include_subdomains,
                                &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(890);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      good_pin + ";" + backup_pin + "; max-age=123;IGNORED;", chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(123);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "max-age=394082;" + backup_pin + ";" + good_pin + ";  ", chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(394082);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "max-age=39408299  ;" + backup_pin + ";" + good_pin + ";  ", chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHPKPAgeSecs, 39408299u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "max-age=39408038  ;    cybers=39408038  ;  includeSubdomains; " +
          good_pin + ";" + backup_pin + ";   ",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age =
      base::TimeDelta::FromSeconds(std::min(kMaxHPKPAgeSecs, 394082038u));
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "  max-age=0  ;  " + good_pin + ";" + backup_pin, chain_hashes, &max_age,
      &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(0);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "  max-age=0 ; includeSubdomains;  " + good_pin + ";" + backup_pin,
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(0);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_TRUE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "  max-age=999999999999999999999999999999999999999999999  ;  " +
          backup_pin + ";" + good_pin + ";   ",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(kMaxHPKPAgeSecs);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  EXPECT_TRUE(ParseAsHPKPHeader(
      "  max-age=999999999999999999999999999999999999999999999  ;  " +
          backup_pin + ";" + good_pin +
          ";   report-uri=\"http://example.test/foo\"",
      chain_hashes, &max_age, &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(kMaxHPKPAgeSecs);
  expect_report_uri = GURL("http://example.test/foo");
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);
  EXPECT_EQ(expect_report_uri, report_uri);

  // Test that parsing a different header resets the hashes.
  hashes.clear();
  EXPECT_TRUE(ParseAsHPKPHeader(
      "  max-age=999;  " + backup_pin + ";" + good_pin + ";   ", chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_EQ(2u, hashes.size());
  EXPECT_TRUE(ParseAsHPKPHeader(
      "  max-age=999;  " + backup_pin + ";" + good_pin2 + ";   ", chain_hashes,
      &max_age, &include_subdomains, &hashes, &report_uri));
  EXPECT_EQ(2u, hashes.size());

  // Test that the parser correctly parses an unencoded ';' inside a
  // quoted report-uri.
  EXPECT_TRUE(ParseAsHPKPHeader("max-age=999; " + backup_pin + "; " + good_pin +
                                    "; report-uri=\"http://foo.com/?;bar\"",
                                chain_hashes, &max_age, &include_subdomains,
                                &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(999);
  expect_report_uri = GURL("http://foo.com/?;bar");
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);
  EXPECT_EQ(expect_report_uri, report_uri);

  // Test that the parser correctly parses a report-uri with a >0x7f
  // character.
  std::string uri = "http://foo.com/";
  uri += char(0x7f);
  expect_report_uri = GURL(uri);
  EXPECT_TRUE(ParseAsHPKPHeader("max-age=999; " + backup_pin + "; " + good_pin +
                                    "; report-uri=\"" + uri + "\"",
                                chain_hashes, &max_age, &include_subdomains,
                                &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(999);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);
  EXPECT_EQ(expect_report_uri, report_uri);

  // Test that the parser allows quoted max-age values.
  EXPECT_TRUE(ParseAsHPKPHeader(
      "max-age='999'; " + backup_pin + "; " + good_pin, chain_hashes, &max_age,
      &include_subdomains, &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(999);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);

  // Test that the parser handles escaped values.
  expect_report_uri = GURL("http://foo.com'a");
  EXPECT_TRUE(ParseAsHPKPHeader("max-age=999; " + backup_pin + "; " + good_pin +
                                    "; report-uri='http://foo.com\\'\\a'",
                                chain_hashes, &max_age, &include_subdomains,
                                &hashes, &report_uri));
  expect_max_age = base::TimeDelta::FromSeconds(999);
  EXPECT_EQ(expect_max_age, max_age);
  EXPECT_FALSE(include_subdomains);
  EXPECT_EQ(expect_report_uri, report_uri);

  // Test that the parser does not require max-age for Report-Only
  // headers.
  expect_report_uri = GURL("http://foo.com");
  EXPECT_TRUE(ParseHPKPReportOnlyHeader(
      backup_pin + "; " + good_pin + "; report-uri='http://foo.com'",
      &include_subdomains, &hashes, &report_uri));
  EXPECT_EQ(expect_report_uri, report_uri);
}

TEST_F(HttpSecurityHeadersTest, BogusPinsHeadersSHA256) {
  TestBogusPinsHeaders(HASH_VALUE_SHA256);
}

TEST_F(HttpSecurityHeadersTest, ValidPKPHeadersSHA256) {
  TestValidPKPHeaders(HASH_VALUE_SHA256);
}

TEST_F(HttpSecurityHeadersTest, UpdateDynamicPKPOnly) {
  SetTransportSecurityStateSourceForTesting(&test_default::kHSTSSource);

  TransportSecurityState state;
  TransportSecurityState::STSState static_sts_state;
  TransportSecurityState::PKPState static_pkp_state;

  std::string domain = "no-rejected-pins-pkp.preloaded.test";
  state.enable_static_pins_ = true;
  EXPECT_TRUE(
      state.GetStaticDomainState(domain, &static_sts_state, &static_pkp_state));
  EXPECT_GT(static_pkp_state.spki_hashes.size(), 1UL);
  HashValueVector saved_hashes = static_pkp_state.spki_hashes;

  // Add a header, which should only update the dynamic state.
  HashValue good_hash = GetTestHashValue(1, HASH_VALUE_SHA256);
  HashValue backup_hash = GetTestHashValue(2, HASH_VALUE_SHA256);
  std::string good_pin = GetTestPin(1, HASH_VALUE_SHA256);
  std::string backup_pin = GetTestPin(2, HASH_VALUE_SHA256);
  GURL report_uri("http://report-uri.test/pkp");
  std::string header = "max-age = 10000; " + good_pin + "; " + backup_pin +
                       ";report-uri=\"" + report_uri.spec() + "\"";

  // Construct a fake SSLInfo that will pass AddHPKPHeader's checks.
  SSLInfo ssl_info;
  ssl_info.public_key_hashes.push_back(good_hash);
  ssl_info.public_key_hashes.push_back(saved_hashes[0]);
  EXPECT_TRUE(state.AddHPKPHeader(domain, header, ssl_info));

  // Expect the static state to remain unchanged.
  TransportSecurityState::STSState new_static_sts_state;
  TransportSecurityState::PKPState new_static_pkp_state;
  EXPECT_TRUE(state.GetStaticDomainState(domain, &new_static_sts_state,
                                         &new_static_pkp_state));
  EXPECT_EQ(saved_hashes, new_static_pkp_state.spki_hashes);

  // Expect the dynamic state to reflect the header.
  TransportSecurityState::PKPState dynamic_pkp_state;
  EXPECT_TRUE(state.GetDynamicPKPState(domain, &dynamic_pkp_state));
  EXPECT_EQ(2UL, dynamic_pkp_state.spki_hashes.size());
  EXPECT_EQ(report_uri, dynamic_pkp_state.report_uri);

  EXPECT_TRUE(base::ContainsValue(dynamic_pkp_state.spki_hashes, good_hash));

  EXPECT_TRUE(base::ContainsValue(dynamic_pkp_state.spki_hashes, backup_hash));

  // Expect the overall state to reflect the header, too.
  EXPECT_TRUE(state.HasPublicKeyPins(domain));
  HashValueVector hashes;
  hashes.push_back(good_hash);
  std::string failure_log;
  const bool is_issued_by_known_root = true;
  HostPortPair domain_port(domain, 443);
  EXPECT_EQ(TransportSecurityState::PKPStatus::OK,
            state.CheckPublicKeyPins(
                domain_port, is_issued_by_known_root, hashes, nullptr, nullptr,
                TransportSecurityState::DISABLE_PIN_REPORTS, &failure_log));

  TransportSecurityState::PKPState new_dynamic_pkp_state;
  EXPECT_TRUE(state.GetDynamicPKPState(domain, &new_dynamic_pkp_state));
  EXPECT_EQ(2UL, new_dynamic_pkp_state.spki_hashes.size());
  EXPECT_EQ(report_uri, new_dynamic_pkp_state.report_uri);

  EXPECT_TRUE(
      base::ContainsValue(new_dynamic_pkp_state.spki_hashes, good_hash));

  EXPECT_TRUE(
      base::ContainsValue(new_dynamic_pkp_state.spki_hashes, backup_hash));
}

TEST_F(HttpSecurityHeadersTest, UpdateDynamicPKPMaxAge0) {
  SetTransportSecurityStateSourceForTesting(&test_default::kHSTSSource);

  TransportSecurityState state;
  TransportSecurityState::STSState static_sts_state;
  TransportSecurityState::PKPState static_pkp_state;

  std::string domain = "no-rejected-pins-pkp.preloaded.test";
  state.enable_static_pins_ = true;
  ASSERT_TRUE(
      state.GetStaticDomainState(domain, &static_sts_state, &static_pkp_state));
  EXPECT_GT(static_pkp_state.spki_hashes.size(), 1UL);
  HashValueVector saved_hashes = static_pkp_state.spki_hashes;

  // Add a header, which should only update the dynamic state.
  HashValue good_hash = GetTestHashValue(1, HASH_VALUE_SHA256);
  std::string good_pin = GetTestPin(1, HASH_VALUE_SHA256);
  std::string backup_pin = GetTestPin(2, HASH_VALUE_SHA256);
  std::string header = "max-age = 10000; " + good_pin + "; " + backup_pin;

  // Construct a fake SSLInfo that will pass AddHPKPHeader's checks.
  SSLInfo ssl_info;
  ssl_info.public_key_hashes.push_back(good_hash);
  ssl_info.public_key_hashes.push_back(saved_hashes[0]);
  EXPECT_TRUE(state.AddHPKPHeader(domain, header, ssl_info));

  // Expect the static state to remain unchanged.
  TransportSecurityState::STSState new_static_sts_state;
  TransportSecurityState::PKPState new_static_pkp_state;
  EXPECT_TRUE(state.GetStaticDomainState(domain, &new_static_sts_state,
                                         &new_static_pkp_state));
  EXPECT_EQ(saved_hashes, new_static_pkp_state.spki_hashes);

  // Expect the dynamic state to have pins.
  TransportSecurityState::PKPState new_dynamic_pkp_state;
  EXPECT_TRUE(state.GetDynamicPKPState(domain, &new_dynamic_pkp_state));
  EXPECT_EQ(2UL, new_dynamic_pkp_state.spki_hashes.size());
  EXPECT_TRUE(new_dynamic_pkp_state.HasPublicKeyPins());

  // Now set another header with max-age=0, and check that the pins are
  // cleared in the dynamic state only.
  header = "max-age = 0; " + good_pin + "; " + backup_pin;
  EXPECT_TRUE(state.AddHPKPHeader(domain, header, ssl_info));

  // Expect the static state to remain unchanged.
  TransportSecurityState::PKPState new_static_pkp_state2;
  EXPECT_TRUE(state.GetStaticDomainState(domain, &static_sts_state,
                                         &new_static_pkp_state2));
  EXPECT_EQ(saved_hashes, new_static_pkp_state2.spki_hashes);

  // Expect the dynamic pins to be gone.
  TransportSecurityState::PKPState new_dynamic_pkp_state2;
  EXPECT_FALSE(state.GetDynamicPKPState(domain, &new_dynamic_pkp_state2));

  // Expect the exact-matching static policy to continue to apply, even
  // though dynamic policy has been removed. (This policy may change in the
  // future, in which case this test must be updated.)
  EXPECT_TRUE(state.HasPublicKeyPins(domain));
  EXPECT_TRUE(state.ShouldSSLErrorsBeFatal(domain));
  std::string failure_log;

  // Damage the hashes to cause a pin validation failure.
  for (size_t i = 0; i < new_static_pkp_state2.spki_hashes.size(); i++) {
    new_static_pkp_state2.spki_hashes[i].data()[0] ^= 0x80;
  }

  const bool is_issued_by_known_root = true;
  HostPortPair domain_port(domain, 443);
  EXPECT_EQ(TransportSecurityState::PKPStatus::VIOLATED,
            state.CheckPublicKeyPins(
                domain_port, is_issued_by_known_root,
                new_static_pkp_state2.spki_hashes, nullptr, nullptr,
                TransportSecurityState::DISABLE_PIN_REPORTS, &failure_log));
  EXPECT_NE(0UL, failure_log.length());
}

// Tests that when a static HSTS and a static HPKP entry are present, adding a
// dynamic HSTS header does not clobber the static HPKP entry. Further, adding a
// dynamic HPKP entry could not affect the HSTS entry for the site.
TEST_F(HttpSecurityHeadersTest, NoClobberPins) {
  SetTransportSecurityStateSourceForTesting(&test_default::kHSTSSource);

  TransportSecurityState state;
  TransportSecurityState::STSState sts_state;
  TransportSecurityState::PKPState pkp_state;

  std::string domain = "hsts-hpkp-preloaded.test";
  state.enable_static_pins_ = true;

  // Retrieve the static STS and PKP states as it is by default, including its
  // known good pins.
  EXPECT_TRUE(state.GetStaticDomainState(domain, &sts_state, &pkp_state));
  HashValueVector saved_hashes = pkp_state.spki_hashes;
  EXPECT_TRUE(sts_state.ShouldUpgradeToSSL());
  EXPECT_TRUE(pkp_state.HasPublicKeyPins());
  EXPECT_TRUE(state.ShouldUpgradeToSSL(domain));
  EXPECT_TRUE(state.HasPublicKeyPins(domain));

  // Add a dynamic HSTS header. CheckPublicKeyPins should still pass when given
  // the original |saved_hashes|, indicating that the static PKP data is still
  // configured for the domain.
  EXPECT_TRUE(state.AddHSTSHeader(domain, "includesubdomains; max-age=10000"));
  EXPECT_TRUE(state.ShouldUpgradeToSSL(domain));
  std::string failure_log;
  const bool is_issued_by_known_root = true;
  HostPortPair domain_port(domain, 443);
  EXPECT_EQ(
      TransportSecurityState::PKPStatus::OK,
      state.CheckPublicKeyPins(
          domain_port, is_issued_by_known_root, saved_hashes, nullptr, nullptr,
          TransportSecurityState::DISABLE_PIN_REPORTS, &failure_log));

  // Add an HPKP header, which should only update the dynamic state.
  HashValue good_hash = GetTestHashValue(1, HASH_VALUE_SHA256);
  std::string good_pin = GetTestPin(1, HASH_VALUE_SHA256);
  std::string backup_pin = GetTestPin(2, HASH_VALUE_SHA256);
  std::string header = "max-age = 10000; " + good_pin + "; " + backup_pin;

  // Construct a fake SSLInfo that will pass AddHPKPHeader's checks.
  SSLInfo ssl_info;
  ssl_info.public_key_hashes.push_back(good_hash);
  ssl_info.public_key_hashes.push_back(saved_hashes[0]);
  EXPECT_TRUE(state.AddHPKPHeader(domain, header, ssl_info));

  EXPECT_TRUE(state.AddHPKPHeader(domain, header, ssl_info));
  // HSTS should still be configured for this domain.
  EXPECT_TRUE(sts_state.ShouldUpgradeToSSL());
  EXPECT_TRUE(state.ShouldUpgradeToSSL(domain));
  // The dynamic pins, which do not match |saved_hashes|, should take
  // precedence over the static pins and cause the check to fail.
  EXPECT_EQ(
      TransportSecurityState::PKPStatus::VIOLATED,
      state.CheckPublicKeyPins(
          domain_port, is_issued_by_known_root, saved_hashes, nullptr, nullptr,
          TransportSecurityState::DISABLE_PIN_REPORTS, &failure_log));
}

// Tests that seeing an invalid HPKP header leaves the existing one alone.
TEST_F(HttpSecurityHeadersTest, IgnoreInvalidHeaders) {
  TransportSecurityState state;

  HashValue good_hash = GetTestHashValue(1, HASH_VALUE_SHA256);
  std::string good_pin = GetTestPin(1, HASH_VALUE_SHA256);
  std::string bad_pin = GetTestPin(2, HASH_VALUE_SHA256);
  std::string backup_pin = GetTestPin(3, HASH_VALUE_SHA256);

  SSLInfo ssl_info;
  ssl_info.public_key_hashes.push_back(good_hash);

  // Add a valid HPKP header.
  EXPECT_TRUE(state.AddHPKPHeader(
      "example.com", "max-age = 10000; " + good_pin + "; " + backup_pin,
      ssl_info));

  // Check the insertion was valid.
  EXPECT_TRUE(state.HasPublicKeyPins("example.com"));
  std::string failure_log;
  bool is_issued_by_known_root = true;
  HostPortPair domain_port("example.com", 443);
  EXPECT_EQ(TransportSecurityState::PKPStatus::OK,
            state.CheckPublicKeyPins(
                domain_port, is_issued_by_known_root,
                ssl_info.public_key_hashes, nullptr, nullptr,
                TransportSecurityState::DISABLE_PIN_REPORTS, &failure_log));

  // Now assert an invalid one. This should fail.
  EXPECT_FALSE(state.AddHPKPHeader(
      "example.com", "max-age = 10000; " + bad_pin + "; " + backup_pin,
      ssl_info));

  // The old pins must still exist.
  EXPECT_TRUE(state.HasPublicKeyPins("example.com"));
  EXPECT_EQ(TransportSecurityState::PKPStatus::OK,
            state.CheckPublicKeyPins(
                domain_port, is_issued_by_known_root,
                ssl_info.public_key_hashes, nullptr, nullptr,
                TransportSecurityState::DISABLE_PIN_REPORTS, &failure_log));
}

TEST_F(HttpSecurityHeadersTest, BogusExpectCTHeaders) {
  base::TimeDelta max_age;
  bool enforce = false;
  GURL report_uri;
  EXPECT_FALSE(
      ParseExpectCTHeader(std::string(), &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("    ", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("abc", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("  abc", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("  abc   ", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("  max-age", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("  max-age  ", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-age=", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("   max-age=", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("   max-age  =", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("   max-age=   ", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("   max-age  =     ", &max_age, &enforce,
                                   &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("   max-age  =     xy", &max_age, &enforce,
                                   &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("   max-age  =     3488a923", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488a923  ", &max_age, &enforce,
                                   &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-ag=3488923", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-aged=3488923", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-age==3488923", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("amax-age=3488923", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-age=-3488923", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-age=+3488923", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-age=13####", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=9223372036854775807#####", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=18446744073709551615####", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=999999999999999999999999$.&#!",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923     e", &max_age, &enforce,
                                   &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923     includesubdomain",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923includesubdomains", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923=includesubdomains",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923 includesubdomainx",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader(
      "max-age=3488923 includesubdomain=", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923 includesubdomain=true",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923 includesubdomainsx",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=3488923 includesubdomains x",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=34889.23 includesubdomains",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=34889 includesubdomains", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader(",,,, ,,,", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader(",,,, includeSubDomains,,,", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("   includeSubDomains,  ", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader(",", &max_age, &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("max-age, ,", &max_age, &enforce, &report_uri));

  // Test that the parser rejects misquoted or invalid report-uris.
  EXPECT_FALSE(ParseExpectCTHeader("max-age=999, report-uri=\"http://foo;bar\'",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=999, report-uri=\"foo;bar\"",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("max-age=999, report-uri=\"\"", &max_age,
                                   &enforce, &report_uri));

  // Test that the parser does not fix up misquoted values.
  EXPECT_FALSE(
      ParseExpectCTHeader("max-age=\"999", &max_age, &enforce, &report_uri));

  // Test that the parser rejects headers that contain duplicate directives.
  EXPECT_FALSE(ParseExpectCTHeader("max-age=999, enforce, max-age=99999",
                                   &max_age, &enforce, &report_uri));
  EXPECT_FALSE(ParseExpectCTHeader("enforce, max-age=999, enforce", &max_age,
                                   &enforce, &report_uri));
  EXPECT_FALSE(
      ParseExpectCTHeader("report-uri=\"http://foo\", max-age=999, enforce, "
                          "report-uri=\"http://foo\"",
                          &max_age, &enforce, &report_uri));

  // Test that the parser rejects headers with values for the valueless
  // 'enforce' directive.
  EXPECT_FALSE(ParseExpectCTHeader("max-age=999, enforce=true", &max_age,
                                   &enforce, &report_uri));

  // Check the out args were not updated by checking the default
  // values for its predictable fields.
  EXPECT_EQ(0, max_age.InSeconds());
  EXPECT_FALSE(enforce);
  EXPECT_TRUE(report_uri.is_empty());
}

TEST_F(HttpSecurityHeadersTest, ValidExpectCTHeaders) {
  base::TimeDelta max_age;
  bool enforce = false;
  GURL report_uri;

  EXPECT_TRUE(
      ParseExpectCTHeader("max-age=243", &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(243), max_age);
  EXPECT_FALSE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  EXPECT_TRUE(ParseExpectCTHeader("  Max-agE    = 567", &max_age, &enforce,
                                  &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(567), max_age);
  EXPECT_FALSE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  EXPECT_TRUE(ParseExpectCTHeader("  mAx-aGe    = 890      ", &max_age,
                                  &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(890), max_age);
  EXPECT_FALSE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  EXPECT_TRUE(ParseExpectCTHeader("max-age=123,enFoRce", &max_age, &enforce,
                                  &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("enFoRCE, max-age=123", &max_age, &enforce,
                                  &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("   enFORce, max-age=123", &max_age, &enforce,
                                  &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader(
      "report-uri=\"https://foo.test\",   enFORce, max-age=123", &max_age,
      &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_EQ(GURL("https://foo.test"), report_uri);

  enforce = false;
  report_uri = GURL();
  EXPECT_TRUE(
      ParseExpectCTHeader("enforce,report-uri=\"https://foo.test\",max-age=123",
                          &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_EQ(GURL("https://foo.test"), report_uri);

  enforce = false;
  report_uri = GURL();
  EXPECT_TRUE(
      ParseExpectCTHeader("enforce,report-uri=https://foo.test,max-age=123",
                          &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_EQ(GURL("https://foo.test"), report_uri);

  report_uri = GURL();
  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("report-uri=\"https://foo.test\",max-age=123",
                                  &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_FALSE(enforce);
  EXPECT_EQ(GURL("https://foo.test"), report_uri);

  report_uri = GURL();
  EXPECT_TRUE(ParseExpectCTHeader("   enFORcE, max-age=123, pumpkin=kitten",
                                  &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader(
      "   pumpkin=894, report-uri=     \"https://bar\", enFORce, max-age=123  ",
      &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_EQ(GURL("https://bar"), report_uri);

  enforce = false;
  report_uri = GURL();
  EXPECT_TRUE(ParseExpectCTHeader("   pumpkin, enFoRcE, max-age=123  ",
                                  &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("   pumpkin, enforce, max-age=\"123\"  ",
                                  &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader(
      "animal=\"squirrel, distinguished\", enFoRce, max-age=123", &max_age,
      &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(123), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("max-age=394082,  enforce", &max_age,
                                  &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(394082), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("max-age=39408299  ,enforce", &max_age,
                                  &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(kMaxExpectCTAgeSecs), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  // Per RFC 7230, "a recipient MUST parse and ignore a reasonable number of
  // empty list elements".
  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader(",, max-age=394082038  , enfoRce, ,",
                                  &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(kMaxExpectCTAgeSecs), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader(",, max-age=394082038  ,", &max_age, &enforce,
                                  &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(kMaxExpectCTAgeSecs), max_age);
  EXPECT_FALSE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  EXPECT_TRUE(
      ParseExpectCTHeader(",,    , , max-age=394082038,,, enforce     ,,  ,",
                          &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(kMaxExpectCTAgeSecs), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("enfORce   , max-age=394082038 ,,", &max_age,
                                  &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(kMaxExpectCTAgeSecs), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader("  max-age=0  ,  enforce   ", &max_age,
                                  &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(0), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());

  enforce = false;
  EXPECT_TRUE(ParseExpectCTHeader(
      "  max-age=999999999999999999999999999999999999999999999  ,"
      "  enforce   ",
      &max_age, &enforce, &report_uri));
  EXPECT_EQ(base::TimeDelta::FromSeconds(kMaxExpectCTAgeSecs), max_age);
  EXPECT_TRUE(enforce);
  EXPECT_TRUE(report_uri.is_empty());
}

}  // namespace net
