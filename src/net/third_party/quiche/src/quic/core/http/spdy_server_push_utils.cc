// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/spdy_server_push_utils.h"

#include "url/gurl.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using spdy::SpdyHeaderBlock;

namespace quic {

// static
std::string SpdyServerPushUtils::GetPromisedUrlFromHeaders(
    const SpdyHeaderBlock& headers) {
  // RFC 7540, Section 8.1.2.3: All HTTP/2 requests MUST include exactly
  // one valid value for the ":method", ":scheme", and ":path" pseudo-header
  // fields, unless it is a CONNECT request.

  // RFC 7540, Section  8.2.1:  The header fields in PUSH_PROMISE and any
  // subsequent CONTINUATION frames MUST be a valid and complete set of request
  // header fields (Section 8.1.2.3).  The server MUST include a method in the
  // ":method" pseudo-header field that is safe and cacheable.
  //
  // RFC 7231, Section  4.2.1: Of the request methods defined by this
  // specification, the GET, HEAD, OPTIONS, and TRACE methods are defined to be
  // safe.
  //
  // RFC 7231, Section  4.2.1: ... this specification defines GET, HEAD, and
  // POST as cacheable, ...
  //
  // So the only methods allowed in a PUSH_PROMISE are GET and HEAD.
  SpdyHeaderBlock::const_iterator it = headers.find(":method");
  if (it == headers.end() || (it->second != "GET" && it->second != "HEAD")) {
    return std::string();
  }

  it = headers.find(":scheme");
  if (it == headers.end() || it->second.empty()) {
    return std::string();
  }
  quiche::QuicheStringPiece scheme = it->second;

  // RFC 7540, Section 8.2: The server MUST include a value in the
  // ":authority" pseudo-header field for which the server is authoritative
  // (see Section 10.1).
  it = headers.find(":authority");
  if (it == headers.end() || it->second.empty()) {
    return std::string();
  }
  quiche::QuicheStringPiece authority = it->second;

  // RFC 7540, Section 8.1.2.3 requires that the ":path" pseudo-header MUST
  // NOT be empty for "http" or "https" URIs;
  //
  // However, to ensure the scheme is consistently canonicalized, that check
  // is deferred to implementations in QuicUrlUtils::GetPushPromiseUrl().
  it = headers.find(":path");
  if (it == headers.end()) {
    return std::string();
  }
  quiche::QuicheStringPiece path = it->second;

  return GetPushPromiseUrl(scheme, authority, path);
}

// static
std::string SpdyServerPushUtils::GetPromisedHostNameFromHeaders(
    const SpdyHeaderBlock& headers) {
  // TODO(fayang): Consider just checking out the value of the ":authority" key
  // in headers.
  return GURL(GetPromisedUrlFromHeaders(headers)).host();
}

// static
bool SpdyServerPushUtils::PromisedUrlIsValid(const SpdyHeaderBlock& headers) {
  std::string url(GetPromisedUrlFromHeaders(headers));
  return !url.empty() && GURL(url).is_valid();
}

// static
std::string SpdyServerPushUtils::GetPushPromiseUrl(
    quiche::QuicheStringPiece scheme,
    quiche::QuicheStringPiece authority,
    quiche::QuicheStringPiece path) {
  // RFC 7540, Section 8.1.2.3: The ":path" pseudo-header field includes the
  // path and query parts of the target URI (the "path-absolute" production
  // and optionally a '?' character followed by the "query" production (see
  // Sections 3.3 and 3.4 of RFC3986). A request in asterisk form includes the
  // value '*' for the ":path" pseudo-header field.
  //
  // This pseudo-header field MUST NOT be empty for "http" or "https" URIs;
  // "http" or "https" URIs that do not contain a path MUST include a value of
  // '/'. The exception to this rule is an OPTIONS request for an "http" or
  // "https" URI that does not include a path component; these MUST include a
  // ":path" pseudo-header with a value of '*' (see RFC7230, Section 5.3.4).
  //
  // In addition to the above restriction from RFC 7540, note that RFC3986
  // defines the "path-absolute" construction as starting with "/" but not "//".
  //
  // RFC 7540, Section  8.2.1:  The header fields in PUSH_PROMISE and any
  // subsequent CONTINUATION frames MUST be a valid and complete set of request
  // header fields (Section 8.1.2.3).  The server MUST include a method in the
  // ":method" pseudo-header field that is safe and cacheable.
  //
  // RFC 7231, Section  4.2.1:
  // ... this specification defines GET, HEAD, and POST as cacheable, ...
  //
  // Since the OPTIONS method is not cacheable, it cannot be the method of a
  // PUSH_PROMISE. Therefore, the exception mentioned in RFC 7540, Section
  // 8.1.2.3 about OPTIONS requests does not apply here (i.e. ":path" cannot be
  // "*").
  if (path.empty() || path[0] != '/' || (path.size() >= 2 && path[1] == '/')) {
    return std::string();
  }

  // Validate the scheme; this is to ensure a scheme of "foo://bar" is not
  // parsed as a URL of "foo://bar://baz" when combined with a host of "baz".
  std::string canonical_scheme;
  url::StdStringCanonOutput canon_scheme_output(&canonical_scheme);
  url::Component canon_component;
  url::Component scheme_component(0, scheme.size());

  if (!url::CanonicalizeScheme(scheme.data(), scheme_component,
                               &canon_scheme_output, &canon_component) ||
      !canon_component.is_nonempty() || canon_component.begin != 0) {
    return std::string();
  }
  canonical_scheme.resize(canon_component.len + 1);

  // Validate the authority; this is to ensure an authority such as
  // "host/path" is not accepted, as when combined with a scheme like
  // "http://", could result in a URL of "http://host/path".
  url::Component auth_component(0, authority.size());
  url::Component username_component;
  url::Component password_component;
  url::Component host_component;
  url::Component port_component;

  url::ParseAuthority(authority.data(), auth_component, &username_component,
                      &password_component, &host_component, &port_component);

  // RFC 7540, Section 8.1.2.3: The authority MUST NOT include the deprecated
  // "userinfo" subcomponent for "http" or "https" schemed URIs.
  //
  // Note: Although |canonical_scheme| has not yet been checked for that, as
  // it is performed later in processing, only "http" and "https" schemed
  // URIs are supported for PUSH.
  if (username_component.is_valid() || password_component.is_valid()) {
    return std::string();
  }

  // Failed parsing or no host present. ParseAuthority() will ensure that
  // host_component + port_component cover the entire string, if
  // username_component and password_component are not present.
  if (!host_component.is_nonempty()) {
    return std::string();
  }

  // Validate the port (if present; it's optional).
  int parsed_port_number = url::PORT_INVALID;
  if (port_component.is_nonempty()) {
    parsed_port_number = url::ParsePort(authority.data(), port_component);
    if (parsed_port_number < 0 && parsed_port_number != url::PORT_UNSPECIFIED) {
      return std::string();
    }
  }

  // Validate the host by attempting to canonicalize it. Invalid characters
  // will result in a canonicalization failure (e.g. '/')
  std::string canon_host;
  url::StdStringCanonOutput canon_host_output(&canon_host);
  canon_component.reset();
  if (!url::CanonicalizeHost(authority.data(), host_component,
                             &canon_host_output, &canon_component) ||
      !canon_component.is_nonempty() || canon_component.begin != 0) {
    return std::string();
  }

  // At this point, "authority" has been validated to either be of the form
  // 'host:port' or 'host', with 'host' being a valid domain or IP address,
  // and 'port' (if present), being a valid port. Attempt to construct a
  // URL of just the (scheme, host, port), which should be safe and will not
  // result in ambiguous parsing.
  //
  // This also enforces that all PUSHed URLs are either HTTP or HTTPS-schemed
  // URIs, consistent with the other restrictions enforced above.
  //
  // Note: url::CanonicalizeScheme() will have added the ':' to
  // |canonical_scheme|.
  GURL origin_url(canonical_scheme + "//" + std::string(authority));
  if (!origin_url.is_valid() || !origin_url.SchemeIsHTTPOrHTTPS() ||
      // The following checks are merely defense in depth.
      origin_url.has_username() || origin_url.has_password() ||
      (origin_url.has_path() && origin_url.path_piece() != "/") ||
      origin_url.has_query() || origin_url.has_ref()) {
    return std::string();
  }

  // Attempt to parse the path.
  std::string spec = origin_url.GetWithEmptyPath().spec();
  spec.pop_back();  // Remove the '/', as ":path" must contain it.
  spec.append(std::string(path));

  // Attempt to parse the full URL, with the path as well. Ensure there is no
  // fragment to the query.
  GURL full_url(spec);
  if (!full_url.is_valid() || full_url.has_ref()) {
    return std::string();
  }

  return full_url.spec();
}

}  // namespace quic
