// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/balsa/balsa_headers.h"

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "quiche/balsa/balsa_enums.h"
#include "quiche/balsa/header_properties.h"
#include "quiche/common/platform/api/quiche_header_policy.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace {

constexpr absl::string_view kContentLength("Content-Length");
constexpr absl::string_view kCookie("Cookie");
constexpr absl::string_view kHost("Host");
constexpr absl::string_view kTransferEncoding("Transfer-Encoding");

// The following list defines list of headers that Envoy considers multivalue.
// Headers on this list are coalesced by EFG in order to provide forward
// compatibility with Envoy behavior. See b/143490671 for details.
// Date, Last-Modified and Location are excluded because they're found on Chrome
// HttpUtil::IsNonCoalescingHeader() list.
#define ALL_ENVOY_HEADERS(HEADER_FUNC)                    \
  HEADER_FUNC("Accept")                                   \
  HEADER_FUNC("Accept-Encoding")                          \
  HEADER_FUNC("Access-Control-Request-Headers")           \
  HEADER_FUNC("Access-Control-Request-Method")            \
  HEADER_FUNC("Access-Control-Allow-Origin")              \
  HEADER_FUNC("Access-Control-Allow-Headers")             \
  HEADER_FUNC("Access-Control-Allow-Methods")             \
  HEADER_FUNC("Access-Control-Allow-Credentials")         \
  HEADER_FUNC("Access-Control-Expose-Headers")            \
  HEADER_FUNC("Access-Control-Max-Age")                   \
  HEADER_FUNC("Authorization")                            \
  HEADER_FUNC("Cache-Control")                            \
  HEADER_FUNC("X-Client-Trace-Id")                        \
  HEADER_FUNC("Connection")                               \
  HEADER_FUNC("Content-Encoding")                         \
  HEADER_FUNC("Content-Length")                           \
  HEADER_FUNC("Content-Type")                             \
  /* HEADER_FUNC("Date") */                               \
  HEADER_FUNC("Envoy-Attempt-Count")                      \
  HEADER_FUNC("Envoy-Degraded")                           \
  HEADER_FUNC("Envoy-Decorator-Operation")                \
  HEADER_FUNC("Envoy-Downstream-Service-Cluster")         \
  HEADER_FUNC("Envoy-Downstream-Service-Node")            \
  HEADER_FUNC("Envoy-Expected-Request-Timeout-Ms")        \
  HEADER_FUNC("Envoy-External-Address")                   \
  HEADER_FUNC("Envoy-Force-Trace")                        \
  HEADER_FUNC("Envoy-Hedge-On-Per-Try-Timeout")           \
  HEADER_FUNC("Envoy-Immediate-Health-Check-Fail")        \
  HEADER_FUNC("Envoy-Internal-Request")                   \
  HEADER_FUNC("Envoy-Ip-Tags")                            \
  HEADER_FUNC("Envoy-Max-Retries")                        \
  HEADER_FUNC("Envoy-Original-Path")                      \
  HEADER_FUNC("Envoy-Original-Url")                       \
  HEADER_FUNC("Envoy-Overloaded")                         \
  HEADER_FUNC("Envoy-Rate-Limited")                       \
  HEADER_FUNC("Envoy-Retry-On")                           \
  HEADER_FUNC("Envoy-Retry-Grpc-On")                      \
  HEADER_FUNC("Envoy-Retriable-StatusCodes")              \
  HEADER_FUNC("Envoy-Retriable-HeaderNames")              \
  HEADER_FUNC("Envoy-Upstream-AltStatName")               \
  HEADER_FUNC("Envoy-Upstream-Canary")                    \
  HEADER_FUNC("Envoy-Upstream-HealthCheckedCluster")      \
  HEADER_FUNC("Envoy-Upstream-RequestPerTryTimeoutMs")    \
  HEADER_FUNC("Envoy-Upstream-RequestTimeoutAltResponse") \
  HEADER_FUNC("Envoy-Upstream-RequestTimeoutMs")          \
  HEADER_FUNC("Envoy-Upstream-ServiceTime")               \
  HEADER_FUNC("Etag")                                     \
  HEADER_FUNC("Expect")                                   \
  HEADER_FUNC("X-Forwarded-Client-Cert")                  \
  HEADER_FUNC("X-Forwarded-For")                          \
  HEADER_FUNC("X-Forwarded-Proto")                        \
  HEADER_FUNC("Grpc-Accept-Encoding")                     \
  HEADER_FUNC("Grpc-Message")                             \
  HEADER_FUNC("Grpc-Status")                              \
  HEADER_FUNC("Grpc-Timeout")                             \
  HEADER_FUNC("Host")                                     \
  HEADER_FUNC("Keep-Alive")                               \
  /* HEADER_FUNC("Last-Modified") */                      \
  /* HEADER_FUNC("Location") */                           \
  HEADER_FUNC("Method")                                   \
  HEADER_FUNC("No-Chunks")                                \
  HEADER_FUNC("Origin")                                   \
  HEADER_FUNC("X-Ot-Span-Context")                        \
  HEADER_FUNC("Path")                                     \
  HEADER_FUNC("Protocol")                                 \
  HEADER_FUNC("Proxy-Connection")                         \
  HEADER_FUNC("Referer")                                  \
  HEADER_FUNC("X-Request-Id")                             \
  HEADER_FUNC("Scheme")                                   \
  HEADER_FUNC("Server")                                   \
  HEADER_FUNC("Status")                                   \
  HEADER_FUNC("TE")                                       \
  HEADER_FUNC("Transfer-Encoding")                        \
  HEADER_FUNC("Upgrade")                                  \
  HEADER_FUNC("User-Agent")                               \
  HEADER_FUNC("Vary")                                     \
  HEADER_FUNC("Via")

