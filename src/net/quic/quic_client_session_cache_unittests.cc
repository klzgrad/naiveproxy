// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_client_session_cache.h"

#include "base/run_loop.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace net {

namespace {

const quic::QuicVersionLabel kFakeVersionLabel = 0x01234567;
const quic::QuicVersionLabel kFakeVersionLabel2 = 0x89ABCDEF;
const uint64_t kFakeIdleTimeoutMilliseconds = 12012;
const uint8_t kFakeStatelessResetTokenData[16] = {
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F};
const uint64_t kFakeMaxPacketSize = 9001;
const uint64_t kFakeInitialMaxData = 101;
const bool kFakeDisableMigration = true;
const auto kCustomParameter1 =
    static_cast<quic::TransportParameters::TransportParameterId>(0xffcd);
const char* kCustomParameter1Value = "foo";
const auto kCustomParameter2 =
    static_cast<quic::TransportParameters::TransportParameterId>(0xff34);
const char* kCustomParameter2Value = "bar";

std::vector<uint8_t> CreateFakeStatelessResetToken() {
  return std::vector<uint8_t>(
      kFakeStatelessResetTokenData,
      kFakeStatelessResetTokenData + base::size(kFakeStatelessResetTokenData));
}

std::unique_ptr<base::SimpleTestClock> MakeTestClock() {
  std::unique_ptr<base::SimpleTestClock> clock =
      std::make_unique<base::SimpleTestClock>();
  // SimpleTestClock starts at the null base::Time which converts to and from
  // time_t confusingly.
  clock->SetNow(base::Time::FromTimeT(1000000000));
  return clock;
}

// Make a TransportParameters that has a few fields set to help test comparison.
std::unique_ptr<quic::TransportParameters> MakeFakeTransportParams() {
  auto params = std::make_unique<quic::TransportParameters>();
  params->perspective = quic::Perspective::IS_CLIENT;
  params->version = kFakeVersionLabel;
  params->supported_versions.push_back(kFakeVersionLabel);
  params->supported_versions.push_back(kFakeVersionLabel2);
  params->idle_timeout_milliseconds.set_value(kFakeIdleTimeoutMilliseconds);
  params->stateless_reset_token = CreateFakeStatelessResetToken();
  params->max_packet_size.set_value(kFakeMaxPacketSize);
  params->initial_max_data.set_value(kFakeInitialMaxData);
  params->disable_migration = kFakeDisableMigration;
  params->custom_parameters[kCustomParameter1] = kCustomParameter1Value;
  params->custom_parameters[kCustomParameter2] = kCustomParameter2Value;
  return params;
}

class QuicClientSessionCacheTest : public testing::Test {
 public:
  QuicClientSessionCacheTest() : ssl_ctx_(SSL_CTX_new(TLS_method())) {}

 protected:
  bssl::UniquePtr<SSL_SESSION> NewSSLSession() {
    SSL_SESSION* session = SSL_SESSION_new(ssl_ctx_.get());
    if (!SSL_SESSION_set_protocol_version(session, TLS1_3_VERSION))
      return nullptr;
    return bssl::UniquePtr<SSL_SESSION>(session);
  }

  bssl::UniquePtr<SSL_SESSION> MakeTestSession(base::Time now,
                                               base::TimeDelta timeout) {
    bssl::UniquePtr<SSL_SESSION> session = NewSSLSession();
    SSL_SESSION_set_time(session.get(), now.ToTimeT());
    SSL_SESSION_set_timeout(session.get(), timeout.InSeconds());
    return session;
  }

  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
};

}  // namespace

