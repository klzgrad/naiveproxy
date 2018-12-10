// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_ORIGIN_H_
#define URL_ORIGIN_H_

#include <stdint.h>

#include <string>

#include "base/debug/alias.h"
#include "base/optional.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/unguessable_token.h"
#include "url/scheme_host_port.h"
#include "url/third_party/mozilla/url_parse.h"
#include "url/url_canon.h"
#include "url/url_constants.h"
#include "url/url_export.h"

class GURL;

namespace url {

// Per https://html.spec.whatwg.org/multipage/origin.html#origin, an origin is
// either:
// - a tuple origin of (scheme, host, port) as described in RFC 6454.
// - an opaque origin with an internal value
//
// TL;DR: If you need to make a security-relevant decision, use 'url::Origin'.
// If you only need to extract the bits of a URL which are relevant for a
// network connection, use 'url::SchemeHostPort'.
//
// STL;SDR: If you aren't making actual network connections, use 'url::Origin'.
//
// This class ought to be used when code needs to determine if two resources
// are "same-origin", and when a canonical serialization of an origin is
// required. Note that the canonical serialization of an origin *must not* be
// used to determine if two resources are same-origin.
//
// A tuple origin, like 'SchemeHostPort', is composed of a tuple of (scheme,
// host, port), but contains a number of additional concepts which make it
// appropriate for use as a security boundary and access control mechanism
// between contexts. Two tuple origins are same-origin if the tuples are equal.
// A tuple origin may also be re-created from its serialization.
//
// An opaque origin is cross-origin to any origin, including itself and copies
// of itself. Unlike tuple origins, an opaque origin cannot be re-created from
// its serialization, which is always the string "null".
//
// TODO(https://crbug.com/768460): work is in progress to associate an internal
// globally unique identifier with an opaque origin: completing this work will
// allow a copy of an opaque origin to be same-origin to the original instance
// of that opaque origin.
//
// IMPORTANT: Since opaque origins always serialize as the string "null", it is
// *never* safe to use the serialization for security checks!
//
// A tuple origin and an opaque origin are never same-origin.
//
// There are a few subtleties to note:
//
// * A default constructed Origin is opaque, but unlike the spec definition, has
//   no associated identifier. A default constructed Origin is cross-origin to
//   every other Origin object.
//
// * Invalid and non-standard GURLs are parsed as opaque origins. This includes
//   non-hierarchical URLs like 'data:text/html,...' and 'javascript:alert(1)'.
//
// * GURLs with schemes of 'filesystem' or 'blob' parse the origin out of the
//   internals of the URL. That is, 'filesystem:https://example.com/temporary/f'
//   is parsed as ('https', 'example.com', 443).
//
// * GURLs with a 'file' scheme are tricky. They are parsed as ('file', '', 0),
//   but their behavior may differ from embedder to embedder.
//   TODO(dcheng): This behavior is not consistent with Blink's notion of file
//   URLs, which always creates an opaque origin.
//
// * The host component of an IPv6 address includes brackets, just like the URL
//   representation.
//
// Usage:
//
// * Origins are generally constructed from an already-canonicalized GURL:
//
//     GURL url("https://example.com/");
//     url::Origin origin = Origin::Create(url);
//     origin.scheme(); // "https"
//     origin.host(); // "example.com"
//     origin.port(); // 443
//     origin.unique(); // false
//
// * To answer the question "Are |this| and |that| "same-origin" with each
//   other?", use |Origin::IsSameOriginWith|:
//
//     if (this.IsSameOriginWith(that)) {
//       // Amazingness goes here.
//     }
class URL_EXPORT Origin {
 public:
  // Creates an opaque and always unique Origin. The returned Origin is
  // always cross-origin to any Origin, including itself.
  Origin();

  // Creates an Origin from |url|, as described at
  // https://url.spec.whatwg.org/#origin, with the following additions:
  //
  // 1. If |url| is invalid or non-standard, an opaque Origin is constructed.
  // 2. 'filesystem' URLs behave as 'blob' URLs (that is, the origin is parsed
  //    out of everything in the URL which follows the scheme).
  // 3. 'file' URLs all parse as ("file", "", 0).
  //
  // If this method returns an opaque Origin, the returned Origin will be
  // cross-origin to any Origin, including itself.
  static Origin Create(const GURL& url);

  // Copyable and movable.
  Origin(const Origin&);
  Origin& operator=(const Origin&);
  Origin(Origin&&);
  Origin& operator=(Origin&&);

  // Creates an Origin from a |scheme|, |host|, and |port|. All the parameters
  // must be valid and canonicalized. Do not use this method to create opaque
  // origins. Use Origin() or Origin::CreateOpaque() for that.
  //
  // This constructor should be used in order to pass 'Origin' objects back and
  // forth over IPC (as transitioning through GURL would risk potentially
  // dangerous recanonicalization); other potential callers should prefer the
  // 'GURL'-based constructor.
  static Origin UnsafelyCreateOriginWithoutNormalization(
      base::StringPiece scheme,
      base::StringPiece host,
      uint16_t port);

