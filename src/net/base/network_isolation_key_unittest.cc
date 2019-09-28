// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_isolation_key.h"

#include "base/stl_util.h"
#include "base/test/scoped_feature_list.h"
#include "net/base/features.h"
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
  NetworkIsolationKey key(origin, origin);
  EXPECT_TRUE(key.IsFullyPopulated());
  EXPECT_EQ(origin.Serialize(), key.ToString());
  EXPECT_FALSE(key.IsTransient());
  EXPECT_EQ("http://a.test", key.ToDebugString());
}

TEST(NetworkIsolationKeyTest, OpaqueOriginKey) {
  url::Origin origin_data =
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"));
  NetworkIsolationKey key(origin_data, origin_data);
  EXPECT_TRUE(key.IsFullyPopulated());
  EXPECT_EQ(std::string(), key.ToString());
  EXPECT_TRUE(key.IsTransient());

  // Create another opaque origin, and make sure it has a different debug
  // string.
  const auto kOriginNew = origin_data.DeriveNewOpaqueOrigin();
  EXPECT_NE(key.ToDebugString(),
            NetworkIsolationKey(kOriginNew, kOriginNew).ToDebugString());
}

TEST(NetworkIsolationKeyTest, Operators) {
  // These are in ascending order.
  const NetworkIsolationKey kKeys[] = {
      NetworkIsolationKey(),
      // Unique origins are still sorted by scheme, so data is before file, and
      // file before http.
      NetworkIsolationKey(
          url::Origin::Create(GURL("data:text/html,<body>Hello World</body>")),
          url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"))),
      NetworkIsolationKey(url::Origin::Create(GURL("file:///foo")),
                          url::Origin::Create(GURL("file:///foo"))),
      NetworkIsolationKey(url::Origin::Create(GURL("http://a.test/")),
                          url::Origin::Create(GURL("http://a.test/"))),
      NetworkIsolationKey(url::Origin::Create(GURL("http://b.test/")),
                          url::Origin::Create(GURL("http://b.test/"))),
      NetworkIsolationKey(url::Origin::Create(GURL("https://a.test/")),
                          url::Origin::Create(GURL("https://a.test/"))),
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
  const auto kOrigin1 =
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"));
  const auto kOrigin2 =
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"));
  NetworkIsolationKey key1(kOrigin1, kOrigin1);
  NetworkIsolationKey key2(kOrigin2, kOrigin2);

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

TEST(NetworkIsolationKeyTest, WithFrameOrigin) {
  const auto kOriginA = url::Origin::Create(GURL("http://a.test"));
  const auto kOriginB = url::Origin::Create(GURL("http://b.test"));
  NetworkIsolationKey key1(kOriginB, kOriginB);
  NetworkIsolationKey key2(kOriginB, kOriginA);
  EXPECT_TRUE(key2.IsFullyPopulated());
  EXPECT_FALSE(key2.IsTransient());
  EXPECT_EQ("http://b.test", key2.ToString());
  EXPECT_EQ("http://b.test", key2.ToDebugString());

  EXPECT_TRUE(key1 == key2);
  EXPECT_FALSE(key1 != key2);
  EXPECT_FALSE(key1 < key2);
  EXPECT_FALSE(key2 < key1);
}

TEST(NetworkIsolationKeyTest, OpaqueOriginKeyWithFrameOrigin) {
  url::Origin origin_data =
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"));

  NetworkIsolationKey key1(url::Origin::Create(GURL("http://a.test")),
                           origin_data);
  EXPECT_TRUE(key1.IsFullyPopulated());
  EXPECT_FALSE(key1.IsTransient());
  EXPECT_EQ("http://a.test", key1.ToString());
  EXPECT_EQ("http://a.test", key1.ToDebugString());

  NetworkIsolationKey key2(origin_data,
                           url::Origin::Create(GURL("http://a.test")));
  EXPECT_TRUE(key2.IsFullyPopulated());
  EXPECT_TRUE(key2.IsTransient());
  EXPECT_EQ("", key2.ToString());
  EXPECT_EQ(origin_data.GetDebugString(), key2.ToDebugString());
  EXPECT_NE(origin_data.DeriveNewOpaqueOrigin().GetDebugString(),
            key2.ToDebugString());
}

class NetworkIsolationKeyWithFrameOriginTest : public testing::Test {
 public:
  NetworkIsolationKeyWithFrameOriginTest() {
    feature_list_.InitAndEnableFeature(
        net::features::kAppendFrameOriginToNetworkIsolationKey);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

TEST_F(NetworkIsolationKeyWithFrameOriginTest, WithFrameOrigin) {
  NetworkIsolationKey key(url::Origin::Create(GURL("http://b.test")),
                          url::Origin::Create(GURL("http://a.test/")));
  EXPECT_TRUE(key.IsFullyPopulated());
  EXPECT_FALSE(key.IsTransient());
  EXPECT_EQ("http://b.test http://a.test", key.ToString());
  EXPECT_EQ("http://b.test http://a.test", key.ToDebugString());

  EXPECT_TRUE(key == key);
  EXPECT_FALSE(key != key);
  EXPECT_FALSE(key < key);
}

TEST_F(NetworkIsolationKeyWithFrameOriginTest, OpaqueOriginKey) {
  url::Origin origin_data =
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"));

  NetworkIsolationKey key1(url::Origin::Create(GURL("http://a.test")),
                           origin_data);
  EXPECT_TRUE(key1.IsFullyPopulated());
  EXPECT_TRUE(key1.IsTransient());
  EXPECT_EQ("", key1.ToString());
  EXPECT_EQ("http://a.test " + origin_data.GetDebugString(),
            key1.ToDebugString());
  EXPECT_NE(
      "http://a.test " + origin_data.DeriveNewOpaqueOrigin().GetDebugString(),
      key1.ToDebugString());

  NetworkIsolationKey key2(origin_data,
                           url::Origin::Create(GURL("http://a.test")));
  EXPECT_TRUE(key2.IsFullyPopulated());
  EXPECT_TRUE(key2.IsTransient());
  EXPECT_EQ("", key2.ToString());
  EXPECT_EQ(origin_data.GetDebugString() + " http://a.test",
            key2.ToDebugString());
  EXPECT_NE(
      origin_data.DeriveNewOpaqueOrigin().GetDebugString() + " http://a.test",
      key2.ToDebugString());
}

TEST_F(NetworkIsolationKeyWithFrameOriginTest, OpaqueOriginKeyBoth) {
  url::Origin origin_data_1 =
      url::Origin::Create(GURL("data:text/html,<body>Hello World</body>"));
  url::Origin origin_data_2 =
      url::Origin::Create(GURL("data:text/html,<body>Hello Universe</body>"));
  url::Origin origin_data_3 =
      url::Origin::Create(GURL("data:text/html,<body>Hello Cosmos</body>"));

  NetworkIsolationKey key1(origin_data_1, origin_data_2);
  NetworkIsolationKey key2(origin_data_1, origin_data_2);
  NetworkIsolationKey key3(origin_data_1, origin_data_3);

  // All the keys should be fully populated and transient.
  EXPECT_TRUE(key1.IsFullyPopulated());
  EXPECT_TRUE(key2.IsFullyPopulated());
  EXPECT_TRUE(key3.IsFullyPopulated());
  EXPECT_TRUE(key1.IsTransient());
  EXPECT_TRUE(key2.IsTransient());
  EXPECT_TRUE(key3.IsTransient());

  // Test the equality/comparisons of the various keys
  EXPECT_TRUE(key1 == key2);
  EXPECT_FALSE(key1 == key3);
  EXPECT_FALSE(key1 < key2 || key2 < key1);
  EXPECT_TRUE(key1 < key3 || key3 < key1);

  // Test the ToString and ToDebugString
  EXPECT_EQ(key1.ToDebugString(), key2.ToDebugString());
  EXPECT_NE(key1.ToDebugString(), key3.ToDebugString());
  EXPECT_EQ("", key1.ToString());
  EXPECT_EQ("", key2.ToString());
  EXPECT_EQ("", key3.ToString());
}

}  // namespace net
