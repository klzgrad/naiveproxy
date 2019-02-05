// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brought to you by number 42.

#ifndef NET_COOKIES_COOKIE_STORE_H_
#define NET_COOKIES_COOKIE_STORE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_deletion_info.h"
#include "net/cookies/cookie_options.h"

class GURL;

namespace base {
namespace trace_event {
class ProcessMemoryDump;
}
}  // namespace base

namespace net {

class CookieChangeDispatcher;

// An interface for storing and retrieving cookies. Implementations are not
// thread safe, as with most other net classes. All methods must be invoked on
// the network thread, and all callbacks will be called there.
//
// All async functions may either invoke the callback asynchronously, or they
// may be invoked immediately (prior to return of the asynchronous function).
// Destroying the CookieStore will cancel pending async callbacks.
class NET_EXPORT CookieStore {
 public:
  // Callback definitions.
  typedef base::OnceCallback<void(const CookieList& cookies)>
      GetCookieListCallback;
  typedef base::OnceCallback<void(bool success)> SetCookiesCallback;
  typedef base::OnceCallback<void(uint32_t num_deleted)> DeleteCallback;

  virtual ~CookieStore();

  // Sets the cookies specified by |cookie_list| returned from |url|
  // with options |options| in effect.  Expects a cookie line, like
  // "a=1; domain=b.com".
  //
  // Fails either if the cookie is invalid or if this is a non-HTTPONLY cookie
  // and it would overwrite an existing HTTPONLY cookie.
  // Returns true if the cookie is successfully set.
  virtual void SetCookieWithOptionsAsync(const GURL& url,
                                         const std::string& cookie_line,
                                         const CookieOptions& options,
                                         SetCookiesCallback callback) = 0;

  // Set the cookie on the cookie store.  |cookie.IsCanonical()| must
  // be true.  |secure_source| indicates if the source of the setting
  // may be considered secure (if from a URL, the scheme is
  // cryptographic), and |modify_http_only| indicates if the source of
  // the setting may modify http_only cookies.  The current time will
  // be used in place of a null creation time.
  virtual void SetCanonicalCookieAsync(std::unique_ptr<CanonicalCookie> cookie,
                                       bool secure_source,
                                       bool modify_http_only,
                                       SetCookiesCallback callback) = 0;

  // Obtains a CookieList for the given |url| and |options|. The returned
  // cookies are passed into |callback|, ordered by longest path, then earliest
  // creation date.
  virtual void GetCookieListWithOptionsAsync(
      const GURL& url,
      const CookieOptions& options,
      GetCookieListCallback callback) = 0;

  // Returns all cookies associated with |url|, including http-only, and
  // same-site cookies. The returned cookies are ordered by longest path, then
  // by earliest creation date, and are not marked as having been accessed.
  //
  // TODO(mkwst): This method is deprecated, and should be removed, either by
  // updating callsites to use 'GetCookieListWithOptionsAsync' with an explicit
  // CookieOptions, or by changing CookieOptions' defaults.
  void GetAllCookiesForURLAsync(const GURL& url,
                                GetCookieListCallback callback);

  // Returns all the cookies, for use in management UI, etc. This does not mark
  // the cookies as having been accessed. The returned cookies are ordered by
  // longest path, then by earliest creation date.
  virtual void GetAllCookiesAsync(GetCookieListCallback callback) = 0;

  // Deletes all cookies that might apply to |url| that have |cookie_name|.
  virtual void DeleteCookieAsync(const GURL& url,
                                 const std::string& cookie_name,
                                 base::OnceClosure callback) = 0;

  // Deletes one specific cookie. |cookie| must have been returned by a previous
  // query on this CookieStore. Invokes |callback| with 1 if a cookie was
  // deleted, 0 otherwise.
  virtual void DeleteCanonicalCookieAsync(const CanonicalCookie& cookie,
                                          DeleteCallback callback) = 0;

  // Deletes all of the cookies that have a creation_date matching
  // |creation_range|. See CookieDeletionInfo::TimeRange::Matches().
  // Calls |callback| with the number of cookies deleted.
  virtual void DeleteAllCreatedInTimeRangeAsync(
      const CookieDeletionInfo::TimeRange& creation_range,
      DeleteCallback callback) = 0;

  // Deletes all of the cookies matching |delete_info|. This includes all
  // http_only and secure cookies. Avoid deleting cookies that could leave
  // websites with a partial set of visible cookies.
  // Calls |callback| with the number of cookies deleted.
  virtual void DeleteAllMatchingInfoAsync(CookieDeletionInfo delete_info,
                                          DeleteCallback callback) = 0;

  virtual void DeleteSessionCookiesAsync(DeleteCallback) = 0;

  // Deletes all cookies in the store.
  void DeleteAllAsync(DeleteCallback callback);

  // Flush the backing store (if any) to disk and post the given callback when
  // done.
  virtual void FlushStore(base::OnceClosure callback) = 0;

  // Protects session cookies from deletion on shutdown, if the underlying
  // CookieStore implemention is currently configured to store them to disk.
  // Otherwise, does nothing.
  virtual void SetForceKeepSessionState();

  // The interface used to observe changes to this CookieStore's contents.
  virtual CookieChangeDispatcher& GetChangeDispatcher() = 0;

  // Returns true if this cookie store is ephemeral, and false if it is backed
  // by some sort of persistence layer.
  // TODO(nharper): Remove this method once crbug.com/548423 has been closed.
  virtual bool IsEphemeral() = 0;
  void SetChannelIDServiceID(int id);
  int GetChannelIDServiceID();

  // Reports the estimate of dynamically allocated memory in bytes.
  virtual void DumpMemoryStats(base::trace_event::ProcessMemoryDump* pmd,
                               const std::string& parent_absolute_name) const;

 protected:
  CookieStore();
  int channel_id_service_id_;
};

}  // namespace net

#endif  // NET_COOKIES_COOKIE_STORE_H_
