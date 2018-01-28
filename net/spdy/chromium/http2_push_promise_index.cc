// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/http2_push_promise_index.h"

#include <utility>

#include "net/spdy/chromium/spdy_session.h"

namespace net {

Http2PushPromiseIndex::Http2PushPromiseIndex() {}

Http2PushPromiseIndex::~Http2PushPromiseIndex() {
  DCHECK(unclaimed_pushed_streams_.empty());
}

base::WeakPtr<SpdySession> Http2PushPromiseIndex::Find(
    const SpdySessionKey& key,
    const GURL& url) const {
  DCHECK(!url.is_empty());

  UnclaimedPushedStreamMap::const_iterator url_it =
      unclaimed_pushed_streams_.find(url);

  if (url_it == unclaimed_pushed_streams_.end()) {
    return base::WeakPtr<SpdySession>();
  }

  DCHECK(url.SchemeIsCryptographic());
  for (const auto& session : url_it->second) {
    const SpdySessionKey& spdy_session_key = session->spdy_session_key();
    if (spdy_session_key.proxy_server() != key.proxy_server() ||
        spdy_session_key.privacy_mode() != key.privacy_mode()) {
      continue;
    }
    if (!session->VerifyDomainAuthentication(key.host_port_pair().host())) {
      continue;
    }
    return session;
  }

  return base::WeakPtr<SpdySession>();
}

void Http2PushPromiseIndex::RegisterUnclaimedPushedStream(
    const GURL& url,
    base::WeakPtr<SpdySession> spdy_session) {
  DCHECK(!url.is_empty());
  DCHECK(url.SchemeIsCryptographic());

  // Use lower_bound() so that if key does not exists, then insertion can use
  // its return value as a hint.
  UnclaimedPushedStreamMap::iterator url_it =
      unclaimed_pushed_streams_.lower_bound(url);
  if (url_it == unclaimed_pushed_streams_.end() || url_it->first != url) {
    WeakSessionList list;
    list.push_back(std::move(spdy_session));
    UnclaimedPushedStreamMap::value_type value(url, std::move(list));
    unclaimed_pushed_streams_.insert(url_it, std::move(value));
    return;
  }
  url_it->second.push_back(spdy_session);
}

void Http2PushPromiseIndex::UnregisterUnclaimedPushedStream(
    const GURL& url,
    SpdySession* spdy_session) {
  DCHECK(!url.is_empty());
  DCHECK(url.SchemeIsCryptographic());

  UnclaimedPushedStreamMap::iterator url_it =
      unclaimed_pushed_streams_.find(url);
  if (url_it == unclaimed_pushed_streams_.end()) {
    NOTREACHED();
    return;
  }
  for (WeakSessionList::iterator it = url_it->second.begin();
       it != url_it->second.end();) {
    if (it->get() == spdy_session) {
      url_it->second.erase(it);
      if (url_it->second.empty()) {
        unclaimed_pushed_streams_.erase(url_it);
      }
      return;
    }
    ++it;
  }
  NOTREACHED();
}

}  // namespace net