// Tests that simple insertion and lookup work correctly.
TEST_F(QuicClientSessionCacheTest, SingleSession) {
  QuicClientSessionCache cache;

  auto params = MakeFakeTransportParams();
  auto session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);

  auto params2 = MakeFakeTransportParams();
  auto session2 = NewSSLSession();
  SSL_SESSION* unowned2 = session2.get();
  quic::QuicServerId id2("b.com", 443);

  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache.Lookup(id2, ssl_ctx_.get()));
  EXPECT_EQ(0u, cache.size());

  cache.Insert(id1, std::move(session), *params, nullptr);
  EXPECT_EQ(1u, cache.size());
  EXPECT_EQ(*params, *(cache.Lookup(id1, ssl_ctx_.get())->transport_params));
  EXPECT_EQ(nullptr, cache.Lookup(id2, ssl_ctx_.get()));
  // No session is available for id1, even though the entry exists.
  EXPECT_EQ(1u, cache.size());
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  // Lookup() will trigger a deletion of invalid entry.
  EXPECT_EQ(0u, cache.size());

  auto session3 = NewSSLSession();
  SSL_SESSION* unowned3 = session3.get();
  quic::QuicServerId id3("c.com", 443);
  cache.Insert(id3, std::move(session3), *params, nullptr);
  cache.Insert(id2, std::move(session2), *params2, nullptr);
  EXPECT_EQ(2u, cache.size());
  EXPECT_EQ(unowned2, cache.Lookup(id2, ssl_ctx_.get())->tls_session.get());
  EXPECT_EQ(unowned3, cache.Lookup(id3, ssl_ctx_.get())->tls_session.get());

  // Verify that the cache is cleared after Lookups.
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache.Lookup(id2, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache.Lookup(id3, ssl_ctx_.get()));
  EXPECT_EQ(0u, cache.size());
}

TEST_F(QuicClientSessionCacheTest, MultipleSessions) {
  QuicClientSessionCache cache;

  auto params = MakeFakeTransportParams();
  auto session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);
  auto session2 = NewSSLSession();
  SSL_SESSION* unowned2 = session2.get();
  auto session3 = NewSSLSession();
  SSL_SESSION* unowned3 = session3.get();

  cache.Insert(id1, std::move(session), *params, nullptr);
  cache.Insert(id1, std::move(session2), *params, nullptr);
  cache.Insert(id1, std::move(session3), *params, nullptr);
  // The latest session is popped first.
  EXPECT_EQ(unowned3, cache.Lookup(id1, ssl_ctx_.get())->tls_session.get());
  EXPECT_EQ(unowned2, cache.Lookup(id1, ssl_ctx_.get())->tls_session.get());
  // Only two sessions are cached.
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
}

// Test that when a different TransportParameter is inserted for
// the same server id, the existing entry is removed.
TEST_F(QuicClientSessionCacheTest, DifferentTransportParams) {
  QuicClientSessionCache cache;

  auto params = MakeFakeTransportParams();
  auto session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);
  auto session2 = NewSSLSession();
  auto session3 = NewSSLSession();
  SSL_SESSION* unowned3 = session3.get();

  cache.Insert(id1, std::move(session), *params, nullptr);
  cache.Insert(id1, std::move(session2), *params, nullptr);
  // tweak the transport parameters a little bit.
  params->perspective = quic::Perspective::IS_SERVER;
  cache.Insert(id1, std::move(session3), *params, nullptr);
  auto resumption_state = cache.Lookup(id1, ssl_ctx_.get());
  EXPECT_EQ(unowned3, resumption_state->tls_session.get());
  EXPECT_EQ(*params.get(), *resumption_state->transport_params);
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
}

TEST_F(QuicClientSessionCacheTest, DifferentApplicationState) {
  QuicClientSessionCache cache;

  auto params = MakeFakeTransportParams();
  auto session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);
  auto session2 = NewSSLSession();
  auto session3 = NewSSLSession();
  SSL_SESSION* unowned3 = session3.get();
  quic::ApplicationState state;
  state.push_back('a');

  cache.Insert(id1, std::move(session), *params, &state);
  cache.Insert(id1, std::move(session2), *params, &state);
  cache.Insert(id1, std::move(session3), *params, nullptr);
  auto resumption_state = cache.Lookup(id1, ssl_ctx_.get());
  EXPECT_EQ(unowned3, resumption_state->tls_session.get());
  EXPECT_EQ(nullptr, resumption_state->application_state);
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
}