// HEADER_FUNC to insert "name" into the MultivaluedHeadersSet of Envoy headers.
#define MULTIVALUE_ENVOY_HEADER(name) {name},

absl::string_view::difference_type FindIgnoreCase(absl::string_view haystack,
                                                  absl::string_view needle) {
  absl::string_view::difference_type pos = 0;
  while (haystack.size() >= needle.size()) {
    if (absl::StartsWithIgnoreCase(haystack, needle)) {
      return pos;
    }
    ++pos;
    haystack.remove_prefix(1);
  }

  return absl::string_view::npos;
}

absl::string_view::difference_type RemoveLeadingWhitespace(
    absl::string_view* text) {
  size_t count = 0;
  const char* ptr = text->data();
  while (count < text->size() && absl::ascii_isspace(*ptr)) {
    count++;
    ptr++;
  }
  text->remove_prefix(count);
  return count;
}

absl::string_view::difference_type RemoveTrailingWhitespace(
    absl::string_view* text) {
  size_t count = 0;
  const char* ptr = text->data() + text->size() - 1;
  while (count < text->size() && absl::ascii_isspace(*ptr)) {
    ++count;
    --ptr;
  }
  text->remove_suffix(count);
  return count;
}

absl::string_view::difference_type RemoveWhitespaceContext(
    absl::string_view* text) {
  return RemoveLeadingWhitespace(text) + RemoveTrailingWhitespace(text);
}

}  // namespace

