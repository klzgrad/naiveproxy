// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/spdy_utils.h"

#include <memory>
#include <vector>

#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_map_util.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "net/quic/platform/api/quic_text_utils.h"
#include "net/quic/platform/api/quic_url_utils.h"
#include "net/spdy/core/spdy_frame_builder.h"
#include "net/spdy/core/spdy_framer.h"
#include "net/spdy/core/spdy_protocol.h"

using std::string;

namespace net {

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
    const string& name = p.first;
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
    const string& name = p.first;

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
string SpdyUtils::GetUrlFromHeaderBlock(const SpdyHeaderBlock& headers) {
  SpdyHeaderBlock::const_iterator it = headers.find(":scheme");
  if (it == headers.end()) {
    return "";
  }
  std::string url = it->second.as_string();

  url.append("://");

  it = headers.find(":authority");
  if (it == headers.end()) {
    return "";
  }
  url.append(it->second.as_string());

  it = headers.find(":path");
  if (it == headers.end()) {
    return "";
  }
  url.append(it->second.as_string());
  return url;
}

// static
string SpdyUtils::GetHostNameFromHeaderBlock(const SpdyHeaderBlock& headers) {
  // TODO(fayang): Consider just checking out the value of the ":authority" key
  // in headers.
  return QuicUrlUtils::HostName(GetUrlFromHeaderBlock(headers));
}

// static
bool SpdyUtils::UrlIsValid(const SpdyHeaderBlock& headers) {
  string url(GetUrlFromHeaderBlock(headers));
  return !url.empty() && QuicUrlUtils::IsValidUrl(url);
}

// static
bool SpdyUtils::PopulateHeaderBlockFromUrl(const string url,
                                           SpdyHeaderBlock* headers) {
  (*headers)[":method"] = "GET";
  size_t pos = url.find("://");
  if (pos == string::npos) {
    return false;
  }
  (*headers)[":scheme"] = url.substr(0, pos);
  size_t start = pos + 3;
  pos = url.find("/", start);
  if (pos == string::npos) {
    (*headers)[":authority"] = url.substr(start);
    (*headers)[":path"] = "/";
    return true;
  }
  (*headers)[":authority"] = url.substr(start, pos - start);
  (*headers)[":path"] = url.substr(pos);
  return true;
}

}  // namespace net
