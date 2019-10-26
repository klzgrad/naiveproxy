// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FTP_FTP_AUTH_CACHE_H_
#define NET_FTP_FTP_AUTH_CACHE_H_

#include <stddef.h>

#include <list>

#include "net/base/auth.h"
#include "net/base/net_export.h"
#include "url/gurl.h"

namespace net {

// The FtpAuthCache class is a simple cache structure to store authentication
// information for ftp. Provides lookup, insertion, and deletion of entries.
// The parameter for doing lookups, insertions, and deletions is a GURL of the
// server's address (not a full URL with path, since FTP auth isn't per path).
// For example:
//   GURL("ftp://myserver") -- OK (implied port of 21)
//   GURL("ftp://myserver:21") -- OK
//   GURL("ftp://myserver/PATH") -- WRONG, paths not allowed
class NET_EXPORT_PRIVATE FtpAuthCache {
 public:
  // Maximum number of entries we allow in the cache.
  static const size_t kMaxEntries;

  struct Entry {
    Entry(const GURL& origin, const AuthCredentials& credentials);
    ~Entry();

    GURL origin;
    AuthCredentials credentials;
  };

  FtpAuthCache();
  ~FtpAuthCache();

  // Return Entry corresponding to given |origin| or NULL if not found.
  Entry* Lookup(const GURL& origin);

  // Add an entry for |origin| to the cache using |credentials|. If there is
  // already an entry for |origin|, it will be overwritten.
  void Add(const GURL& origin, const AuthCredentials& credentials);

  // Remove the entry for |origin| from the cache, if one exists and matches
  // |credentials|.
  void Remove(const GURL& origin, const AuthCredentials& credentials);

 private:
  typedef std::list<Entry> EntryList;

  // Internal representation of cache, an STL list. This makes lookups O(n),
  // but we expect n to be very low.
  EntryList entries_;
};

}  // namespace net

#endif  // NET_FTP_FTP_AUTH_CACHE_H_
