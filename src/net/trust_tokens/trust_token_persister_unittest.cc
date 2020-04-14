// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/trust_tokens/trust_token_persister.h"

#include <string>
#include <utility>

#include "net/trust_tokens/in_memory_trust_token_persister.h"
#include "net/trust_tokens/proto/public.pb.h"
#include "net/trust_tokens/proto/storage.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using ::testing::IsNull;
using ::testing::Pointee;

namespace net {

namespace {

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

class InMemoryTrustTokenPersisterFactory {
 public:
  static std::unique_ptr<TrustTokenPersister> Create() {
    return std::make_unique<InMemoryTrustTokenPersister>();
  }
};

}  // namespace

template <typename Factory>
class TrustTokenPersisterTest : public ::testing::Test {};

TYPED_TEST_SUITE(TrustTokenPersisterTest, InMemoryTrustTokenPersisterFactory);

TYPED_TEST(TrustTokenPersisterTest, NegativeResults) {
  std::unique_ptr<TrustTokenPersister> persister = TypeParam::Create();

  auto origin = url::Origin::Create(GURL("https://a.com/"));
  EXPECT_THAT(persister->GetIssuerConfig(origin), IsNull());
  EXPECT_THAT(persister->GetToplevelConfig(origin), IsNull());
  EXPECT_THAT(persister->GetIssuerToplevelPairConfig(origin, origin), IsNull());
}

TYPED_TEST(TrustTokenPersisterTest, StoresIssuerConfigs) {
  std::unique_ptr<TrustTokenPersister> persister = TypeParam::Create();
  TrustTokenIssuerConfig config;
  config.set_batch_size(5);

  auto config_to_store = std::make_unique<TrustTokenIssuerConfig>(config);
  auto origin = url::Origin::Create(GURL("https://a.com/"));
  persister->SetIssuerConfig(origin, std::move(config_to_store));

  auto result = persister->GetIssuerConfig(origin);

  EXPECT_THAT(result, Pointee(EqualsProto(config)));
}

TYPED_TEST(TrustTokenPersisterTest, StoresToplevelConfigs) {
  std::unique_ptr<TrustTokenPersister> persister = TypeParam::Create();
  TrustTokenToplevelConfig config;
  *config.add_associated_issuers() = "an issuer";

  auto config_to_store = std::make_unique<TrustTokenToplevelConfig>(config);
  auto origin = url::Origin::Create(GURL("https://a.com/"));
  persister->SetToplevelConfig(origin, std::move(config_to_store));

  auto result = persister->GetToplevelConfig(origin);

  EXPECT_THAT(result, Pointee(EqualsProto(config)));
}

TYPED_TEST(TrustTokenPersisterTest, StoresIssuerToplevelPairConfigs) {
  std::unique_ptr<TrustTokenPersister> persister = TypeParam::Create();
  TrustTokenIssuerToplevelPairConfig config;
  config.set_last_redemption("five o'clock");

  auto config_to_store =
      std::make_unique<TrustTokenIssuerToplevelPairConfig>(config);
  auto toplevel = url::Origin::Create(GURL("https://a.com/"));
  auto issuer = url::Origin::Create(GURL("https://issuer.com/"));
  persister->SetIssuerToplevelPairConfig(issuer, toplevel,
                                         std::move(config_to_store));

  auto result = persister->GetIssuerToplevelPairConfig(issuer, toplevel);

  EXPECT_THAT(result, Pointee(EqualsProto(config)));
}

}  // namespace net