  // Creates an origin without sanity checking that the host is canonicalized.
  // This should only be used when converting between already normalized types,
  // and should NOT be used for IPC. Method takes std::strings for use with move
  // operators to avoid copies.
  static Origin CreateFromNormalizedTuple(std::string scheme,
                                          std::string host,
                                          uint16_t port);

  ~Origin();

  // For opaque origins, these return ("", "", 0).
  const std::string& scheme() const {
    return !unique() ? tuple_.scheme() : base::EmptyString();
  }
  const std::string& host() const {
    return !unique() ? tuple_.host() : base::EmptyString();
  }
  uint16_t port() const { return !unique() ? tuple_.port() : 0; }

  // TODO(dcheng): Rename this to opaque().
  bool unique() const { return tuple_.IsInvalid(); }

  // An ASCII serialization of the Origin as per Section 6.2 of RFC 6454, with
  // the addition that all Origins with a 'file' scheme serialize to "file://".
  std::string Serialize() const;

  // Two Origins are "same-origin" if their schemes, hosts, and ports are exact
  // matches; and neither is unique.
  bool IsSameOriginWith(const Origin& other) const;
  bool operator==(const Origin& other) const {
    return IsSameOriginWith(other);
  }

  // Efficiently returns what GURL(Serialize()) would without re-parsing the
  // URL. This can be used for the (rare) times a GURL representation is needed
  // for an Origin.
  // Note: The returned URL will not necessarily be serialized to the same value
  // as the Origin would. The GURL will have an added "/" path for Origins with
  // valid SchemeHostPorts and file Origins.
  //
  // Try not to use this method under normal circumstances, as it loses type
  // information. Downstream consumers can mistake the returned GURL with a full
  // URL (e.g. with a path component).
  GURL GetURL() const;

  // Same as GURL::DomainIs. If |this| origin is unique, then returns false.
  bool DomainIs(base::StringPiece canonical_domain) const;

  // Allows Origin to be used as a key in STL (for example, a std::set or
  // std::map).
  bool operator<(const Origin& other) const;

 private:
  friend class OriginTest;

  // Creates a new opaque origin that is guaranteed to be cross-origin to all
  // currently existing origins. An origin created by this method retains its
  // identity across copies. Copies are guaranteed to be same-origin to each
  // other, e.g.
  //
  //   url::Origin a = Origin::CreateUniqueOpaque();
  //   url::Origin b = Origin::CreateUniqueOpaque();
  //   url::Origin c = a;
  //   url::Origin d = b;
  //
  // |a| and |c| are same-origin, since |c| was copied from |a|. |b| and |d| are
  // same-origin as well, since |d| was copied from |b|. All other combinations
  // of origins are considered cross-origin, e.g. |a| is cross-origin to |b| and
  // |d|, |b| is cross-origin to |a| and |c|, |c| is cross-origin to |b| and
  // |d|, and |d| is cross-origin to |a| and |c|.
  //
  // Note that this is private internal helper, since relatively few locations
  // should be responsible for deriving a canonical origin from a GURL.
  static Origin CreateUniqueOpaque();

  // Similar to Create(const GURL&). However, if the returned Origin is an
  // opaque origin, it will be created with CreateUniqueOpaque(), have an
  // associated identity, and be considered same-origin to copies of itself.
  static Origin CreateCanonical(const GURL&);

  enum class ConstructAsOpaque { kTag };
  explicit Origin(ConstructAsOpaque);

  // |tuple| must be valid, implying that the created Origin is never an opaque
  // origin.
  explicit Origin(SchemeHostPort tuple);

  // Helpers for managing union for destroy, copy, and move.
  // The tuple is used for tuple origins (e.g. https://example.com:80). This
  // is expected to be the common case. |IsInvalid()| will be true for opaque
  // origins.
  SchemeHostPort tuple_;

  // The nonce is used for maintaining identity of an opaque origin. This
  // nonce is preserved when an opaque origin is copied or moved.
  base::Optional<base::UnguessableToken> nonce_;
};

URL_EXPORT std::ostream& operator<<(std::ostream& out, const Origin& origin);

URL_EXPORT bool IsSameOriginWith(const GURL& a, const GURL& b);

// DEBUG_ALIAS_FOR_ORIGIN(var_name, origin) copies |origin| into a new
// stack-allocated variable named |<var_name>|.  This helps ensure that the
// value of |origin| gets preserved in crash dumps.
#define DEBUG_ALIAS_FOR_ORIGIN(var_name, origin) \
  DEBUG_ALIAS_FOR_CSTR(var_name, (origin).Serialize().c_str(), 128)

}  // namespace url

#endif  // URL_ORIGIN_H_
