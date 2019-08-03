// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/mime_sniffer.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>

#include "base/strings/string_piece.h"
#include "url/gurl.h"

namespace {

// Finds the line break in |input|, removes every thing up to and including the
// line break from |input|, and returns everything up to the line break as a
// string.
std::string GetNextArgument(base::StringPiece* input) {
  base::StringPiece::size_type argument_end = input->find('\n');
  if (argument_end == base::StringPiece::npos)
    argument_end = input->size();
  base::StringPiece argument = input->substr(0, argument_end);
  *input = input->substr(argument_end + 1);
  return argument.as_string();
}

}  // namespace

// Fuzzer for the two main mime sniffing functions:
// SniffMimeType and SniffMimeTypeFromLocalData.
//
// Breaks |data| up into 3 substrings: URL, MIME type hint, and content, and
// passes them to the MIME sniffing functions (SniffMimeTypeFromLocalData
// does not take all 3 arguments). The first two substrings are each on their
// own line, and content is everything after them. Since neither URLs nor
// content-encoding headers can have line breaks, this doesn't reduce coverage.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // net::SniffMimeType DCHECKs if passed an input buffer that's too large,
  // since it's meant to be used only on the first chunk of a file that's bing
  // fed into a stream. Set a max size of the input to avoid running into that
  // DCHECK.  Use 64k because that's twice the size of a typical read attempt.
  constexpr size_t kMaxSniffLength = 64 * 1024;
  static_assert(kMaxSniffLength >= net::kMaxBytesToSniff,
                "kMaxSniffLength is too small.");

  base::StringPiece input(reinterpret_cast<const char*>(data), size);
  // Divide up the input.  It's important not to pass |url_string| to the GURL
  // constructor until after the length check, to prevent the fuzzer from
  // exploring GURL space with invalid inputs.
  std::string url_string = GetNextArgument(&input);
  std::string mime_type_hint = GetNextArgument(&input);
  net::ForceSniffFileUrlsForHtml force_sniff_file_urls_for_html =
      GetNextArgument(&input).size() == 0
          ? net::ForceSniffFileUrlsForHtml::kDisabled
          : net::ForceSniffFileUrlsForHtml::kEnabled;

  // Do nothing if input is too long.
  if (input.length() > kMaxSniffLength)
    return 0;

  std::string result;
  net::SniffMimeType(input.data(), input.length(), GURL(url_string),
                     mime_type_hint, force_sniff_file_urls_for_html, &result);

  net::SniffMimeTypeFromLocalData(input.data(), input.length(), &result);

  return 0;
}
