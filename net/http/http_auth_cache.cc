// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_cache.h"

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"

namespace {

// Helper to find the containing directory of path. In RFC 2617 this is what
// they call the "last symbolic element in the absolute path".
// Examples:
//   "/foo/bar.txt" --> "/foo/"
//   "/foo/" --> "/foo/"
std::string GetParentDirectory(const std::string& path) {
  std::string::size_type last_slash = path.rfind("/");
  if (last_slash == std::string::npos) {
    // No slash (absolute paths always start with slash, so this must be
    // the proxy case which uses empty string).
    DCHECK(path.empty());
    return path;
  }
  return path.substr(0, last_slash + 1);
}

// Debug helper to check that |path| arguments are properly formed.
// (should be absolute path, or empty string).
void CheckPathIsValid(const std::string& path) {
  DCHECK(path.empty() || path[0] == '/');
}

// Return true if |path| is a subpath of |container|. In other words, is
// |container| an ancestor of |path|?
bool IsEnclosingPath(const std::string& container, const std::string& path) {
  DCHECK(container.empty() || *(container.end() - 1) == '/');
  return ((container.empty() && path.empty()) ||
          (!container.empty() &&
           base::StartsWith(path, container, base::CompareCase::SENSITIVE)));
}

// Debug helper to check that |origin| arguments are properly formed.
void CheckOriginIsValid(const GURL& origin) {
  DCHECK(origin.is_valid());
  // Note that the scheme may be FTP when we're using a HTTP proxy.
  DCHECK(origin.SchemeIsHTTPOrHTTPS() || origin.SchemeIs("ftp") ||
         origin.SchemeIsWSOrWSS());
  DCHECK(origin.GetOrigin() == origin);
}

// Functor used by EraseIf.
struct IsEnclosedBy {
  explicit IsEnclosedBy(const std::string& path) : path(path) { }
  bool operator() (const std::string& x) const {
    return IsEnclosingPath(path, x);
  }
  const std::string& path;
};

void RecordLookupPosition(int position) {
  UMA_HISTOGRAM_COUNTS_100("Net.HttpAuthCacheLookupPosition", position);
}

void RecordLookupByPathPosition(int position) {
  UMA_HISTOGRAM_COUNTS_100("Net.HttpAuthCacheLookupByPathPosition", position);
}

}  // namespace

