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
bool DataURL::Parse(const GURL& url, std::string* mime_type,
                    std::string* charset, std::string* data) {
  if (!url.is_valid())
    return false;

  DCHECK(mime_type->empty());
  DCHECK(charset->empty());
  std::string::const_iterator begin = url.spec().begin();
  std::string::const_iterator end = url.spec().end();

  std::string::const_iterator after_colon = std::find(begin, end, ':');
  if (after_colon == end)
    return false;
  ++after_colon;

  std::string::const_iterator comma = std::find(after_colon, end, ',');
  if (comma == end)
    return false;

  std::vector<base::StringPiece> meta_data =
      base::SplitStringPiece(base::StringPiece(after_colon, comma), ";",
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  auto iter = meta_data.cbegin();
  if (iter != meta_data.cend()) {
    *mime_type = base::ToLowerASCII(*iter);
    ++iter;
  }

  static const char kBase64Tag[] = "base64";
  static const char kCharsetTag[] = "charset=";
  const size_t kCharsetTagLength = arraysize(kCharsetTag) - 1;

  bool base64_encoded = false;
  for (; iter != meta_data.cend(); ++iter) {
    if (!base64_encoded && *iter == kBase64Tag) {
      base64_encoded = true;
    } else if (charset->empty() &&
               base::StartsWith(*iter, kCharsetTag,
                                base::CompareCase::SENSITIVE)) {
      *charset = std::string(iter->substr(kCharsetTagLength));
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
  } else if (!ParseMimeTypeWithoutParameter(*mime_type, NULL, NULL)) {
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
  std::string temp_data = std::string(comma + 1, end);

  // For base64, we may have url-escaped whitespace which is not part
  // of the data, and should be stripped. Otherwise, the escaped whitespace
  // could be part of the payload, so don't strip it.
  if (base64_encoded) {
    temp_data = UnescapeURLComponent(
        temp_data, UnescapeRule::SPACES | UnescapeRule::PATH_SEPARATORS |
                       UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
                       UnescapeRule::SPOOFING_AND_CONTROL_CHARS);
  }

  // Strip whitespace.
  if (base64_encoded || !(mime_type->compare(0, 5, "text/") == 0 ||
                          mime_type->find("xml") != std::string::npos)) {
    base::EraseIf(temp_data, base::IsAsciiWhitespace<wchar_t>);
  }

  if (!base64_encoded) {
    temp_data = UnescapeURLComponent(
        temp_data, UnescapeRule::SPACES | UnescapeRule::PATH_SEPARATORS |
                       UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
                       UnescapeRule::SPOOFING_AND_CONTROL_CHARS);
  }

  if (base64_encoded) {
    size_t length = temp_data.length();
    size_t padding_needed = 4 - (length % 4);
    // If the input wasn't padded, then we pad it as necessary until we have a
    // length that is a multiple of 4 as required by our decoder. We don't
    // correct if the input was incorrectly padded. If |padding_needed| == 3,
    // then the input isn't well formed and decoding will fail with or without
    // padding.
    if ((padding_needed == 1 || padding_needed == 2) &&
        temp_data[length - 1] != '=') {
      temp_data.resize(length + padding_needed, '=');
    }
    return base::Base64Decode(temp_data, data);
  }

  temp_data.swap(*data);
  return true;
}

}  // namespace net
