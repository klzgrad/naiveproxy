// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/curve25519_key_exchange.h"

#include <memory>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {

class Curve25519KeyExchangeTest : public QuicTest {
 public:
  // Holds the result of a key exchange callback.
  class TestCallbackResult {
   public:
    void set_ok(bool ok) { ok_ = ok; }
    bool ok() { return ok_; }

   private:
    bool ok_ = false;
  };

  // Key exchange callback which sets the result into the specified
  // TestCallbackResult.
  class TestCallback : public AsynchronousKeyExchange::Callback {
   public:
    TestCallback(TestCallbackResult* result) : result_(result) {}
    virtual ~TestCallback() = default;

    void Run(bool ok) { result_->set_ok(ok); }

   private:
    TestCallbackResult* result_;
  };
};

// SharedKey just tests that the basic key exchange identity holds: that both
// parties end up with the same key.
TEST_F(Curve25519KeyExchangeTest, SharedKey) {
  QuicRandom* const rand = QuicRandom::GetInstance();

  for (int i = 0; i < 5; i++) {
    const std::string alice_key(Curve25519KeyExchange::NewPrivateKey(rand));
    const std::string bob_key(Curve25519KeyExchange::NewPrivateKey(rand));

    std::unique_ptr<Curve25519KeyExchange> alice(
        Curve25519KeyExchange::New(alice_key));
    std::unique_ptr<Curve25519KeyExchange> bob(
        Curve25519KeyExchange::New(bob_key));

    const quiche::QuicheStringPiece alice_public(alice->public_value());
    const quiche::QuicheStringPiece bob_public(bob->public_value());

    std::string alice_shared, bob_shared;
    ASSERT_TRUE(alice->CalculateSharedKeySync(bob_public, &alice_shared));
    ASSERT_TRUE(bob->CalculateSharedKeySync(alice_public, &bob_shared));
    ASSERT_EQ(alice_shared, bob_shared);
  }
}

// SharedKeyAsync just tests that the basic asynchronous key exchange identity
// holds: that both parties end up with the same key.
TEST_F(Curve25519KeyExchangeTest, SharedKeyAsync) {
  QuicRandom* const rand = QuicRandom::GetInstance();

  for (int i = 0; i < 5; i++) {
    const std::string alice_key(Curve25519KeyExchange::NewPrivateKey(rand));
    const std::string bob_key(Curve25519KeyExchange::NewPrivateKey(rand));

    std::unique_ptr<Curve25519KeyExchange> alice(
        Curve25519KeyExchange::New(alice_key));
    std::unique_ptr<Curve25519KeyExchange> bob(
        Curve25519KeyExchange::New(bob_key));

    const quiche::QuicheStringPiece alice_public(alice->public_value());
    const quiche::QuicheStringPiece bob_public(bob->public_value());

    std::string alice_shared, bob_shared;
    TestCallbackResult alice_result;
    ASSERT_FALSE(alice_result.ok());
    alice->CalculateSharedKeyAsync(
        bob_public, &alice_shared,
        std::make_unique<TestCallback>(&alice_result));
    ASSERT_TRUE(alice_result.ok());
    TestCallbackResult bob_result;
    ASSERT_FALSE(bob_result.ok());
    bob->CalculateSharedKeyAsync(alice_public, &bob_shared,
                                 std::make_unique<TestCallback>(&bob_result));
    ASSERT_TRUE(bob_result.ok());
    ASSERT_EQ(alice_shared, bob_shared);
    ASSERT_NE(0u, alice_shared.length());
    ASSERT_NE(0u, bob_shared.length());
  }
}

}  // namespace test
}  // namespace quic