namespace net {

HttpAuthCache::HttpAuthCache() = default;

HttpAuthCache::~HttpAuthCache() = default;

// Performance: O(n), where n is the number of realm entries.
HttpAuthCache::Entry* HttpAuthCache::Lookup(const GURL& origin,
                                            const std::string& realm,
                                            HttpAuth::Scheme scheme) {
  CheckOriginIsValid(origin);

  int entries_examined = 0;
  // Linear scan through the realm entries.
  for (EntryList::iterator it = entries_.begin(); it != entries_.end(); ++it) {
    ++entries_examined;
    if (it->origin() == origin && it->realm() == realm &&
        it->scheme() == scheme) {
      it->last_use_time_ = base::TimeTicks::Now();
      RecordLookupPosition(entries_examined);
      return &(*it);
    }
  }
  RecordLookupPosition(0);
  return NULL;  // No realm entry found.
}

// Performance: O(n*m), where n is the number of realm entries, m is the number
// of path entries per realm. Both n amd m are expected to be small; m is
// kept small because AddPath() only keeps the shallowest entry.
HttpAuthCache::Entry* HttpAuthCache::LookupByPath(const GURL& origin,
                                                  const std::string& path) {
  HttpAuthCache::Entry* best_match = NULL;
  size_t best_match_length = 0;
  int best_match_position = 0;
  CheckOriginIsValid(origin);
  CheckPathIsValid(path);

  // RFC 2617 section 2:
  // A client SHOULD assume that all paths at or deeper than the depth of
  // the last symbolic element in the path field of the Request-URI also are
  // within the protection space ...
  std::string parent_dir = GetParentDirectory(path);

  int entries_examined = 0;
  // Linear scan through the realm entries.
  for (EntryList::iterator it = entries_.begin(); it != entries_.end(); ++it) {
    ++entries_examined;
    size_t len = 0;
    if (it->origin() == origin && it->HasEnclosingPath(parent_dir, &len) &&
        (!best_match || len > best_match_length)) {
      best_match = &(*it);
      best_match_length = len;
      best_match_position = entries_examined;
    }
  }
  if (best_match)
    best_match->last_use_time_ = base::TimeTicks::Now();
  RecordLookupByPathPosition(best_match_position);
  return best_match;
}

HttpAuthCache::Entry* HttpAuthCache::Add(const GURL& origin,
                                         const std::string& realm,
                                         HttpAuth::Scheme scheme,
                                         const std::string& auth_challenge,
                                         const AuthCredentials& credentials,
                                         const std::string& path) {
  CheckOriginIsValid(origin);
  CheckPathIsValid(path);

  base::TimeTicks now = base::TimeTicks::Now();

  // Check for existing entry (we will re-use it if present).
  HttpAuthCache::Entry* entry = Lookup(origin, realm, scheme);
  if (!entry) {
    bool evicted = false;
    // Failsafe to prevent unbounded memory growth of the cache.
    if (entries_.size() >= kMaxNumRealmEntries) {
      LOG(WARNING) << "Num auth cache entries reached limit -- evicting";
      UMA_HISTOGRAM_LONG_TIMES("Net.HttpAuthCacheAddEvictedCreation",
          now - entries_.back().creation_time_);
      UMA_HISTOGRAM_LONG_TIMES("Net.HttpAuthCacheAddEvictedLastUse",
          now - entries_.back().last_use_time_);
      entries_.pop_back();
      evicted = true;
    }
    UMA_HISTOGRAM_BOOLEAN("Net.HttpAuthCacheAddEvicted", evicted);

    entries_.push_front(Entry());
    entry = &entries_.front();
    entry->origin_ = origin;
    entry->realm_ = realm;
    entry->scheme_ = scheme;
    entry->creation_time_ = now;
  }
  DCHECK_EQ(origin, entry->origin_);
  DCHECK_EQ(realm, entry->realm_);
  DCHECK_EQ(scheme, entry->scheme_);

  entry->auth_challenge_ = auth_challenge;
  entry->credentials_ = credentials;
  entry->nonce_count_ = 1;
  entry->AddPath(path);
  entry->last_use_time_ = now;

  return entry;
}

HttpAuthCache::Entry::Entry(const Entry& other) = default;

HttpAuthCache::Entry::~Entry() = default;

void HttpAuthCache::Entry::UpdateStaleChallenge(
    const std::string& auth_challenge) {
  auth_challenge_ = auth_challenge;
  nonce_count_ = 1;
}

HttpAuthCache::Entry::Entry()
    : scheme_(HttpAuth::AUTH_SCHEME_MAX),
      nonce_count_(0) {
}

void HttpAuthCache::Entry::AddPath(const std::string& path) {
  std::string parent_dir = GetParentDirectory(path);
  if (!HasEnclosingPath(parent_dir, NULL)) {
    // Remove any entries that have been subsumed by the new entry.
    base::EraseIf(paths_, IsEnclosedBy(parent_dir));

    bool evicted = false;
    // Failsafe to prevent unbounded memory growth of the cache.
    if (paths_.size() >= kMaxNumPathsPerRealmEntry) {
      LOG(WARNING) << "Num path entries for " << origin()
                   << " has grown too large -- evicting";
      paths_.pop_back();
      evicted = true;
    }
    UMA_HISTOGRAM_BOOLEAN("Net.HttpAuthCacheAddPathEvicted", evicted);

    // Add new path.
    paths_.push_front(parent_dir);
  }
}

bool HttpAuthCache::Entry::HasEnclosingPath(const std::string& dir,
                                            size_t* path_len) {
  DCHECK(GetParentDirectory(dir) == dir);
  for (PathList::const_iterator it = paths_.begin(); it != paths_.end();
       ++it) {
    if (IsEnclosingPath(*it, dir)) {
      // No element of paths_ may enclose any other element.
      // Therefore this path is the tightest bound.  Important because
      // the length returned is used to determine the cache entry that
      // has the closest enclosing path in LookupByPath().
      if (path_len)
        *path_len = it->length();
      return true;
    }
  }
  return false;
}

bool HttpAuthCache::Remove(const GURL& origin,
                           const std::string& realm,
                           HttpAuth::Scheme scheme,
                           const AuthCredentials& credentials) {
  for (EntryList::iterator it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->origin() == origin && it->realm() == realm &&
        it->scheme() == scheme) {
      if (credentials.Equals(it->credentials())) {
        entries_.erase(it);
        return true;
      }
      return false;
    }
  }
  return false;
}

void HttpAuthCache::ClearEntriesAddedWithin(base::TimeDelta duration) {
  base::TimeTicks begin_time = base::TimeTicks::Now() - duration;
  base::EraseIf(entries_, [begin_time](const Entry& entry) {
    return entry.creation_time_ >= begin_time;
  });
}

bool HttpAuthCache::UpdateStaleChallenge(const GURL& origin,
                                         const std::string& realm,
                                         HttpAuth::Scheme scheme,
                                         const std::string& auth_challenge) {
  HttpAuthCache::Entry* entry = Lookup(origin, realm, scheme);
  if (!entry)
    return false;
  entry->UpdateStaleChallenge(auth_challenge);
  entry->last_use_time_ = base::TimeTicks::Now();
  return true;
}

void HttpAuthCache::UpdateAllFrom(const HttpAuthCache& other) {
  for (EntryList::const_iterator it = other.entries_.begin();
       it != other.entries_.end(); ++it) {
    // Add an Entry with one of the original entry's paths.
    DCHECK(it->paths_.size() > 0);
    Entry* entry = Add(it->origin(), it->realm(), it->scheme(),
                       it->auth_challenge(), it->credentials(),
                       it->paths_.back());
    // Copy all other paths.
    for (Entry::PathList::const_reverse_iterator it2 = ++it->paths_.rbegin();
         it2 != it->paths_.rend(); ++it2)
      entry->AddPath(*it2);
    // Copy nonce count (for digest authentication).
    entry->nonce_count_ = it->nonce_count_;
  }
}

}  // namespace net
