// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is designed to be included by C/C++ files which need the contents
// of the HPACK static table. It may be included more than once if necessary.
// See http://httpwg.org/specs/rfc7541.html#static.table.definition

STATIC_TABLE_ENTRY(":authority", "", 1u);
STATIC_TABLE_ENTRY(":method", "GET", 2u);
STATIC_TABLE_ENTRY(":method", "POST", 3u);
STATIC_TABLE_ENTRY(":path", "/", 4u);
STATIC_TABLE_ENTRY(":path", "/index.html", 5u);
STATIC_TABLE_ENTRY(":scheme", "http", 6u);
STATIC_TABLE_ENTRY(":scheme", "https", 7u);
STATIC_TABLE_ENTRY(":status", "200", 8u);
STATIC_TABLE_ENTRY(":status", "204", 9u);
STATIC_TABLE_ENTRY(":status", "206", 10u);
STATIC_TABLE_ENTRY(":status", "304", 11u);
STATIC_TABLE_ENTRY(":status", "400", 12u);
STATIC_TABLE_ENTRY(":status", "404", 13u);
STATIC_TABLE_ENTRY(":status", "500", 14u);
STATIC_TABLE_ENTRY("accept-charset", "", 15u);
STATIC_TABLE_ENTRY("accept-encoding", "gzip, deflate", 16u);
STATIC_TABLE_ENTRY("accept-language", "", 17u);
STATIC_TABLE_ENTRY("accept-ranges", "", 18u);
STATIC_TABLE_ENTRY("accept", "", 19u);
STATIC_TABLE_ENTRY("access-control-allow-origin", "", 20u);
STATIC_TABLE_ENTRY("age", "", 21u);
STATIC_TABLE_ENTRY("allow", "", 22u);
STATIC_TABLE_ENTRY("authorization", "", 23u);
STATIC_TABLE_ENTRY("cache-control", "", 24u);
STATIC_TABLE_ENTRY("content-disposition", "", 25u);
STATIC_TABLE_ENTRY("content-encoding", "", 26u);
STATIC_TABLE_ENTRY("content-language", "", 27u);
STATIC_TABLE_ENTRY("content-length", "", 28u);
STATIC_TABLE_ENTRY("content-location", "", 29u);
STATIC_TABLE_ENTRY("content-range", "", 30u);
STATIC_TABLE_ENTRY("content-type", "", 31u);
STATIC_TABLE_ENTRY("cookie", "", 32u);
STATIC_TABLE_ENTRY("date", "", 33u);
STATIC_TABLE_ENTRY("etag", "", 34u);
STATIC_TABLE_ENTRY("expect", "", 35u);
STATIC_TABLE_ENTRY("expires", "", 36u);
STATIC_TABLE_ENTRY("from", "", 37u);
STATIC_TABLE_ENTRY("host", "", 38u);
STATIC_TABLE_ENTRY("if-match", "", 39u);
STATIC_TABLE_ENTRY("if-modified-since", "", 40u);
STATIC_TABLE_ENTRY("if-none-match", "", 41u);
STATIC_TABLE_ENTRY("if-range", "", 42u);
STATIC_TABLE_ENTRY("if-unmodified-since", "", 43u);
STATIC_TABLE_ENTRY("last-modified", "", 44u);
STATIC_TABLE_ENTRY("link", "", 45u);
STATIC_TABLE_ENTRY("location", "", 46u);
STATIC_TABLE_ENTRY("max-forwards", "", 47u);
STATIC_TABLE_ENTRY("proxy-authenticate", "", 48u);
STATIC_TABLE_ENTRY("proxy-authorization", "", 49u);
STATIC_TABLE_ENTRY("range", "", 50u);
STATIC_TABLE_ENTRY("referer", "", 51u);
STATIC_TABLE_ENTRY("refresh", "", 52u);
STATIC_TABLE_ENTRY("retry-after", "", 53u);
STATIC_TABLE_ENTRY("server", "", 54u);
STATIC_TABLE_ENTRY("set-cookie", "", 55u);
STATIC_TABLE_ENTRY("strict-transport-security", "", 56u);
STATIC_TABLE_ENTRY("transfer-encoding", "", 57u);
STATIC_TABLE_ENTRY("user-agent", "", 58u);
STATIC_TABLE_ENTRY("vary", "", 59u);
STATIC_TABLE_ENTRY("via", "", 60u);
STATIC_TABLE_ENTRY("www-authenticate", "", 61u);