TEST_F(QuicClientSessionCacheTest, BothStatesDifferent) {
  QuicClientSessionCache cache;

  auto params = MakeFakeTransportParams();
  auto session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);
  auto session2 = NewSSLSession();
  auto session3 = NewSSLSession();
  SSL_SESSION* unowned3 = session3.get();
  quic::ApplicationState state;
  state.push_back('a');

  cache.Insert(id1, std::move(session), *params, &state);
  cache.Insert(id1, std::move(session2), *params, &state);
  params->perspective = quic::Perspective::IS_SERVER;
  cache.Insert(id1, std::move(session3), *params, nullptr);
  auto resumption_state = cache.Lookup(id1, ssl_ctx_.get());
  EXPECT_EQ(unowned3, resumption_state->tls_session.get());
  EXPECT_EQ(*params.get(), *resumption_state->transport_params);
  EXPECT_EQ(nullptr, resumption_state->application_state);
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
}

// When the size limit is exceeded, the oldest entry should be erased.
TEST_F(QuicClientSessionCacheTest, SizeLimit) {
  QuicClientSessionCache cache(2);

  auto params = MakeFakeTransportParams();
  auto session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);

  auto session2 = NewSSLSession();
  SSL_SESSION* unowned2 = session2.get();
  quic::QuicServerId id2("b.com", 443);

  auto session3 = NewSSLSession();
  SSL_SESSION* unowned3 = session3.get();
  quic::QuicServerId id3("c.com", 443);

  cache.Insert(id1, std::move(session), *params, nullptr);
  cache.Insert(id2, std::move(session2), *params, nullptr);
  cache.Insert(id3, std::move(session3), *params, nullptr);

  EXPECT_EQ(2u, cache.size());
  EXPECT_EQ(unowned2, cache.Lookup(id2, ssl_ctx_.get())->tls_session.get());
  EXPECT_EQ(unowned3, cache.Lookup(id3, ssl_ctx_.get())->tls_session.get());
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
}

// Expired session isn't considered valid and nullptr will be returned upon
// Lookup.
TEST_F(QuicClientSessionCacheTest, Expiration) {
  const base::TimeDelta kTimeout = base::TimeDelta::FromSeconds(1000);
  QuicClientSessionCache cache;
  std::unique_ptr<base::SimpleTestClock> clock = MakeTestClock();
  cache.SetClockForTesting(clock.get());

  auto params = MakeFakeTransportParams();
  auto session = MakeTestSession(clock->Now(), kTimeout);
  quic::QuicServerId id1("a.com", 443);

  auto session2 = MakeTestSession(clock->Now(), 3 * kTimeout);
  SSL_SESSION* unowned2 = session2.get();
  quic::QuicServerId id2("b.com", 443);

  cache.Insert(id1, std::move(session), *params, nullptr);
  cache.Insert(id2, std::move(session2), *params, nullptr);

  EXPECT_EQ(2u, cache.size());
  // Expire the session.
  clock->Advance(kTimeout * 2);
  // The entry has not been removed yet.
  EXPECT_EQ(2u, cache.size());

  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(1u, cache.size());
  EXPECT_EQ(unowned2, cache.Lookup(id2, ssl_ctx_.get())->tls_session.get());
  EXPECT_EQ(1u, cache.size());
}

TEST_F(QuicClientSessionCacheTest, FlushOnMemoryNotifications) {
  base::test::TaskEnvironment task_environment;
  const base::TimeDelta kTimeout = base::TimeDelta::FromSeconds(1000);
  QuicClientSessionCache cache;
  std::unique_ptr<base::SimpleTestClock> clock = MakeTestClock();
  cache.SetClockForTesting(clock.get());

  auto params = MakeFakeTransportParams();
  auto session = MakeTestSession(clock->Now(), kTimeout);
  quic::QuicServerId id1("a.com", 443);

  auto session2 = MakeTestSession(clock->Now(), 3 * kTimeout);
  quic::QuicServerId id2("b.com", 443);

  cache.Insert(id1, std::move(session), *params, nullptr);
  cache.Insert(id2, std::move(session2), *params, nullptr);

  EXPECT_EQ(2u, cache.size());
  // Expire the session.
  clock->Advance(kTimeout * 2);
  // The entry has not been removed yet.
  EXPECT_EQ(2u, cache.size());

  // Fire a notification that will flush expired sessions.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE);
  base::RunLoop().RunUntilIdle();

  // session is expired and should be flushed.
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(1u, cache.size());

  // Fire notification that will flush everything.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, cache.size());
}

}  // namespace net
