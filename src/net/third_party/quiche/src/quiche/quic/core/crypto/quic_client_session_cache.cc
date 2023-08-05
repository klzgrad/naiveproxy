// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

#include "quiche/quic/core/quic_clock.h"

namespace quic {

namespace {

const size_t kDefaultMaxEntries = 1024;
// Returns false if the SSL |session| doesn't exist or it is expired at |now|.
bool IsValid(SSL_SESSION* session, uint64_t now) {
  if (!session) return false;

  // now_u64 may be slightly behind because of differences in how
  // time is calculated at this layer versus BoringSSL.
  // Add a second of wiggle room to account for this.
  return !(now + 1 < SSL_SESSION_get_time(session) ||
           now >= SSL_SESSION_get_time(session) +
                      SSL_SESSION_get_timeout(session));
}

bool DoApplicationStatesMatch(const ApplicationState* state,
                              ApplicationState* other) {
  if ((state && !other) || (!state && other)) return false;
  if ((!state && !other) || *state == *other) return true;
  return false;
}

}  // namespace

QuicClientSessionCache::QuicClientSessionCache()
    : QuicClientSessionCache(kDefaultMaxEntries) {}

QuicClientSessionCache::QuicClientSessionCache(size_t max_entries)
    : cache_(max_entries) {}

QuicClientSessionCache::~QuicClientSessionCache() { Clear(); }

void QuicClientSessionCache::Insert(const QuicServerId& server_id,
                                    bssl::UniquePtr<SSL_SESSION> session,
                                    const TransportParameters& params,
                                    const ApplicationState* application_state) {
  QUICHE_DCHECK(session) << "TLS session is not inserted into client cache.";
  auto iter = cache_.Lookup(server_id);
  if (iter == cache_.end()) {
    CreateAndInsertEntry(server_id, std::move(session), params,
                         application_state);
    return;
  }

  QUICHE_DCHECK(iter->second->params);
  // The states are both the same, so only need to insert sessions.
  if (params == *iter->second->params &&
      DoApplicationStatesMatch(application_state,
                               iter->second->application_state.get())) {
    iter->second->PushSession(std::move(session));
    return;
  }
  // Erase the existing entry because this Insert call must come from a
  // different QUIC session.
  cache_.Erase(iter);
  CreateAndInsertEntry(server_id, std::move(session), params,
                       application_state);
}

std::unique_ptr<QuicResumptionState> QuicClientSessionCache::Lookup(
    const QuicServerId& server_id, QuicWallTime now, const SSL_CTX* /*ctx*/) {
  auto iter = cache_.Lookup(server_id);
  if (iter == cache_.end()) return nullptr;

  if (!IsValid(iter->second->PeekSession(), now.ToUNIXSeconds())) {
    QUIC_DLOG(INFO) << "TLS Session expired for host:" << server_id.host();
    cache_.Erase(iter);
    return nullptr;
  }
  auto state = std::make_unique<QuicResumptionState>();
  state->tls_session = iter->second->PopSession();
  if (iter->second->params != nullptr) {
    state->transport_params =
        std::make_unique<TransportParameters>(*iter->second->params);
  }
  if (iter->second->application_state != nullptr) {
    state->application_state =
        std::make_unique<ApplicationState>(*iter->second->application_state);
  }
  if (!iter->second->token.empty()) {
    state->token = iter->second->token;
    // Clear token after use.
    iter->second->token.clear();
  }

  return state;
}

void QuicClientSessionCache::ClearEarlyData(const QuicServerId& server_id) {
  auto iter = cache_.Lookup(server_id);
  if (iter == cache_.end()) return;
  for (auto& session : iter->second->sessions) {
    if (session) {
      QUIC_DLOG(INFO) << "Clear early data for for host: " << server_id.host();
      session.reset(SSL_SESSION_copy_without_early_data(session.get()));
    }
  }
}

void QuicClientSessionCache::OnNewTokenReceived(const QuicServerId& server_id,
                                                absl::string_view token) {
  if (token.empty()) {
    return;
  }
  auto iter = cache_.Lookup(server_id);
  if (iter == cache_.end()) {
    return;
  }
  iter->second->token = std::string(token);
}

void QuicClientSessionCache::RemoveExpiredEntries(QuicWallTime now) {
  auto iter = cache_.begin();
  while (iter != cache_.end()) {
    if (!IsValid(iter->second->PeekSession(), now.ToUNIXSeconds())) {
      iter = cache_.Erase(iter);
    } else {
      ++iter;
    }
  }
}

void QuicClientSessionCache::Clear() { cache_.Clear(); }

void QuicClientSessionCache::CreateAndInsertEntry(
    const QuicServerId& server_id, bssl::UniquePtr<SSL_SESSION> session,
    const TransportParameters& params,
    const ApplicationState* application_state) {
  auto entry = std::make_unique<Entry>();
  entry->PushSession(std::move(session));
  entry->params = std::make_unique<TransportParameters>(params);
  if (application_state) {
    entry->application_state =
        std::make_unique<ApplicationState>(*application_state);
  }
  cache_.Insert(server_id, std::move(entry));
}

QuicClientSessionCache::Entry::Entry() = default;
QuicClientSessionCache::Entry::Entry(Entry&&) = default;
QuicClientSessionCache::Entry::~Entry() = default;

void QuicClientSessionCache::Entry::PushSession(
    bssl::UniquePtr<SSL_SESSION> session) {
  if (sessions[0] != nullptr) {
    sessions[1] = std::move(sessions[0]);
  }
  sessions[0] = std::move(session);
}

bssl::UniquePtr<SSL_SESSION> QuicClientSessionCache::Entry::PopSession() {
  if (sessions[0] == nullptr) return nullptr;
  bssl::UniquePtr<SSL_SESSION> session = std::move(sessions[0]);
  sessions[0] = std::move(sessions[1]);
  sessions[1] = nullptr;
  return session;
}

SSL_SESSION* QuicClientSessionCache::Entry::PeekSession() {
  return sessions[0].get();
}

}  // namespace quic
