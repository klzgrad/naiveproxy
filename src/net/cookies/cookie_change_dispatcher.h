// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_COOKIES_COOKIE_CHANGE_DISPATCHER_H_
#define NET_COOKIES_COOKIE_CHANGE_DISPATCHER_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "net/base/net_export.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"

class GURL;

namespace net {

class CanonicalCookie;

// The publicly relevant reasons a cookie might be changed.
enum class CookieChangeCause {
  // The cookie was inserted.
  INSERTED,
  // The cookie was deleted directly by a consumer's action.
  EXPLICIT,
  // The cookie was deleted, but no more details are known.
  UNKNOWN_DELETION,
  // The cookie was automatically removed due to an insert operation that
  // overwrote it.
  OVERWRITE,
  // The cookie was automatically removed as it expired.
  EXPIRED,
  // The cookie was automatically evicted during garbage collection.
  EVICTED,
  // The cookie was overwritten with an already-expired expiration date.
  EXPIRED_OVERWRITE
};

struct NET_EXPORT CookieChangeInfo {
  CookieChangeInfo();
  CookieChangeInfo(const CanonicalCookie& cookie,
                   CookieAccessResult access_result,
                   CookieChangeCause cause);
  ~CookieChangeInfo();

  // The cookie that changed.
  CanonicalCookie cookie;

  // The access result of the cookie at the time of the change.
  CookieAccessResult access_result;

  // The reason for the change.
  CookieChangeCause cause = CookieChangeCause::EXPLICIT;
};

// Return a string corresponding to the change cause.  For debugging/logging.
NET_EXPORT const char* CookieChangeCauseToString(CookieChangeCause cause);

// Returns whether |cause| is one that could be a reason for deleting a cookie.
// This function assumes that ChangeCause::EXPLICIT is a reason for deletion.
NET_EXPORT bool CookieChangeCauseIsDeletion(CookieChangeCause cause);

// Called when a cookie is changed in a CookieStore.
//
// Receives the CanonicalCookie which was added to or removed from the store,
// the CookieAccessSemantics of the cookie at the time of the change event,
// and a CookieStore::ChangeCause indicating if the cookie was added, updated,
// or removed.
//
// Note that the callback is called twice when a cookie is updated: the first
// call communicates the removal of the existing cookie, and the second call
// expresses the addition of the new cookie.
//
// The callback must not synchronously modify any cookie in the CookieStore
// whose change it is observing.
using CookieChangeCallback =
    base::RepeatingCallback<void(const CookieChangeInfo&)>;

// Records a listener's interest in CookieStore changes.
//
// Each call to CookieChangeDispatcher::Add*() is a listener expressing an
// interest in observing CookieStore changes. Each call creates a
// CookieChangeSubscription instance whose ownership is passed to the listener.
//
// When the listener's interest disappears (usually at destruction time), the
// listener must communicate this by destroying the CookieChangeSubscription
// instance. The callback passed to the Add*() method will not to be called
// after the returned handle is destroyed.
//
// CookieChangeSubscription instances do not keep the observed CookieStores
// alive.
//
// Instances of this class are not thread-safe, and must be destroyed on the
// same thread that they were obtained on.
class CookieChangeSubscription {
 public:
  CookieChangeSubscription() = default;

  CookieChangeSubscription(const CookieChangeSubscription&) = delete;
  CookieChangeSubscription& operator=(const CookieChangeSubscription&) = delete;

  virtual ~CookieChangeSubscription() = default;
};

// Exposes changes to a CookieStore's contents.
//
// A component that wishes to react to changes in a CookieStore (the listener)
// must register its interest (subscribe) by calling one of the Add*() methods
// exposed by this interface.
//
// CookieChangeDispatch instances are intended to be embedded in CookieStore
// implementations, so they are not intended to be created as standalone objects
// on the heap.
//
// At the time of this writing (Q1 2018), using this interface has non-trivial
// performance implications on all implementations. This issue should be fixed
// by the end of 2018, at which point this warning should go away. Until then,
// please understand and reason about the performance impact of your change if
// you're adding uses of this to the codebase.
class CookieChangeDispatcher {
 public:
  CookieChangeDispatcher() = default;

  CookieChangeDispatcher(const CookieChangeDispatcher&) = delete;
  CookieChangeDispatcher& operator=(const CookieChangeDispatcher&) = delete;

  virtual ~CookieChangeDispatcher() = default;

  // Observe changes to all cookies named `name` that would be sent in a
  // request to `url`.
  //
  // If `cookie_partition_key` is nullopt, then we ignore all change events for
  // partitioned cookies. Otherwise it only subscribes to change events for
  // partitioned cookies with the same provided key.
  // Unpartitioned cookies are not affected by the `cookie_partition_key`
  // parameter.
  [[nodiscard]] virtual std::unique_ptr<CookieChangeSubscription>
  AddCallbackForCookie(
      const GURL& url,
      const std::string& name,
      const std::optional<CookiePartitionKey>& cookie_partition_key,
      CookieChangeCallback callback) = 0;

  // Observe changes to the cookies that would be sent for a request to `url`.
  //
  // If `cookie_partition_key` is nullopt, then we ignore all change events for
  // partitioned cookies. Otherwise it only subscribes to change events for
  // partitioned cookies with the same provided key.
  // Unpartitioned cookies are not affected by the `cookie_partition_key`
  // parameter.
  [[nodiscard]] virtual std::unique_ptr<CookieChangeSubscription>
  AddCallbackForUrl(
      const GURL& url,
      const std::optional<CookiePartitionKey>& cookie_partition_key,
      CookieChangeCallback callback) = 0;

  // Observe all the CookieStore's changes.
  //
  // The callback will not observe a few bookkeeping changes.
  // See kChangeCauseMapping in cookie_monster.cc for details.
  // TODO(crbug.com/40188414): Add support for Partitioned cookies.
  [[nodiscard]] virtual std::unique_ptr<CookieChangeSubscription>
  AddCallbackForAllChanges(CookieChangeCallback callback) = 0;
};

}  // namespace net

#endif  // NET_COOKIES_COOKIE_CHANGE_DISPATCHER_H_
