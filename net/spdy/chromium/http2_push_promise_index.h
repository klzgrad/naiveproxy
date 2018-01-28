// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_HTTP2_PUSH_PROMISE_INDEX_H_
#define NET_SPDY_CHROMIUM_HTTP2_PUSH_PROMISE_INDEX_H_

#include <map>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/net_export.h"
#include "net/spdy/chromium/spdy_session_key.h"
#include "url/gurl.h"

namespace net {

class SpdySession;

// This class manages cross-origin pushed streams from the receipt of
// PUSH_PROMISE frame until they are matched to a request.  Each SpdySessionPool
// owns one instance of this class, which then allows requests to be matched
// with a pushed stream regardless of which HTTP/2 connection the stream is on
// on.  Only pushed streams with cryptographic schemes (for example, https) are
// allowed to be shared across connections.  Non-cryptographic scheme pushes
// (for example, http) are fully managed within each SpdySession.
class NET_EXPORT Http2PushPromiseIndex {
 public:
  Http2PushPromiseIndex();
  ~Http2PushPromiseIndex();

  // Returns a session with |key| that has an unclaimed push stream for |url| if
  // such exists.  Returns nullptr otherwise.
  base::WeakPtr<SpdySession> Find(const SpdySessionKey& key, const GURL& url);

  // (Un)registers a SpdySession with an unclaimed pushed stream for |url|.
  void RegisterUnclaimedPushedStream(const GURL& url,
                                     base::WeakPtr<SpdySession> spdy_session);
  void UnregisterUnclaimedPushedStream(const GURL& url,
                                       SpdySession* spdy_session);

 private:
  typedef std::vector<base::WeakPtr<SpdySession>> WeakSessionList;
  typedef std::map<GURL, WeakSessionList> UnclaimedPushedStreamMap;

  // A map of all SpdySessions owned by |this| that have an unclaimed pushed
  // streams for a GURL.  Might contain invalid WeakPtr's.
  // A single SpdySession can only have at most one pushed stream for each GURL,
  // but it is possible that multiple SpdySessions have pushed streams for the
  // same GURL.
  UnclaimedPushedStreamMap unclaimed_pushed_streams_;

  DISALLOW_COPY_AND_ASSIGN(Http2PushPromiseIndex);
};

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_HTTP2_PUSH_PROMISE_INDEX_H_
