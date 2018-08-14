// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/http/spdy_utils.h"

#include <memory>
#include <vector>

#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_map_util.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/platform/api/quic_url_utils.h"
#include "net/third_party/spdy/core/spdy_frame_builder.h"
#include "net/third_party/spdy/core/spdy_framer.h"
#include "net/third_party/spdy/core/spdy_protocol.h"

using spdy::SpdyHeaderBlock;

namespace quic {

// static
bool SpdyUtils::ExtractContentLengthFromHeaders(int64_t* content_length,
                                                SpdyHeaderBlock* headers) {
  auto it = headers->find("content-length");
  if (it == headers->end()) {
    return false;
  } else {
    // Check whether multiple values are consistent.
    QuicStringPiece content_length_header = it->second;
    std::vector<QuicStringPiece> values =
        QuicTextUtils::Split(content_length_header, '\0');
    for (const QuicStringPiece& value : values) {
      uint64_t new_value;
      if (!QuicTextUtils::StringToUint64(value, &new_value)) {
        QUIC_DLOG(ERROR)
            << "Content length was either unparseable or negative.";
        return false;
      }
      if (*content_length < 0) {
        *content_length = new_value;
        continue;
      }
      if (new_value != static_cast<uint64_t>(*content_length)) {
        QUIC_DLOG(ERROR)
            << "Parsed content length " << new_value << " is "
            << "inconsistent with previously detected content length "
            << *content_length;
        return false;
      }
    }
    return true;
  }
}

bool SpdyUtils::CopyAndValidateHeaders(const QuicHeaderList& header_list,
                                       int64_t* content_length,
                                       SpdyHeaderBlock* headers) {
  for (const auto& p : header_list) {
    const QuicString& name = p.first;
    if (name.empty()) {
      QUIC_DLOG(ERROR) << "Header name must not be empty.";
      return false;
    }

    if (QuicTextUtils::ContainsUpperCase(name)) {
      QUIC_DLOG(ERROR) << "Malformed header: Header name " << name
                       << " contains upper-case characters.";
      return false;
    }

    headers->AppendValueOrAddHeader(name, p.second);
  }

  if (QuicContainsKey(*headers, "content-length") &&
      !ExtractContentLengthFromHeaders(content_length, headers)) {
    return false;
  }

  QUIC_DVLOG(1) << "Successfully parsed headers: " << headers->DebugString();
  return true;
}

bool SpdyUtils::CopyAndValidateTrailers(const QuicHeaderList& header_list,
                                        size_t* final_byte_offset,
                                        SpdyHeaderBlock* trailers) {
  bool found_final_byte_offset = false;
  for (const auto& p : header_list) {
    const QuicString& name = p.first;

    // Pull out the final offset pseudo header which indicates the number of
    // response body bytes expected.
    if (!found_final_byte_offset && name == kFinalOffsetHeaderKey &&
        QuicTextUtils::StringToSizeT(p.second, final_byte_offset)) {
      found_final_byte_offset = true;
      continue;
    }

    if (name.empty() || name[0] == ':') {
      QUIC_DLOG(ERROR)
          << "Trailers must not be empty, and must not contain pseudo-"
          << "headers. Found: '" << name << "'";
      return false;
    }

    if (QuicTextUtils::ContainsUpperCase(name)) {
      QUIC_DLOG(ERROR) << "Malformed header: Header name " << name
                       << " contains upper-case characters.";
      return false;
    }

    trailers->AppendValueOrAddHeader(name, p.second);
  }

  if (!found_final_byte_offset) {
    QUIC_DLOG(ERROR) << "Required key '" << kFinalOffsetHeaderKey
                     << "' not present";
    return false;
  }

  // TODO(rjshade): Check for other forbidden keys, following the HTTP/2 spec.

  QUIC_DVLOG(1) << "Successfully parsed Trailers: " << trailers->DebugString();
  return true;
}

// static
QuicString SpdyUtils::GetPromisedUrlFromHeaders(
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
    return QuicString();
  }

  it = headers.find(":scheme");
  if (it == headers.end() || it->second.empty()) {
    return QuicString();
  }
  QuicStringPiece scheme = it->second;

  // RFC 7540, Section 8.2: The server MUST include a value in the
  // ":authority" pseudo-header field for which the server is authoritative
  // (see Section 10.1).
  it = headers.find(":authority");
  if (it == headers.end() || it->second.empty()) {
    return QuicString();
  }
  QuicStringPiece authority = it->second;

  // RFC 7540, Section 8.1.2.3 requires that the ":path" pseudo-header MUST
  // NOT be empty for "http" or "https" URIs;
  //
  // However, to ensure the scheme is consistently canonicalized, that check
  // is deferred to implementations in QuicUrlUtils::GetPushPromiseUrl().
  it = headers.find(":path");
  if (it == headers.end()) {
    return QuicString();
  }
  QuicStringPiece path = it->second;

  return QuicUrlUtils::GetPushPromiseUrl(scheme, authority, path);
}

// static
QuicString SpdyUtils::GetPromisedHostNameFromHeaders(
    const SpdyHeaderBlock& headers) {
  // TODO(fayang): Consider just checking out the value of the ":authority" key
  // in headers.
  return QuicUrlUtils::HostName(GetPromisedUrlFromHeaders(headers));
}

// static
bool SpdyUtils::PromisedUrlIsValid(const SpdyHeaderBlock& headers) {
  QuicString url(GetPromisedUrlFromHeaders(headers));
  return !url.empty() && QuicUrlUtils::IsValidUrl(url);
}

// static
bool SpdyUtils::PopulateHeaderBlockFromUrl(const QuicString url,
                                           SpdyHeaderBlock* headers) {
  (*headers)[":method"] = "GET";
  size_t pos = url.find("://");
  if (pos == QuicString::npos) {
    return false;
  }
  (*headers)[":scheme"] = url.substr(0, pos);
  size_t start = pos + 3;
  pos = url.find("/", start);
  if (pos == QuicString::npos) {
    (*headers)[":authority"] = url.substr(start);
    (*headers)[":path"] = "/";
    return true;
  }
  (*headers)[":authority"] = url.substr(start, pos - start);
  (*headers)[":path"] = url.substr(pos);
  return true;
}

}  // namespace quic
