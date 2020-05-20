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

std::unique_ptr<base::SimpleTestClock> MakeTestClock() {
  std::unique_ptr<base::SimpleTestClock> clock =
      std::make_unique<base::SimpleTestClock>();
  // SimpleTestClock starts at the null base::Time which converts to and from
  // time_t confusingly.
  clock->SetNow(base::Time::FromTimeT(1000000000));
  return clock;
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
TEST_F(QuicClientSessionCacheTest, Basic) {
  QuicClientSessionCache cache;

  std::unique_ptr<quic::QuicResumptionState> state1 =
      std::make_unique<quic::QuicResumptionState>();
  state1->application_state.push_back('a');
  state1->tls_session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);
  std::unique_ptr<quic::QuicResumptionState> state2 =
      std::make_unique<quic::QuicResumptionState>();
  state2->application_state.push_back('b');
  state2->tls_session = NewSSLSession();
  quic::QuicServerId id2("b.com", 443);

  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache.Lookup(id2, ssl_ctx_.get()));
  EXPECT_EQ(0u, cache.size());

  cache.Insert(id1, std::move(state1));
  EXPECT_EQ(1u, cache.size());
  EXPECT_EQ('a', cache.Lookup(id1, ssl_ctx_.get())->application_state.front());
  EXPECT_EQ(nullptr, cache.Lookup(id2, ssl_ctx_.get()));

  std::unique_ptr<quic::QuicResumptionState> state3 =
      std::make_unique<quic::QuicResumptionState>();
  state3->application_state.push_back('c');
  state3->tls_session = NewSSLSession();
  quic::QuicServerId id3("c.com", 443);
  cache.Insert(id3, std::move(state3));
  cache.Insert(id2, std::move(state2));
  EXPECT_EQ(2u, cache.size());
  EXPECT_EQ('b', cache.Lookup(id2, ssl_ctx_.get())->application_state.front());
  EXPECT_EQ('c', cache.Lookup(id3, ssl_ctx_.get())->application_state.front());

  // Verify that the cache is cleared after Lookups.
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache.Lookup(id2, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache.Lookup(id3, ssl_ctx_.get()));
  EXPECT_EQ(0u, cache.size());
}

// When the size limit is exceeded, the oldest entry should be erased.
TEST_F(QuicClientSessionCacheTest, SizeLimit) {
  QuicClientSessionCache cache(2);

  std::unique_ptr<quic::QuicResumptionState> state1 =
      std::make_unique<quic::QuicResumptionState>();
  state1->application_state.push_back('a');
  state1->tls_session = NewSSLSession();
  quic::QuicServerId id1("a.com", 443);

  std::unique_ptr<quic::QuicResumptionState> state2 =
      std::make_unique<quic::QuicResumptionState>();
  state2->application_state.push_back('b');
  state2->tls_session = NewSSLSession();
  quic::QuicServerId id2("b.com", 443);

  std::unique_ptr<quic::QuicResumptionState> state3 =
      std::make_unique<quic::QuicResumptionState>();
  state3->application_state.push_back('c');
  state3->tls_session = NewSSLSession();
  quic::QuicServerId id3("c.com", 443);

  cache.Insert(id1, std::move(state1));
  cache.Insert(id2, std::move(state2));
  cache.Insert(id3, std::move(state3));

  EXPECT_EQ(2u, cache.size());
  EXPECT_EQ('b', cache.Lookup(id2, ssl_ctx_.get())->application_state.front());
  EXPECT_EQ('c', cache.Lookup(id3, ssl_ctx_.get())->application_state.front());
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
}

// Expired session isn't considered valid and nullptr will be returned upon
// Lookup.
TEST_F(QuicClientSessionCacheTest, Expiration) {
  const base::TimeDelta kTimeout = base::TimeDelta::FromSeconds(1000);
  QuicClientSessionCache cache;
  std::unique_ptr<base::SimpleTestClock> clock = MakeTestClock();
  cache.SetClockForTesting(clock.get());

  std::unique_ptr<quic::QuicResumptionState> state1 =
      std::make_unique<quic::QuicResumptionState>();
  state1->tls_session = MakeTestSession(clock->Now(), kTimeout);
  quic::QuicServerId id1("a.com", 443);

  std::unique_ptr<quic::QuicResumptionState> state2 =
      std::make_unique<quic::QuicResumptionState>();
  state2->tls_session = MakeTestSession(clock->Now(), 3 * kTimeout);
  ;
  state2->application_state.push_back('b');
  quic::QuicServerId id2("b.com", 443);

  cache.Insert(id1, std::move(state1));
  cache.Insert(id2, std::move(state2));

  EXPECT_EQ(2u, cache.size());
  // Expire the session.
  clock->Advance(kTimeout * 2);
  // The entry has not been removed yet.
  EXPECT_EQ(2u, cache.size());

  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(1u, cache.size());
  EXPECT_EQ('b', cache.Lookup(id2, ssl_ctx_.get())->application_state.front());
  EXPECT_EQ(0u, cache.size());
}

TEST_F(QuicClientSessionCacheTest, FlushOnMemoryNotifications) {
  base::test::TaskEnvironment task_environment;
  const base::TimeDelta kTimeout = base::TimeDelta::FromSeconds(1000);
  QuicClientSessionCache cache;
  std::unique_ptr<base::SimpleTestClock> clock = MakeTestClock();
  cache.SetClockForTesting(clock.get());

  std::unique_ptr<quic::QuicResumptionState> state1 =
      std::make_unique<quic::QuicResumptionState>();
  state1->tls_session = MakeTestSession(clock->Now(), kTimeout);
  quic::QuicServerId id1("a.com", 443);

  std::unique_ptr<quic::QuicResumptionState> state2 =
      std::make_unique<quic::QuicResumptionState>();
  state2->tls_session = MakeTestSession(clock->Now(), 3 * kTimeout);
  ;
  state2->application_state.push_back('b');
  quic::QuicServerId id2("b.com", 443);

  cache.Insert(id1, std::move(state1));
  cache.Insert(id2, std::move(state2));

  EXPECT_EQ(2u, cache.size());
  // Expire the session.
  clock->Advance(kTimeout * 2);
  // The entry has not been removed yet.
  EXPECT_EQ(2u, cache.size());

  // Fire a notification that will flush expired sessions.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE);
  base::RunLoop().RunUntilIdle();

  // session1 is expired and should be flushed.
  EXPECT_EQ(nullptr, cache.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(1u, cache.size());

  // Fire notification that will flush everything.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, cache.size());
}

}  // namespace net