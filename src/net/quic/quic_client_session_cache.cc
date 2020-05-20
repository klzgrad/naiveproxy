// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_client_session_cache.h"

#include "base/time/clock.h"
#include "base/time/default_clock.h"

namespace net {

namespace {

const size_t kDefaultMaxEntries = 1024;
// Check whether the SSL session inside |state| is expired at |now|.
bool IsExpired(quic::QuicResumptionState* state, time_t now) {
  if (now < 0)
    return true;

  SSL_SESSION* session = state->tls_session.get();
  uint64_t now_u64 = static_cast<uint64_t>(now);

  // now_u64 may be slightly behind because of differences in how
  // time is calculated at this layer versus BoringSSL.
  // Add a second of wiggle room to account for this.
  return now_u64 < SSL_SESSION_get_time(session) - 1 ||
         now_u64 >=
             SSL_SESSION_get_time(session) + SSL_SESSION_get_timeout(session);
}

}  // namespace

QuicClientSessionCache::QuicClientSessionCache()
    : QuicClientSessionCache(kDefaultMaxEntries) {}

QuicClientSessionCache::QuicClientSessionCache(size_t max_entries)
    : clock_(base::DefaultClock::GetInstance()), cache_(max_entries) {
  memory_pressure_listener_.reset(
      new base::MemoryPressureListener(base::BindRepeating(
          &QuicClientSessionCache::OnMemoryPressure, base::Unretained(this))));
}

QuicClientSessionCache::~QuicClientSessionCache() {
  Flush();
}

void QuicClientSessionCache::Insert(
    const quic::QuicServerId& server_id,
    std::unique_ptr<quic::QuicResumptionState> state) {
  cache_.Put(server_id, std::move(state));
}

std::unique_ptr<quic::QuicResumptionState> QuicClientSessionCache::Lookup(
    const quic::QuicServerId& server_id,
    const SSL_CTX* /*ctx*/) {
  auto iter = cache_.Get(server_id);
  if (iter == cache_.end())
    return nullptr;

  time_t now = clock_->Now().ToTimeT();
  std::unique_ptr<quic::QuicResumptionState> state = std::move(iter->second);
  cache_.Erase(iter);
  if (IsExpired(state.get(), now))
    state = nullptr;
  return state;
}

void QuicClientSessionCache::FlushExpiredStates() {
  time_t now = clock_->Now().ToTimeT();
  auto iter = cache_.begin();
  while (iter != cache_.end()) {
    if (IsExpired(iter->second.get(), now)) {
      iter = cache_.Erase(iter);
    } else {
      ++iter;
    }
  }
}

void QuicClientSessionCache::OnMemoryPressure(
    base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level) {
  switch (memory_pressure_level) {
    case base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE:
      break;
    case base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE:
      FlushExpiredStates();
      break;
    case base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL:
      Flush();
      break;
  }
}

}  // namespace net
