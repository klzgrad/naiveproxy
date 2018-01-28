// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the list of load flags and their values. For the enum values,
// include the file "net/base/load_flags.h".
//
// Here we define the values using a macro LOAD_FLAG, so it can be
// expanded differently in some places (for example, to automatically
// map a load flag value to its symbolic name).

LOAD_FLAG(NORMAL, 0)

// This is "normal reload", meaning an if-none-match/if-modified-since query
LOAD_FLAG(VALIDATE_CACHE, 1 << 0)

// This is "shift-reload", meaning a "pragma: no-cache" end-to-end fetch
LOAD_FLAG(BYPASS_CACHE, 1 << 1)

// This is a back/forward style navigation where the cached content should
// be preferred over any protocol specific cache validation.
LOAD_FLAG(SKIP_CACHE_VALIDATION, 1 << 2)

// This is a navigation that will fail if it cannot serve the requested
// resource from the cache (or some equivalent local store).
LOAD_FLAG(ONLY_FROM_CACHE, 1 << 3)

// This is a navigation that will not use the cache at all.  It does not
// impact the HTTP request headers.
LOAD_FLAG(DISABLE_CACHE, 1 << 4)

// If present, causes certificate revocation checks to be skipped on secure
// connections.
LOAD_FLAG(DISABLE_CERT_REVOCATION_CHECKING, 1 << 5)

// This load will not make any changes to cookies, including storing new
// cookies or updating existing ones.
LOAD_FLAG(DO_NOT_SAVE_COOKIES, 1 << 6)

// Do not resolve proxies. This override is used when downloading PAC files
// to avoid having a circular dependency.
LOAD_FLAG(BYPASS_PROXY, 1 << 7)

// Requires EV certificate verification.
LOAD_FLAG(VERIFY_EV_CERT, 1 << 8)

// This load will not send any cookies.
LOAD_FLAG(DO_NOT_SEND_COOKIES, 1 << 9)

// This load will not send authentication data (user name/password)
// to the server (as opposed to the proxy).
LOAD_FLAG(DO_NOT_SEND_AUTH_DATA, 1 << 10)

// This should only be used for testing (set by HttpNetworkTransaction).
LOAD_FLAG(IGNORE_ALL_CERT_ERRORS, 1 << 11)

// DO NOT USE THIS FLAG
// The network stack should not have frame level knowledge.  Any pre-connect
// or pre-resolution requiring that knowledge should be done from the
// stack embedder.
// Indicate that this is a top level frame, so that we don't assume it is a
// subresource and speculatively pre-connect or pre-resolve when a referring
// page is loaded.
LOAD_FLAG(MAIN_FRAME_DEPRECATED, 1 << 12)

// Indicates that this load was motivated by the rel=prefetch feature,
// and is (in theory) not intended for the current frame.
LOAD_FLAG(PREFETCH, 1 << 13)

// Indicates that this load could cause deadlock if it has to wait for another
// request. Overrides socket limits. Must always be used with MAXIMUM_PRIORITY.
LOAD_FLAG(IGNORE_LIMITS, 1 << 14)

// Indicates that the operation is somewhat likely to be due to an
// explicit user action. This can be used as a hint to treat the
// request with higher priority.
LOAD_FLAG(MAYBE_USER_GESTURE, 1 << 15)

// Indicates that the username:password portion of the URL should not
// be honored, but that other forms of authority may be used.
LOAD_FLAG(DO_NOT_USE_EMBEDDED_IDENTITY, 1 << 16)

// Indicates that this request is not to be migrated to a new network when QUIC
// connection migration is enabled.
LOAD_FLAG(DISABLE_CONNECTION_MIGRATION, 1 << 17)

// Indicates that the cache should not check that the request matches the
// response's vary header.
LOAD_FLAG(SKIP_VARY_CHECK, 1 << 18)
