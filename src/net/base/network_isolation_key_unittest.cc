// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_isolation_key.h"

#include "base/stl_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {

TEST(NetworkIsolationKeyTest, EmptyKey) {
  NetworkIsolationKey key;
  EXPECT_FALSE(key.IsFullyPopulated());
  EXPECT_EQ(std::string(), key.ToString());
  EXPECT_TRUE(key.IsTransient());
  EXPECT_EQ("null", key.ToDebugString());
}

TEST(NetworkIsolationKeyTest, NonEmptyKey) {
  url::Origin origin = url::Origin::Create(GURL("http://a.test/"));
  NetworkIsolationKey key(origin);
  EXPECT_TRUE(key.IsFullyPopulated());
  EXPECT_EQ(origin.Serialize(), key.ToString());
  EXPECT_FALSE(key.IsTransient());
  EXPECT_EQ("http://a.test", key.ToDebugString());
}

TEST(NetworkIsolationKeyTest, OpaqueOriginKey) {
  url::Origin origin_data =
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"));
  NetworkIsolationKey key(origin_data);
  EXPECT_TRUE(key.IsFullyPopulated());
  EXPECT_EQ(std::string(), key.ToString());
  EXPECT_TRUE(key.IsTransient());

  // Create another opaque origin, and make sure it has a different debug
  // string.
  EXPECT_NE(
      key.ToDebugString(),
      NetworkIsolationKey(origin_data.DeriveNewOpaqueOrigin()).ToDebugString());
}

TEST(NetworkIsolationKeyTest, Operators) {
  // These are in ascending order.
  const NetworkIsolationKey kKeys[] = {
      NetworkIsolationKey(),
      // Unique origins are still sorted by scheme, so data is before file, and
      // file before http.
      NetworkIsolationKey(
          url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"))),
      NetworkIsolationKey(url::Origin::Create(GURL("file:///foo"))),
      NetworkIsolationKey(url::Origin::Create(GURL("http://a.test/"))),
      NetworkIsolationKey(url::Origin::Create(GURL("http://b.test/"))),
      NetworkIsolationKey(url::Origin::Create(GURL("https://a.test/"))),
  };

  for (size_t first = 0; first < base::size(kKeys); ++first) {
    NetworkIsolationKey key1 = kKeys[first];
    SCOPED_TRACE(key1.ToDebugString());

    EXPECT_TRUE(key1 == key1);
    EXPECT_FALSE(key1 < key1);

    // Make sure that copying a key doesn't change the results of any operation.
    // This check is a bit more interesting with unique origins.
    NetworkIsolationKey key1_copy = key1;
    EXPECT_TRUE(key1 == key1_copy);
    EXPECT_FALSE(key1 < key1_copy);
    EXPECT_FALSE(key1_copy < key1);

    for (size_t second = first + 1; second < base::size(kKeys); ++second) {
      NetworkIsolationKey key2 = kKeys[second];
      SCOPED_TRACE(key2.ToDebugString());

      EXPECT_TRUE(key1 < key2);
      EXPECT_FALSE(key2 < key1);
      EXPECT_FALSE(key1 == key2);
      EXPECT_FALSE(key2 == key1);
    }
  }
}

TEST(NetworkIsolationKeyTest, UniqueOriginOperators) {
  NetworkIsolationKey key1(
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>")));
  NetworkIsolationKey key2(
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>")));

  EXPECT_TRUE(key1 == key1);
  EXPECT_TRUE(key2 == key2);

  // Creating copies shouldn't affect comparison result.
  EXPECT_TRUE(NetworkIsolationKey(key1) == NetworkIsolationKey(key1));
  EXPECT_TRUE(NetworkIsolationKey(key2) == NetworkIsolationKey(key2));

  EXPECT_FALSE(key1 == key2);
  EXPECT_FALSE(key2 == key1);

  // Order of Nonces isn't predictable, but they should have an ordering.
  EXPECT_TRUE(key1 < key2 || key2 < key1);
  EXPECT_TRUE(!(key1 < key2) || !(key2 < key1));
}

}  // namespace net