namespace quiche {

const size_t BalsaBuffer::kDefaultBlocksize;

const BalsaHeaders::MultivaluedHeadersSet&
BalsaHeaders::multivalued_envoy_headers() {
  static const MultivaluedHeadersSet* multivalued_envoy_headers =
      new MultivaluedHeadersSet({ALL_ENVOY_HEADERS(MULTIVALUE_ENVOY_HEADER)});
  return *multivalued_envoy_headers;
}

void BalsaHeaders::ParseTokenList(absl::string_view header_value,
                                  HeaderTokenList* tokens) {
  if (header_value.empty()) {
    return;
  }
  const char* start = header_value.data();
  const char* end = header_value.data() + header_value.size();
  while (true) {
    // Cast `*start` to unsigned char to make values above 127 rank as expected
    // on platforms with signed char, where such values are represented as
    // negative numbers before the cast.

    // search for first nonwhitespace, non separator char.
    while (*start == ',' || static_cast<unsigned char>(*start) <= ' ') {
      ++start;
      if (start == end) {
        return;
      }
    }
    // found. marked.
    const char* nws = start;

    // search for next whitspace or separator char.
    while (*start != ',' && static_cast<unsigned char>(*start) > ' ') {
      ++start;
      if (start == end) {
        if (nws != start) {
          tokens->push_back(absl::string_view(nws, start - nws));
        }
        return;
      }
    }
    tokens->push_back(absl::string_view(nws, start - nws));
  }
}

// This can be called after a std::move() operation, so things might be
// in an unspecified state after the move.
void BalsaHeaders::Clear() {
  balsa_buffer_.Clear();
  transfer_encoding_is_chunked_ = false;
  content_length_ = 0;
  content_length_status_ = BalsaHeadersEnums::NO_CONTENT_LENGTH;
  parsed_response_code_ = 0;
  firstline_buffer_base_idx_ = 0;
  whitespace_1_idx_ = 0;
  non_whitespace_1_idx_ = 0;
  whitespace_2_idx_ = 0;
  non_whitespace_2_idx_ = 0;
  whitespace_3_idx_ = 0;
  non_whitespace_3_idx_ = 0;
  whitespace_4_idx_ = 0;
  header_lines_.clear();
  header_lines_.shrink_to_fit();
}

void BalsaHeaders::CopyFrom(const BalsaHeaders& other) {
  // Protect against copying with self.
  if (this == &other) {
    return;
  }

  balsa_buffer_.CopyFrom(other.balsa_buffer_);
  transfer_encoding_is_chunked_ = other.transfer_encoding_is_chunked_;
  content_length_ = other.content_length_;
  content_length_status_ = other.content_length_status_;
  parsed_response_code_ = other.parsed_response_code_;
  firstline_buffer_base_idx_ = other.firstline_buffer_base_idx_;
  whitespace_1_idx_ = other.whitespace_1_idx_;
  non_whitespace_1_idx_ = other.non_whitespace_1_idx_;
  whitespace_2_idx_ = other.whitespace_2_idx_;
  non_whitespace_2_idx_ = other.non_whitespace_2_idx_;
  whitespace_3_idx_ = other.whitespace_3_idx_;
  non_whitespace_3_idx_ = other.non_whitespace_3_idx_;
  whitespace_4_idx_ = other.whitespace_4_idx_;
  header_lines_ = other.header_lines_;
}

void BalsaHeaders::AddAndMakeDescription(absl::string_view key,
                                         absl::string_view value,
                                         HeaderLineDescription* d) {
  QUICHE_CHECK(d != nullptr);

  if (enforce_header_policy_) {
    QuicheHandleHeaderPolicy(key);
  }

  // + 2 to size for ": "
  size_t line_size = key.size() + 2 + value.size();
  BalsaBuffer::Blocks::size_type block_buffer_idx = 0;
  char* storage = balsa_buffer_.Reserve(line_size, &block_buffer_idx);
  size_t base_idx = storage - GetPtr(block_buffer_idx);

  char* cur_loc = storage;
  memcpy(cur_loc, key.data(), key.size());
  cur_loc += key.size();
  *cur_loc = ':';
  ++cur_loc;
  *cur_loc = ' ';
  ++cur_loc;
  memcpy(cur_loc, value.data(), value.size());
  *d = HeaderLineDescription(
      base_idx, base_idx + key.size(), base_idx + key.size() + 2,
      base_idx + key.size() + 2 + value.size(), block_buffer_idx);
}

void BalsaHeaders::AppendAndMakeDescription(absl::string_view key,
                                            absl::string_view value,
                                            HeaderLineDescription* d) {
  // Figure out how much space we need to reserve for the new header size.
  size_t old_value_size = d->last_char_idx - d->value_begin_idx;
  if (old_value_size == 0) {
    AddAndMakeDescription(key, value, d);
    return;
  }
  absl::string_view old_value(GetPtr(d->buffer_base_idx) + d->value_begin_idx,
                              old_value_size);

  BalsaBuffer::Blocks::size_type block_buffer_idx = 0;
  // + 3 because we potentially need to add ": ", and "," to the line.
  size_t new_size = key.size() + 3 + old_value_size + value.size();
  char* storage = balsa_buffer_.Reserve(new_size, &block_buffer_idx);
  size_t base_idx = storage - GetPtr(block_buffer_idx);

  absl::string_view first_value = old_value;
  absl::string_view second_value = value;
  char* cur_loc = storage;
  memcpy(cur_loc, key.data(), key.size());
  cur_loc += key.size();
  *cur_loc = ':';
  ++cur_loc;
  *cur_loc = ' ';
  ++cur_loc;
  memcpy(cur_loc, first_value.data(), first_value.size());
  cur_loc += first_value.size();
  *cur_loc = ',';
  ++cur_loc;
  memcpy(cur_loc, second_value.data(), second_value.size());

  *d = HeaderLineDescription(base_idx, base_idx + key.size(),
                             base_idx + key.size() + 2, base_idx + new_size,
                             block_buffer_idx);
}

// Reset internal flags for chunked transfer encoding or content length if a
// header we're removing is one of those headers.
void BalsaHeaders::MaybeClearSpecialHeaderValues(absl::string_view key) {
  if (absl::EqualsIgnoreCase(key, kContentLength)) {
    if (transfer_encoding_is_chunked_) {
      return;
    }

    content_length_status_ = BalsaHeadersEnums::NO_CONTENT_LENGTH;
    content_length_ = 0;
    return;
  }

  if (absl::EqualsIgnoreCase(key, kTransferEncoding)) {
    transfer_encoding_is_chunked_ = false;
  }
}

// Removes all keys value pairs with key 'key' starting at 'start'.
void BalsaHeaders::RemoveAllOfHeaderStartingAt(absl::string_view key,
                                               HeaderLines::iterator start) {
  MaybeClearSpecialHeaderValues(key);
  while (start != header_lines_.end()) {
    start->skip = true;
    ++start;
    start = GetHeaderLinesIterator(key, start);
  }
}

void BalsaHeaders::ReplaceOrAppendHeader(absl::string_view key,
                                         absl::string_view value) {
  const HeaderLines::iterator end = header_lines_.end();
  const HeaderLines::iterator begin = header_lines_.begin();
  HeaderLines::iterator i = GetHeaderLinesIterator(key, begin);
  if (i != end) {
    // First, remove all of the header lines including this one.  We want to
    // remove before replacing, in case our replacement ends up being appended
    // at the end (and thus would be removed by this call)
    RemoveAllOfHeaderStartingAt(key, i);
    // Now, take the first instance and replace it.  This will remove the
    // 'skipped' tag if the replacement is done in-place.
    AddAndMakeDescription(key, value, &(*i));
    return;
  }
  AppendHeader(key, value);
}

void BalsaHeaders::AppendHeader(absl::string_view key,
                                absl::string_view value) {
  HeaderLineDescription hld;
  AddAndMakeDescription(key, value, &hld);
  header_lines_.push_back(hld);
}

void BalsaHeaders::AppendToHeader(absl::string_view key,
                                  absl::string_view value) {
  HeaderLines::iterator i = GetHeaderLinesIterator(key, header_lines_.begin());
  if (i == header_lines_.end()) {
    // The header did not exist already.  Instead of appending to an existing
    // header simply append the key/value pair to the headers.
    AppendHeader(key, value);
    return;
  }
  HeaderLineDescription hld = *i;

  AppendAndMakeDescription(key, value, &hld);

  // Invalidate the old header line and add the new one.
  i->skip = true;
  header_lines_.push_back(hld);
}

void BalsaHeaders::AppendToHeaderWithCommaAndSpace(absl::string_view key,
                                                   absl::string_view value) {
  HeaderLines::iterator i = GetHeaderLinesIteratorForLastMultivaluedHeader(key);
  if (i == header_lines_.end()) {
    // The header did not exist already. Instead of appending to an existing
    // header simply append the key/value pair to the headers. No extra
    // space will be added before the value.
    AppendHeader(key, value);
    return;
  }

  std::string space_and_value = absl::StrCat(" ", value);

  HeaderLineDescription hld = *i;
  AppendAndMakeDescription(key, space_and_value, &hld);

  // Invalidate the old header line and add the new one.
  i->skip = true;
  header_lines_.push_back(hld);
}

absl::string_view BalsaHeaders::GetValueFromHeaderLineDescription(
    const HeaderLineDescription& line) const {
  QUICHE_DCHECK_GE(line.last_char_idx, line.value_begin_idx);
  return absl::string_view(GetPtr(line.buffer_base_idx) + line.value_begin_idx,
                           line.last_char_idx - line.value_begin_idx);
}

absl::string_view BalsaHeaders::GetHeader(absl::string_view key) const {
  QUICHE_DCHECK(!header_properties::IsMultivaluedHeader(key))
      << "Header '" << key << "' may consist of multiple lines. Do not "
      << "use BalsaHeaders::GetHeader() or you may be missing some of its "
      << "values.";
  const HeaderLines::const_iterator end = header_lines_.end();
  HeaderLines::const_iterator i = GetConstHeaderLinesIterator(key);
  if (i == end) {
    return absl::string_view();
  }
  return GetValueFromHeaderLineDescription(*i);
}

BalsaHeaders::const_header_lines_iterator BalsaHeaders::GetHeaderPosition(
    absl::string_view key) const {
  const HeaderLines::const_iterator end = header_lines_.end();
  HeaderLines::const_iterator i = GetConstHeaderLinesIterator(key);
  if (i == end) {
    // TODO(tgreer) Convert from HeaderLines::const_iterator to
    // const_header_lines_iterator without calling lines().end(), which is
    // nontrivial. Look for other needless calls to lines().end(), or make
    // lines().end() trivial.
    return lines().end();
  }

  return const_header_lines_iterator(this, (i - header_lines_.begin()));
}

BalsaHeaders::const_header_lines_key_iterator BalsaHeaders::GetIteratorForKey(
    absl::string_view key) const {
  HeaderLines::const_iterator i = GetConstHeaderLinesIterator(key);
  if (i == header_lines_.end()) {
    return header_lines_key_end();
  }

  return const_header_lines_key_iterator(this, (i - header_lines_.begin()),
                                         key);
}

BalsaHeaders::HeaderLines::const_iterator
BalsaHeaders::GetConstHeaderLinesIterator(absl::string_view key) const {
  const HeaderLines::const_iterator end = header_lines_.end();
  for (HeaderLines::const_iterator i = header_lines_.begin(); i != end; ++i) {
    const HeaderLineDescription& line = *i;
    if (line.skip) {
      continue;
    }
    const absl::string_view current_key(
        GetPtr(line.buffer_base_idx) + line.first_char_idx,
        line.key_end_idx - line.first_char_idx);
    if (absl::EqualsIgnoreCase(current_key, key)) {
      QUICHE_DCHECK_GE(line.last_char_idx, line.value_begin_idx);
      return i;
    }
  }
  return end;
}

BalsaHeaders::HeaderLines::iterator BalsaHeaders::GetHeaderLinesIterator(
    absl::string_view key, BalsaHeaders::HeaderLines::iterator start) {
  const HeaderLines::iterator end = header_lines_.end();
  for (HeaderLines::iterator i = start; i != end; ++i) {
    const HeaderLineDescription& line = *i;
    if (line.skip) {
      continue;
    }
    const absl::string_view current_key(
        GetPtr(line.buffer_base_idx) + line.first_char_idx,
        line.key_end_idx - line.first_char_idx);
    if (absl::EqualsIgnoreCase(current_key, key)) {
      QUICHE_DCHECK_GE(line.last_char_idx, line.value_begin_idx);
      return i;
    }
  }
  return end;
}

BalsaHeaders::HeaderLines::iterator
BalsaHeaders::GetHeaderLinesIteratorForLastMultivaluedHeader(
    absl::string_view key) {
  const HeaderLines::iterator end = header_lines_.end();
  HeaderLines::iterator last_found_match;
  bool found_a_match = false;
  for (HeaderLines::iterator i = header_lines_.begin(); i != end; ++i) {
    const HeaderLineDescription& line = *i;
    if (line.skip) {
      continue;
    }
    const absl::string_view current_key(
        GetPtr(line.buffer_base_idx) + line.first_char_idx,
        line.key_end_idx - line.first_char_idx);
    if (absl::EqualsIgnoreCase(current_key, key)) {
      QUICHE_DCHECK_GE(line.last_char_idx, line.value_begin_idx);
      last_found_match = i;
      found_a_match = true;
    }
  }
  return (found_a_match ? last_found_match : end);
}

void BalsaHeaders::GetAllOfHeader(absl::string_view key,
                                  std::vector<absl::string_view>* out) const {
  for (const_header_lines_key_iterator it = GetIteratorForKey(key);
       it != lines().end(); ++it) {
    out->push_back(it->second);
  }
}

void BalsaHeaders::GetAllOfHeaderIncludeRemoved(
    absl::string_view key, std::vector<absl::string_view>* out) const {
  const HeaderLines::const_iterator begin = header_lines_.begin();
  const HeaderLines::const_iterator end = header_lines_.end();
  for (bool add_removed : {false, true}) {
    for (HeaderLines::const_iterator i = begin; i != end; ++i) {
      const HeaderLineDescription& line = *i;
      if ((!add_removed && line.skip) || (add_removed && !line.skip)) {
        continue;
      }
      const absl::string_view current_key(
          GetPtr(line.buffer_base_idx) + line.first_char_idx,
          line.key_end_idx - line.first_char_idx);
      if (absl::EqualsIgnoreCase(current_key, key)) {
        QUICHE_DCHECK_GE(line.last_char_idx, line.value_begin_idx);
        out->push_back(GetValueFromHeaderLineDescription(line));
      }
    }
  }
}

namespace {

// Helper function for HeaderHasValue that checks that the specified region
// within line is preceded by whitespace and a comma or beginning of line,
// and followed by whitespace and a comma or end of line.
bool SurroundedOnlyBySpacesAndCommas(absl::string_view::difference_type idx,
                                     absl::string_view::difference_type end_idx,
                                     absl::string_view line) {
  for (idx = idx - 1; idx >= 0; --idx) {
    if (line[idx] == ',') {
      break;
    }
    if (line[idx] != ' ') {
      return false;
    }
  }

  for (; end_idx < static_cast<int64_t>(line.size()); ++end_idx) {
    if (line[end_idx] == ',') {
      break;
    }
    if (line[end_idx] != ' ') {
      return false;
    }
  }
  return true;
}

}  // namespace

bool BalsaHeaders::HeaderHasValueHelper(absl::string_view key,
                                        absl::string_view value,
                                        bool case_sensitive) const {
  for (const_header_lines_key_iterator it = GetIteratorForKey(key);
       it != lines().end(); ++it) {
    absl::string_view line = it->second;
    absl::string_view::size_type idx =
        case_sensitive ? line.find(value, 0) : FindIgnoreCase(line, value);
    while (idx != absl::string_view::npos) {
      absl::string_view::difference_type end_idx = idx + value.size();
      if (SurroundedOnlyBySpacesAndCommas(idx, end_idx, line)) {
        return true;
      }
      idx = line.find(value, idx + 1);
    }
  }
  return false;
}

bool BalsaHeaders::HasNonEmptyHeader(absl::string_view key) const {
  for (const_header_lines_key_iterator it = GetIteratorForKey(key);
       it != header_lines_key_end(); ++it) {
    if (!it->second.empty()) {
      return true;
    }
  }
  return false;
}

std::string BalsaHeaders::GetAllOfHeaderAsString(absl::string_view key) const {
  // Use custom formatter to ignore header key and join only header values.
  // absl::AlphaNumFormatter is the default formatter for absl::StrJoin().
  auto formatter = [](std::string* out,
                      std::pair<absl::string_view, absl::string_view> header) {
    return absl::AlphaNumFormatter()(out, header.second);
  };
  return absl::StrJoin(GetIteratorForKey(key), header_lines_key_end(), ",",
                       formatter);
}

void BalsaHeaders::RemoveAllOfHeaderInList(const HeaderTokenList& keys) {
  if (keys.empty()) {
    return;
  }

  // This extra copy sacrifices some performance to prevent the possible
  // mistakes that the caller does not lower case the headers in keys.
  // Better performance can be achieved by asking caller to lower case
  // the keys and RemoveAllOfheaderInlist just does lookup.
  absl::flat_hash_set<std::string> lowercase_keys;
  for (const auto& key : keys) {
    MaybeClearSpecialHeaderValues(key);
    lowercase_keys.insert(absl::AsciiStrToLower(key));
  }

  for (HeaderLineDescription& line : header_lines_) {
    if (line.skip) {
      continue;
    }
    // Remove the header if it matches any of the keys to remove.
    const size_t key_len = line.key_end_idx - line.first_char_idx;
    absl::string_view key(GetPtr(line.buffer_base_idx) + line.first_char_idx,
                          key_len);

    std::string lowercase_key = absl::AsciiStrToLower(key);
    if (lowercase_keys.count(lowercase_key) != 0) {
      line.skip = true;
    }
  }
}

void BalsaHeaders::RemoveAllOfHeader(absl::string_view key) {
  HeaderLines::iterator it = GetHeaderLinesIterator(key, header_lines_.begin());
  RemoveAllOfHeaderStartingAt(key, it);
}

void BalsaHeaders::RemoveAllHeadersWithPrefix(absl::string_view prefix) {
  for (HeaderLines::size_type i = 0; i < header_lines_.size(); ++i) {
    if (header_lines_[i].skip) {
      continue;
    }

    HeaderLineDescription& line = header_lines_[i];
    const size_t key_len = line.key_end_idx - line.first_char_idx;
    if (key_len < prefix.size()) {
      continue;
    }

    const absl::string_view current_key_prefix(
        GetPtr(line.buffer_base_idx) + line.first_char_idx, prefix.size());
    if (absl::EqualsIgnoreCase(current_key_prefix, prefix)) {
      const absl::string_view current_key(
          GetPtr(line.buffer_base_idx) + line.first_char_idx, key_len);
      MaybeClearSpecialHeaderValues(current_key);
      line.skip = true;
    }
  }
}

bool BalsaHeaders::HasHeadersWithPrefix(absl::string_view prefix) const {
  for (HeaderLines::size_type i = 0; i < header_lines_.size(); ++i) {
    if (header_lines_[i].skip) {
      continue;
    }

    const HeaderLineDescription& line = header_lines_[i];
    if (line.key_end_idx - line.first_char_idx < prefix.size()) {
      continue;
    }

    const absl::string_view current_key_prefix(
        GetPtr(line.buffer_base_idx) + line.first_char_idx, prefix.size());
    if (absl::EqualsIgnoreCase(current_key_prefix, prefix)) {
      return true;
    }
  }
  return false;
}

void BalsaHeaders::GetAllOfHeaderWithPrefix(
    absl::string_view prefix,
    std::vector<std::pair<absl::string_view, absl::string_view>>* out) const {
  for (HeaderLines::size_type i = 0; i < header_lines_.size(); ++i) {
    if (header_lines_[i].skip) {
      continue;
    }
    const HeaderLineDescription& line = header_lines_[i];
    absl::string_view key(GetPtr(line.buffer_base_idx) + line.first_char_idx,
                          line.key_end_idx - line.first_char_idx);
    if (absl::StartsWithIgnoreCase(key, prefix)) {
      out->push_back(std::make_pair(
          key,
          absl::string_view(GetPtr(line.buffer_base_idx) + line.value_begin_idx,
                            line.last_char_idx - line.value_begin_idx)));
    }
  }
}

void BalsaHeaders::GetAllHeadersWithLimit(
    std::vector<std::pair<absl::string_view, absl::string_view>>* out,
    int limit) const {
  for (HeaderLines::size_type i = 0; i < header_lines_.size(); ++i) {
    if (limit >= 0 && out->size() >= static_cast<size_t>(limit)) {
      return;
    }
    if (header_lines_[i].skip) {
      continue;
    }
    const HeaderLineDescription& line = header_lines_[i];
    absl::string_view key(GetPtr(line.buffer_base_idx) + line.first_char_idx,
                          line.key_end_idx - line.first_char_idx);
    out->push_back(std::make_pair(
        key,
        absl::string_view(GetPtr(line.buffer_base_idx) + line.value_begin_idx,
                          line.last_char_idx - line.value_begin_idx)));
  }
}

size_t BalsaHeaders::RemoveValue(absl::string_view key,
                                 absl::string_view search_value) {
  // Remove whitespace around search value.
  absl::string_view needle = search_value;
  RemoveWhitespaceContext(&needle);
  QUICHE_BUG_IF(bug_22783_2, needle != search_value)
      << "Search value should not be surrounded by spaces.";

  // We have nothing to do for empty needle strings.
  if (needle.empty()) {
    return 0;
  }

  // The return value: number of removed values.
  size_t removals = 0;

  // Iterate over all header lines matching key with skip=false.
  for (HeaderLines::iterator it =
           GetHeaderLinesIterator(key, header_lines_.begin());
       it != header_lines_.end(); it = GetHeaderLinesIterator(key, ++it)) {
    HeaderLineDescription* line = &(*it);

    // If needle given to us is longer than this header, don't consider it.
    if (line->ValuesLength() < needle.size()) {
      continue;
    }

    // If the values are equivalent, just remove the whole line.
    char* buf = GetPtr(line->buffer_base_idx);  // The head of our buffer.
    char* value_begin = buf + line->value_begin_idx;
    // StringPiece containing values that have yet to be processed. The head of
    // this stringpiece will continually move forward, and its tail
    // (head+length) will always remain the same.
    absl::string_view values(value_begin, line->ValuesLength());
    RemoveWhitespaceContext(&values);
    if (values.size() == needle.size()) {
      if (values == needle) {
        line->skip = true;
        removals++;
      }
      continue;
    }

    // Find all occurrences of the needle to be removed.
    char* insertion = value_begin;
    while (values.size() >= needle.size()) {
      // Strip leading whitespace.
      ssize_t cur_leading_whitespace = RemoveLeadingWhitespace(&values);

      // See if we've got a match (at least as a prefix).
      bool found = absl::StartsWith(values, needle);

      // Find the entirety of this value (including trailing comma if existent).
      const size_t next_comma =
          values.find(',', /* pos = */ (found ? needle.size() : 0));
      const bool comma_found = next_comma != absl::string_view::npos;
      const size_t cur_size = (comma_found ? next_comma + 1 : values.size());

      // Make sure that our prefix match is a full match.
      if (found && cur_size != needle.size()) {
        absl::string_view cur(values.data(), cur_size);
        if (comma_found) {
          cur.remove_suffix(1);
        }
        RemoveTrailingWhitespace(&cur);
        found = (cur.size() == needle.size());
      }

      // Move as necessary (avoid move just for the sake of leading whitespace).
      if (found) {
        removals++;
        // Remove trailing comma if we happen to have found the last value.
        if (!comma_found) {
          // We modify insertion since it'll be used to update last_char_idx.
          insertion--;
        }
      } else {
        if (insertion + cur_leading_whitespace != values.data()) {
          // Has the side-effect of also copying any trailing whitespace.
          memmove(insertion, values.data(), cur_size);
          insertion += cur_size;
        } else {
          insertion += cur_leading_whitespace + cur_size;
        }
      }

      // No longer consider the current value. (Increment.)
      values.remove_prefix(cur_size);
    }
    // Move remaining data.
    if (!values.empty()) {
      if (insertion != values.data()) {
        memmove(insertion, values.data(), values.size());
      }
      insertion += values.size();
    }
    // Set new line size.
    if (insertion <= value_begin) {
      // All values removed.
      line->skip = true;
    } else {
      line->last_char_idx = insertion - buf;
    }
  }

  return removals;
}

size_t BalsaHeaders::GetSizeForWriteBuffer() const {
  // First add the space required for the first line + line separator.
  size_t write_buf_size = whitespace_4_idx_ - non_whitespace_1_idx_ + 2;
  // Then add the space needed for each header line to write out + line
  // separator.
  const HeaderLines::size_type end = header_lines_.size();
  for (HeaderLines::size_type i = 0; i < end; ++i) {
    const HeaderLineDescription& line = header_lines_[i];
    if (!line.skip) {
      // Add the key size and ": ".
      write_buf_size += line.key_end_idx - line.first_char_idx + 2;
      // Add the value size and the line separator.
      write_buf_size += line.last_char_idx - line.value_begin_idx + 2;
    }
  }
  // Finally tack on the terminal line separator.
  return write_buf_size + 2;
}

void BalsaHeaders::DumpToString(std::string* str) const {
  DumpToPrefixedString(" ", str);
}

std::string BalsaHeaders::DebugString() const {
  std::string s;
  DumpToString(&s);
  return s;
}

bool BalsaHeaders::ForEachHeader(
    quiche::UnretainedCallback<bool(const absl::string_view key,
                                    const absl::string_view value)>
        fn) const {
  int s = header_lines_.size();
  for (int i = 0; i < s; ++i) {
    const HeaderLineDescription& desc = header_lines_[i];
    if (!desc.skip && desc.KeyLength() > 0) {
      const char* stream_begin = GetPtr(desc.buffer_base_idx);
      if (!fn(absl::string_view(stream_begin + desc.first_char_idx,
                                desc.KeyLength()),
              absl::string_view(stream_begin + desc.value_begin_idx,
                                desc.ValuesLength()))) {
        return false;
      }
    }
  }
  return true;
}

void BalsaHeaders::DumpToPrefixedString(const char* spaces,
                                        std::string* str) const {
  const absl::string_view firstline = first_line();
  const int buffer_length = GetReadableBytesFromHeaderStream();
  // First check whether the header object is empty.
  if (firstline.empty() && buffer_length == 0) {
    absl::StrAppend(str, "\n", spaces, "<empty header>\n");
    return;
  }

  // Then check whether the header is in a partially parsed state. If so, just
  // dump the raw data.
  if (!FramerIsDoneWriting()) {
    absl::StrAppendFormat(str, "\n%s<incomplete header len: %d>\n%s%.*s\n",
                          spaces, buffer_length, spaces, buffer_length,
                          OriginalHeaderStreamBegin());
    return;
  }

  // If the header is complete, then just dump them with the logical key value
  // pair.
  str->reserve(str->size() + GetSizeForWriteBuffer());
  absl::StrAppend(str, "\n", spaces, firstline, "\n");
  for (const auto& line : lines()) {
    absl::StrAppend(str, spaces, line.first, ": ", line.second, "\n");
  }
}

void BalsaHeaders::SetContentLength(size_t length) {
  // If the content-length is already the one we want, don't do anything.
  if (content_length_status_ == BalsaHeadersEnums::VALID_CONTENT_LENGTH &&
      content_length_ == length) {
    return;
  }
  // If header state indicates that there is either a content length or
  // transfer encoding header, remove them before adding the new content
  // length. There is always the possibility that client can manually add
  // either header directly and cause content_length_status_ or
  // transfer_encoding_is_chunked_ to be inconsistent with the actual header.
  // In the interest of efficiency, however, we will assume that clients will
  // use the header object correctly and thus we will not scan the all headers
  // each time this function is called.
  if (content_length_status_ != BalsaHeadersEnums::NO_CONTENT_LENGTH) {
    RemoveAllOfHeader(kContentLength);
  } else if (transfer_encoding_is_chunked_) {
    RemoveAllOfHeader(kTransferEncoding);
  }
  content_length_status_ = BalsaHeadersEnums::VALID_CONTENT_LENGTH;
  content_length_ = length;

  AppendHeader(kContentLength, absl::StrCat(length));
}

void BalsaHeaders::SetTransferEncodingToChunkedAndClearContentLength() {
  if (transfer_encoding_is_chunked_) {
    return;
  }
  if (content_length_status_ != BalsaHeadersEnums::NO_CONTENT_LENGTH) {
    // Per https://httpwg.org/specs/rfc7230.html#header.content-length, we can't
    // send both transfer-encoding and content-length.
    ClearContentLength();
  }
  ReplaceOrAppendHeader(kTransferEncoding, "chunked");
  transfer_encoding_is_chunked_ = true;
}

void BalsaHeaders::SetNoTransferEncoding() {
  if (transfer_encoding_is_chunked_) {
    // clears transfer_encoding_is_chunked_
    RemoveAllOfHeader(kTransferEncoding);
  }
}

void BalsaHeaders::ClearContentLength() { RemoveAllOfHeader(kContentLength); }

bool BalsaHeaders::IsEmpty() const {
  return balsa_buffer_.GetTotalBytesUsed() == 0;
}

absl::string_view BalsaHeaders::Authority() const { return GetHeader(kHost); }

void BalsaHeaders::ReplaceOrAppendAuthority(absl::string_view value) {
  ReplaceOrAppendHeader(kHost, value);
}

void BalsaHeaders::RemoveAuthority() { RemoveAllOfHeader(kHost); }

void BalsaHeaders::ApplyToCookie(
    quiche::UnretainedCallback<void(absl::string_view cookie)> f) const {
  f(GetHeader(kCookie));
}

void BalsaHeaders::SetResponseFirstline(absl::string_view version,
                                        size_t parsed_response_code,
                                        absl::string_view reason_phrase) {
  SetFirstlineFromStringPieces(version, absl::StrCat(parsed_response_code),
                               reason_phrase);
  parsed_response_code_ = parsed_response_code;
}

void BalsaHeaders::SetFirstlineFromStringPieces(absl::string_view firstline_a,
                                                absl::string_view firstline_b,
                                                absl::string_view firstline_c) {
  size_t line_size =
      (firstline_a.size() + firstline_b.size() + firstline_c.size() + 2);
  char* storage = balsa_buffer_.Reserve(line_size, &firstline_buffer_base_idx_);
  char* cur_loc = storage;

  memcpy(cur_loc, firstline_a.data(), firstline_a.size());
  cur_loc += firstline_a.size();

  *cur_loc = ' ';
  ++cur_loc;

  memcpy(cur_loc, firstline_b.data(), firstline_b.size());
  cur_loc += firstline_b.size();

  *cur_loc = ' ';
  ++cur_loc;

  memcpy(cur_loc, firstline_c.data(), firstline_c.size());

  whitespace_1_idx_ = storage - BeginningOfFirstLine();
  non_whitespace_1_idx_ = whitespace_1_idx_;
  whitespace_2_idx_ = non_whitespace_1_idx_ + firstline_a.size();
  non_whitespace_2_idx_ = whitespace_2_idx_ + 1;
  whitespace_3_idx_ = non_whitespace_2_idx_ + firstline_b.size();
  non_whitespace_3_idx_ = whitespace_3_idx_ + 1;
  whitespace_4_idx_ = non_whitespace_3_idx_ + firstline_c.size();
}

void BalsaHeaders::SetRequestMethod(absl::string_view method) {
  // This is the first of the three parts of the firstline.
  if (method.size() <= (whitespace_2_idx_ - non_whitespace_1_idx_)) {
    non_whitespace_1_idx_ = whitespace_2_idx_ - method.size();
    if (!method.empty()) {
      char* stream_begin = BeginningOfFirstLine();
      memcpy(stream_begin + non_whitespace_1_idx_, method.data(),
             method.size());
    }
  } else {
    // The new method is too large to fit in the space available for the old
    // one, so we have to reformat the firstline.
    SetRequestFirstlineFromStringPieces(method, request_uri(),
                                        request_version());
  }
}

void BalsaHeaders::SetResponseVersion(absl::string_view version) {
  // Note: There is no difference between request_method() and
  // response_Version(). Thus, a function to set one is equivalent to a
  // function to set the other. We maintain two functions for this as it is
  // much more descriptive, and makes code more understandable.
  SetRequestMethod(version);
}

void BalsaHeaders::SetRequestUri(absl::string_view uri) {
  SetRequestFirstlineFromStringPieces(request_method(), uri, request_version());
}

void BalsaHeaders::SetResponseCode(absl::string_view code) {
  // Note: There is no difference between request_uri() and response_code().
  // Thus, a function to set one is equivalent to a function to set the other.
  // We maintain two functions for this as it is much more descriptive, and
  // makes code more understandable.
  SetRequestUri(code);
}

void BalsaHeaders::SetParsedResponseCodeAndUpdateFirstline(
    size_t parsed_response_code) {
  parsed_response_code_ = parsed_response_code;
  SetResponseCode(absl::StrCat(parsed_response_code));
}

void BalsaHeaders::SetRequestVersion(absl::string_view version) {
  // This is the last of the three parts of the firstline.
  // Since whitespace_3_idx and non_whitespace_3_idx may point to the same
  // place, we ensure below that any available space includes space for a
  // literal space (' ') character between the second component and the third
  // component.
  bool fits_in_space_allowed =
      version.size() + 1 <= whitespace_4_idx_ - whitespace_3_idx_;

  if (!fits_in_space_allowed) {
    // If the new version is too large, then reformat the firstline.
    SetRequestFirstlineFromStringPieces(request_method(), request_uri(),
                                        version);
    return;
  }

  char* stream_begin = BeginningOfFirstLine();
  *(stream_begin + whitespace_3_idx_) = ' ';
  non_whitespace_3_idx_ = whitespace_3_idx_ + 1;
  whitespace_4_idx_ = non_whitespace_3_idx_ + version.size();
  memcpy(stream_begin + non_whitespace_3_idx_, version.data(), version.size());
}

void BalsaHeaders::SetResponseReasonPhrase(absl::string_view reason) {
  // Note: There is no difference between request_version() and
  // response_reason_phrase(). Thus, a function to set one is equivalent to a
  // function to set the other. We maintain two functions for this as it is
  // much more descriptive, and makes code more understandable.
  SetRequestVersion(reason);
}

void BalsaHeaders::RemoveLastTokenFromHeaderValue(absl::string_view key) {
  BalsaHeaders::HeaderLines::iterator it =
      GetHeaderLinesIterator(key, header_lines_.begin());
  if (it == header_lines_.end()) {
    QUICHE_DLOG(WARNING)
        << "Attempting to remove last token from a non-existent "
        << "header \"" << key << "\"";
    return;
  }

  // Find the last line with that key.
  BalsaHeaders::HeaderLines::iterator header_line;
  do {
    header_line = it;
    it = GetHeaderLinesIterator(key, it + 1);
  } while (it != header_lines_.end());

  // Tokenize just that line.
  BalsaHeaders::HeaderTokenList tokens;
  // Find where this line is stored.
  absl::string_view value(
      GetPtr(header_line->buffer_base_idx) + header_line->value_begin_idx,
      header_line->last_char_idx - header_line->value_begin_idx);
  // Tokenize.
  ParseTokenList(value, &tokens);

  if (tokens.empty()) {
    QUICHE_DLOG(WARNING)
        << "Attempting to remove a token from an empty header value "
        << "for header \"" << key << "\"";
    header_line->skip = true;  // remove the whole line
  } else if (tokens.size() == 1) {
    header_line->skip = true;  // remove the whole line
  } else {
    // Shrink the line size and leave the extra data in the buffer.
    absl::string_view new_last_token = tokens[tokens.size() - 2];
    const char* last_char_address =
        new_last_token.data() + new_last_token.size() - 1;
    const char* const stream_begin = GetPtr(header_line->buffer_base_idx);

    header_line->last_char_idx = last_char_address - stream_begin + 1;
  }
}

bool BalsaHeaders::ResponseCanHaveBody(int response_code) {
  // For responses, can't have a body if the request was a HEAD, or if it is
  // one of these response-codes.  rfc2616 section 4.3
  if (response_code >= 100 && response_code < 200) {
    // 1xx responses can't have bodies.
    return false;
  }

  // No content and Not modified responses have no body.
  return (response_code != 204) && (response_code != 304);
}

}  // namespace quiche
