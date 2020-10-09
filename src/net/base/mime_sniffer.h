// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MIME_SNIFFER_H__
#define NET_BASE_MIME_SNIFFER_H__

#include <stddef.h>

#include <string>

#include "net/base/net_export.h"

class GURL;

namespace net {

// The maximum number of bytes used by any internal mime sniffing routine. May
// be useful for callers to determine an efficient buffer size to pass to
// |SniffMimeType|.
// This must be updated if any internal sniffing routine needs more bytes.
const int kMaxBytesToSniff = 1024;

// Whether to force the MIME sniffer to sniff the contents of file URLs for
// HTML. kDisabled is recommended.
enum class ForceSniffFileUrlsForHtml {
  kDisabled,
  kEnabled,
};

// Examine the URL and the mime_type and decide whether to sniff a replacement
// mime type from the content.
//
// |url| is the URL from which the content was obtained.
// |mime_type| is the current mime type, e.g. from the Content-Type header.
// Returns true if the mime type should be sniffed.
NET_EXPORT bool ShouldSniffMimeType(const GURL& url,
                                    const std::string& mime_type);

// Guess a mime type from the first few bytes of content an its URL.  Always
// assigns |result| with its best guess of a mime type.
//
// |content| is the buffer containing the bytes to sniff.
// |content_size| is the number of bytes in the |content| buffer.
// |url| is the URL from which the content was obtained.
// |type_hint| is the current mime type, e.g. from the Content-Type header.
// |result| is the address at which to place the sniffed mime type.
// If |force_sniff_file_url_for_html| is enabled, the contents of |file| URLs
// will be sniffed to see if they contain HTML. It is recommended this be
// disabled.
//
// Returns true if |content| had enough data to guess the mime type. Otherwise,
// |result| will be populated with a putative MIME type, but the method should
// be called again with more of the content.
NET_EXPORT bool SniffMimeType(
    const char* content,
    size_t content_size,
    const GURL& url,
    const std::string& type_hint,
    ForceSniffFileUrlsForHtml force_sniff_file_url_for_html,
    std::string* result);

// Attempt to identify a MIME type from the first few bytes of content only.
// Uses a bigger set of media file searches than |SniffMimeType()|.
// If finds a match, fills in |result| and returns true,
// otherwise returns false.
//
// The caller should understand the security ramifications of trusting
// uncontrolled data before accepting the results of this function.
//
// @param content A buffer containing the bytes to sniff.
// @param content_size The number of bytes in the |content| buffer.
// @param result Address at which to place the sniffed mime type.
// @return Returns true if a MIME type match was found.
NET_EXPORT bool SniffMimeTypeFromLocalData(const char* content,
                                           size_t content_size,
                                           std::string* result);

// Returns true if |content| contains bytes that are control codes that do
// not usually appear in plain text.
// @param content A buffer contains bytes that may be binary.
// @param size    The number of bytes in the |content| buffer.
// @return Returns true if |content| looks like binary.
NET_EXPORT_PRIVATE bool LooksLikeBinary(const char* content, size_t size);

}  // namespace net

#endif  // NET_BASE_MIME_SNIFFER_H__
