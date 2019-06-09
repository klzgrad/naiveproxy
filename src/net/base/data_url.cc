// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOTE: based loosely on mozilla's nsDataChannel.cpp

#include <algorithm>

#include "net/base/data_url.h"

#include "base/base64.h"
#include "base/stl_util.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "net/base/escape.h"
#include "net/base/mime_util.h"
#include "net/http/http_util.h"
#include "url/gurl.h"

namespace net {

// static
bool DataURL::Parse(const GURL& url,
                    std::string* mime_type,
                    std::string* charset,
                    std::string* data) {
  if (!url.is_valid() || !url.has_scheme())
    return false;

  DCHECK(mime_type->empty());
  DCHECK(charset->empty());

  std::string content = url.GetContent();

  std::string::const_iterator begin = content.begin();
  std::string::const_iterator end = content.end();

  std::string::const_iterator comma = std::find(begin, end, ',');

  if (comma == end)
    return false;

  std::vector<base::StringPiece> meta_data =
      base::SplitStringPiece(base::StringPiece(begin, comma), ";",
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  auto iter = meta_data.cbegin();
  if (iter != meta_data.cend()) {
    *mime_type = base::ToLowerASCII(*iter);
    ++iter;
  }

  static constexpr base::StringPiece kBase64Tag("base64");
  static constexpr base::StringPiece kCharsetTag("charset=");

  bool base64_encoded = false;
  for (; iter != meta_data.cend(); ++iter) {
    if (!base64_encoded &&
        base::EqualsCaseInsensitiveASCII(*iter, kBase64Tag)) {
      base64_encoded = true;
    } else if (charset->empty() &&
               base::StartsWith(*iter, kCharsetTag,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      *charset = std::string(iter->substr(kCharsetTag.size()));
      // The grammar for charset is not specially defined in RFC2045 and
      // RFC2397. It just needs to be a token.
      if (!HttpUtil::IsToken(*charset))
        return false;
    }
  }

  if (mime_type->empty()) {
    // Fallback to the default if nothing specified in the mediatype part as
    // specified in RFC2045. As specified in RFC2397, we use |charset| even if
    // |mime_type| is empty.
    mime_type->assign("text/plain");
    if (charset->empty())
      charset->assign("US-ASCII");
  } else if (!ParseMimeTypeWithoutParameter(*mime_type, nullptr, nullptr)) {
    // Fallback to the default as recommended in RFC2045 when the mediatype
    // value is invalid. For this case, we don't respect |charset| but force it
    // set to "US-ASCII".
    mime_type->assign("text/plain");
    charset->assign("US-ASCII");
  }

  // The caller may not be interested in receiving the data.
  if (!data)
    return true;

  // Preserve spaces if dealing with text or xml input, same as mozilla:
  //   https://bugzilla.mozilla.org/show_bug.cgi?id=138052
  // but strip them otherwise:
  //   https://bugzilla.mozilla.org/show_bug.cgi?id=37200
  // (Spaces in a data URL should be escaped, which is handled below, so any
  // spaces now are wrong. People expect to be able to enter them in the URL
  // bar for text, and it can't hurt, so we allow it.)
  //
  // TODO(mmenke): Is removing all spaces reasonable? GURL removes trailing
  // spaces itself, anyways. Should we just trim leading spaces instead?
  // Allowing random intermediary spaces seems unnecessary.

  base::StringPiece raw_body(comma + 1, end);

  // For base64, we may have url-escaped whitespace which is not part
  // of the data, and should be stripped. Otherwise, the escaped whitespace
  // could be part of the payload, so don't strip it.
  if (base64_encoded) {
    std::string unescaped_body = UnescapeBinaryURLComponent(raw_body);

    // Strip spaces, which aren't allowed in Base64 encoding.
    base::EraseIf(unescaped_body, base::IsAsciiWhitespace<char>);

    size_t length = unescaped_body.length();
    size_t padding_needed = 4 - (length % 4);
    // If the input wasn't padded, then we pad it as necessary until we have a
    // length that is a multiple of 4 as required by our decoder. We don't
    // correct if the input was incorrectly padded. If |padding_needed| == 3,
    // then the input isn't well formed and decoding will fail with or without
    // padding.
    if ((padding_needed == 1 || padding_needed == 2) &&
        unescaped_body[length - 1] != '=') {
      unescaped_body.resize(length + padding_needed, '=');
    }
    return base::Base64Decode(unescaped_body, data);
  }

  // Strip whitespace for non-text MIME types.
  std::string temp;
  if (!(mime_type->compare(0, 5, "text/") == 0 ||
        mime_type->find("xml") != std::string::npos)) {
    temp = raw_body.as_string();
    base::EraseIf(temp, base::IsAsciiWhitespace<char>);
    raw_body = temp;
  }

  *data = UnescapeBinaryURLComponent(raw_body);
  return true;
}

}  // namespace net
