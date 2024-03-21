// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/balsa/balsa_frame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "quiche/balsa/balsa_enums.h"
#include "quiche/balsa/balsa_headers.h"
#include "quiche/balsa/balsa_visitor_interface.h"
#include "quiche/balsa/header_properties.h"
#include "quiche/common/platform/api/quiche_logging.h"

// When comparing characters (other than == and !=), cast to unsigned char
// to make sure values above 127 rank as expected, even on platforms where char
// is signed and thus such values are represented as negative numbers before the
// cast.
#define CHAR_LT(a, b) \
  (static_cast<unsigned char>(a) < static_cast<unsigned char>(b))
#define CHAR_LE(a, b) \
  (static_cast<unsigned char>(a) <= static_cast<unsigned char>(b))
#define CHAR_GT(a, b) \
  (static_cast<unsigned char>(a) > static_cast<unsigned char>(b))
#define CHAR_GE(a, b) \
  (static_cast<unsigned char>(a) >= static_cast<unsigned char>(b))
#define QUICHE_DCHECK_CHAR_GE(a, b) \
  QUICHE_DCHECK_GE(static_cast<unsigned char>(a), static_cast<unsigned char>(b))

namespace quiche {

namespace {

constexpr size_t kContinueStatusCode = 100;
constexpr size_t kSwitchingProtocolsStatusCode = 101;

constexpr absl::string_view kChunked = "chunked";
constexpr absl::string_view kContentLength = "content-length";
constexpr absl::string_view kIdentity = "identity";
constexpr absl::string_view kTransferEncoding = "transfer-encoding";

bool IsInterimResponse(size_t response_code) {
  return response_code >= 100 && response_code < 200;
}

}  // namespace

void BalsaFrame::Reset() {
  last_char_was_slash_r_ = false;
  saw_non_newline_char_ = false;
  start_was_space_ = true;
  chunk_length_character_extracted_ = false;
  // is_request_ = true;               // not reset between messages.
  allow_reading_until_close_for_request_ = false;
  // request_was_head_ = false;        // not reset between messages.
  // max_header_length_ = 16 * 1024;   // not reset between messages.
  // visitor_ = &do_nothing_visitor_;  // not reset between messages.
  chunk_length_remaining_ = 0;
  content_length_remaining_ = 0;
  last_slash_n_idx_ = 0;
  term_chars_ = 0;
  parse_state_ = BalsaFrameEnums::READING_HEADER_AND_FIRSTLINE;
  last_error_ = BalsaFrameEnums::BALSA_NO_ERROR;
  invalid_chars_.clear();
  lines_.clear();
  if (continue_headers_ != nullptr) {
    continue_headers_->Clear();
  }
  if (headers_ != nullptr) {
    headers_->Clear();
  }
  trailer_lines_.clear();
  start_of_trailer_line_ = 0;
  trailer_length_ = 0;
  if (trailers_ != nullptr) {
    trailers_->Clear();
  }
}

namespace {

// Within the line bounded by [current, end), parses a single "island",
// comprising a (possibly empty) span of whitespace followed by a (possibly
// empty) span of non-whitespace.
//
// Returns a pointer to the first whitespace character beyond this island, or
// returns end if no additional whitespace characters are present after this
// island.  (I.e., returnvalue == end || *returnvalue > ' ')
//
// Upon return, the whitespace span are the characters
// whose indices fall in [*first_whitespace, *first_nonwhite), while the
// non-whitespace span are the characters whose indices fall in
// [*first_nonwhite, returnvalue - begin).
inline const char* ParseOneIsland(const char* current, const char* begin,
                                  const char* end, size_t* first_whitespace,
                                  size_t* first_nonwhite) {
  *first_whitespace = current - begin;
  while (current < end && CHAR_LE(*current, ' ')) {
    ++current;
  }
  *first_nonwhite = current - begin;
  while (current < end && CHAR_GT(*current, ' ')) {
    ++current;
  }
  return current;
}

}  // namespace

// Summary:
//     Parses the first line of either a request or response.
//     Note that in the case of a detected warning, error_code will be set
//   but the function will not return false.
//     Exactly zero or one warning or error (but not both) may be detected
//   by this function.
//     Note that this function will not write the data of the first-line
//   into the header's buffer (that should already have been done elsewhere).
//
// Pre-conditions:
//     begin != end
//     *begin should be a character which is > ' '. This implies that there
//   is at least one non-whitespace characters between [begin, end).
//   headers is a valid pointer to a BalsaHeaders class.
//     error_code is a valid pointer to a BalsaFrameEnums::ErrorCode value.
//     Entire first line must exist between [begin, end)
//     Exactly zero or one newlines -may- exist between [begin, end)
//     [begin, end) should exist in the header's buffer.
//
// Side-effects:
//   headers will be modified
//   error_code may be modified if either a warning or error is detected
//
// Returns:
//   True if no error (as opposed to warning) is detected.
//   False if an error (as opposed to warning) is detected.

//
// If there is indeed non-whitespace in the line, then the following
// will take care of this for you:
//  while (*begin <= ' ') ++begin;
//  ProcessFirstLine(begin, end, is_request, &headers, &error_code);
//

bool ParseHTTPFirstLine(const char* begin, const char* end, bool is_request,
                        BalsaHeaders* headers,
                        BalsaFrameEnums::ErrorCode* error_code) {
  while (begin < end && (end[-1] == '\n' || end[-1] == '\r')) {
    --end;
  }

  const char* current =
      ParseOneIsland(begin, begin, end, &headers->whitespace_1_idx_,
                     &headers->non_whitespace_1_idx_);
  current = ParseOneIsland(current, begin, end, &headers->whitespace_2_idx_,
                           &headers->non_whitespace_2_idx_);
  current = ParseOneIsland(current, begin, end, &headers->whitespace_3_idx_,
                           &headers->non_whitespace_3_idx_);

  // Clean up any trailing whitespace that comes after the third island
  const char* last = end;
  while (current <= last && CHAR_LE(*last, ' ')) {
    --last;
  }
  headers->whitespace_4_idx_ = last - begin + 1;

  // Either the passed-in line is empty, or it starts with a non-whitespace
  // character.
  QUICHE_DCHECK(begin == end || static_cast<unsigned char>(*begin) > ' ');

  QUICHE_DCHECK_EQ(0u, headers->whitespace_1_idx_);
  QUICHE_DCHECK_EQ(0u, headers->non_whitespace_1_idx_);

  // If the line isn't empty, it has at least one non-whitespace character (see
  // first QUICHE_DCHECK), which will have been identified as a non-empty
  // [non_whitespace_1_idx_, whitespace_2_idx_).
  QUICHE_DCHECK(begin == end ||
                headers->non_whitespace_1_idx_ < headers->whitespace_2_idx_);

  if (headers->non_whitespace_2_idx_ == headers->whitespace_3_idx_) {
    // This error may be triggered if the second token is empty, OR there's no
    // WS after the first token; we don't bother to distinguish exactly which.
    // (I'm not sure why we distinguish different kinds of parse error at all,
    // actually.)
    // FAILED_TO_FIND_WS_AFTER_REQUEST_METHOD   for request
    // FAILED_TO_FIND_WS_AFTER_RESPONSE_VERSION for response
    *error_code = static_cast<BalsaFrameEnums::ErrorCode>(
        BalsaFrameEnums::FAILED_TO_FIND_WS_AFTER_RESPONSE_VERSION +
        static_cast<int>(is_request));
    if (!is_request) {  // FAILED_TO_FIND_WS_AFTER_RESPONSE_VERSION
      return false;
    }
  }
  if (headers->whitespace_3_idx_ == headers->non_whitespace_3_idx_) {
    if (*error_code == BalsaFrameEnums::BALSA_NO_ERROR) {
      // FAILED_TO_FIND_WS_AFTER_REQUEST_METHOD   for request
      // FAILED_TO_FIND_WS_AFTER_RESPONSE_VERSION for response
      *error_code = static_cast<BalsaFrameEnums::ErrorCode>(
          BalsaFrameEnums::FAILED_TO_FIND_WS_AFTER_RESPONSE_STATUSCODE +
          static_cast<int>(is_request));
    }
  }

  if (!is_request) {
    headers->parsed_response_code_ = 0;
    // If the response code is non-empty:
    if (headers->non_whitespace_2_idx_ < headers->whitespace_3_idx_) {
      if (!absl::SimpleAtoi(
              absl::string_view(begin + headers->non_whitespace_2_idx_,
                                headers->non_whitespace_3_idx_ -
                                    headers->non_whitespace_2_idx_),
              &headers->parsed_response_code_)) {
        *error_code = BalsaFrameEnums::FAILED_CONVERTING_STATUS_CODE_TO_INT;
        return false;
      }
    }
  }

  return true;
}

// begin - beginning of the firstline
// end - end of the firstline
//
// A precondition for this function is that there is non-whitespace between
// [begin, end). If this precondition is not met, the function will not perform
// as expected (and bad things may happen, and it will eat your first, second,
// and third unborn children!).
//
// Another precondition for this function is that [begin, end) includes
// at most one newline, which must be at the end of the line.
void BalsaFrame::ProcessFirstLine(const char* begin, const char* end) {
  BalsaFrameEnums::ErrorCode previous_error = last_error_;
  if (!ParseHTTPFirstLine(begin, end, is_request_, headers_, &last_error_)) {
    parse_state_ = BalsaFrameEnums::ERROR;
    HandleError(last_error_);
    return;
  }
  if (previous_error != last_error_) {
    HandleWarning(last_error_);
  }

  const absl::string_view line_input(
      begin + headers_->non_whitespace_1_idx_,
      headers_->whitespace_4_idx_ - headers_->non_whitespace_1_idx_);
  const absl::string_view part1(
      begin + headers_->non_whitespace_1_idx_,
      headers_->whitespace_2_idx_ - headers_->non_whitespace_1_idx_);
  const absl::string_view part2(
      begin + headers_->non_whitespace_2_idx_,
      headers_->whitespace_3_idx_ - headers_->non_whitespace_2_idx_);
  const absl::string_view part3(
      begin + headers_->non_whitespace_3_idx_,
      headers_->whitespace_4_idx_ - headers_->non_whitespace_3_idx_);

  if (is_request_) {
    visitor_->OnRequestFirstLineInput(line_input, part1, part2, part3);
    if (part3.empty()) {
      parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
    }
    return;
  }

  visitor_->OnResponseFirstLineInput(line_input, part1, part2, part3);
}

// 'stream_begin' points to the first character of the headers buffer.
// 'line_begin' points to the first character of the line.
// 'current' points to a char which is ':'.
// 'line_end' points to the position of '\n' + 1.
// 'line_begin' points to the position of first character of line.
void BalsaFrame::CleanUpKeyValueWhitespace(
    const char* stream_begin, const char* line_begin, const char* current,
    const char* line_end, HeaderLineDescription* current_header_line) {
  const char* colon_loc = current;
  QUICHE_DCHECK_LT(colon_loc, line_end);
  QUICHE_DCHECK_EQ(':', *colon_loc);
  QUICHE_DCHECK_EQ(':', *current);
  QUICHE_DCHECK_CHAR_GE(' ', *line_end)
      << "\"" << std::string(line_begin, line_end) << "\"";

  --current;
  while (current > line_begin && CHAR_LE(*current, ' ')) {
    --current;
  }
  current += static_cast<int>(current != colon_loc);
  current_header_line->key_end_idx = current - stream_begin;

  current = colon_loc;
  QUICHE_DCHECK_EQ(':', *current);
  ++current;
  while (current < line_end && CHAR_LE(*current, ' ')) {
    ++current;
  }
  current_header_line->value_begin_idx = current - stream_begin;

  QUICHE_DCHECK_GE(current_header_line->key_end_idx,
                   current_header_line->first_char_idx);
  QUICHE_DCHECK_GE(current_header_line->value_begin_idx,
                   current_header_line->key_end_idx);
  QUICHE_DCHECK_GE(current_header_line->last_char_idx,
                   current_header_line->value_begin_idx);
}

bool BalsaFrame::FindColonsAndParseIntoKeyValue(const Lines& lines,
                                                bool is_trailer,
                                                BalsaHeaders* headers) {
  QUICHE_DCHECK(!lines.empty());
  const char* stream_begin = headers->OriginalHeaderStreamBegin();
  // The last line is always just a newline (and is uninteresting).
  const Lines::size_type lines_size_m1 = lines.size() - 1;
  // For a trailer, there is no first line, so lines[0] is the first header.
  // For real headers, the first line takes lines[0], so real header starts
  // at index 1.
  int first_header_idx = (is_trailer ? 0 : 1);
  const char* current = stream_begin + lines[first_header_idx].first;
  // This code is a bit more subtle than it may appear at first glance.
  // This code looks for a colon in the current line... but it also looks
  // beyond the current line. If there is no colon in the current line, then
  // for each subsequent line (until the colon which -has- been found is
  // associated with a line), no searching for a colon will be performed. In
  // this way, we minimize the amount of bytes we have scanned for a colon.
  for (Lines::size_type i = first_header_idx; i < lines_size_m1;) {
    const char* line_begin = stream_begin + lines[i].first;

    // Here we handle possible continuations.  Note that we do not replace
    // the '\n' in the line before a continuation (at least, as of now),
    // which implies that any code which looks for a value must deal with
    // "\r\n", etc -within- the line (and not just at the end of it).
    for (++i; i < lines_size_m1; ++i) {
      const char c = *(stream_begin + lines[i].first);
      if (CHAR_GT(c, ' ')) {
        // Not a continuation, so stop.  Note that if the 'original' i = 1,
        // and the next line is not a continuation, we'll end up with i = 2
        // when we break. This handles the incrementing of i for the outer
        // loop.
        break;
      }

      // Space and tab are valid starts to continuation lines.
      // https://tools.ietf.org/html/rfc7230#section-3.2.4 says that a proxy
      // can choose to reject or normalize continuation lines.
      if ((c != ' ' && c != '\t') ||
          http_validation_policy().disallow_header_continuation_lines) {
        HandleError(is_trailer ? BalsaFrameEnums::INVALID_TRAILER_FORMAT
                               : BalsaFrameEnums::INVALID_HEADER_FORMAT);
        return false;
      }

      // If disallow_header_continuation_lines() is false, we neither reject nor
      // normalize continuation lines, in violation of RFC7230.
    }
    const char* line_end = stream_begin + lines[i - 1].second;
    QUICHE_DCHECK_LT(line_begin - stream_begin, line_end - stream_begin);

    // We cleanup the whitespace at the end of the line before doing anything
    // else of interest as it allows us to do nothing when irregularly formatted
    // headers are parsed (e.g. those with only keys, only values, or no colon).
    //
    // We're guaranteed to have *line_end > ' ' while line_end >= line_begin.
    --line_end;
    QUICHE_DCHECK_EQ('\n', *line_end)
        << "\"" << std::string(line_begin, line_end) << "\"";
    while (CHAR_LE(*line_end, ' ') && line_end > line_begin) {
      --line_end;
    }
    ++line_end;
    QUICHE_DCHECK_CHAR_GE(' ', *line_end);
    QUICHE_DCHECK_LT(line_begin, line_end);

    // We use '0' for the block idx, because we're always writing to the first
    // block from the framer (we do this because the framer requires that the
    // entire header sequence be in a contiguous buffer).
    headers->header_lines_.push_back(HeaderLineDescription(
        line_begin - stream_begin, line_end - stream_begin,
        line_end - stream_begin, line_end - stream_begin, 0));
    if (current >= line_end) {
      if (http_validation_policy().require_header_colon) {
        HandleError(is_trailer ? BalsaFrameEnums::TRAILER_MISSING_COLON
                               : BalsaFrameEnums::HEADER_MISSING_COLON);
        return false;
      }
      HandleWarning(is_trailer ? BalsaFrameEnums::TRAILER_MISSING_COLON
                               : BalsaFrameEnums::HEADER_MISSING_COLON);
      // Then the next colon will not be found within this header line-- time
      // to try again with another header-line.
      continue;
    }
    if (current < line_begin) {
      // When this condition is true, the last detected colon was part of a
      // previous line.  We reset to the beginning of the line as we don't care
      // about the presence of any colon before the beginning of the current
      // line.
      current = line_begin;
    }
    for (; current < line_end; ++current) {
      if (*current == ':') {
        break;
      }

      // Generally invalid characters were found earlier.
      if (http_validation_policy().disallow_double_quote_in_header_name) {
        if (header_properties::IsInvalidHeaderKeyChar(*current)) {
          HandleError(is_trailer
                          ? BalsaFrameEnums::INVALID_TRAILER_NAME_CHARACTER
                          : BalsaFrameEnums::INVALID_HEADER_NAME_CHARACTER);
          return false;
        }
      } else if (header_properties::IsInvalidHeaderKeyCharAllowDoubleQuote(
                     *current)) {
        HandleError(is_trailer
                        ? BalsaFrameEnums::INVALID_TRAILER_NAME_CHARACTER
                        : BalsaFrameEnums::INVALID_HEADER_NAME_CHARACTER);
        return false;
      }
    }

    if (current == line_end) {
      // There was no colon in the line. The arguments we passed into the
      // construction for the HeaderLineDescription object should be OK-- it
      // assumes that the entire content is 'key' by default (which is true, as
      // there was no colon, there can be no value). Note that this is a
      // construct which is technically not allowed by the spec.

      // In strict mode, we do treat this invalid value-less key as an error.
      if (http_validation_policy().require_header_colon) {
        HandleError(is_trailer ? BalsaFrameEnums::TRAILER_MISSING_COLON
                               : BalsaFrameEnums::HEADER_MISSING_COLON);
        return false;
      }
      HandleWarning(is_trailer ? BalsaFrameEnums::TRAILER_MISSING_COLON
                               : BalsaFrameEnums::HEADER_MISSING_COLON);
      continue;
    }

    QUICHE_DCHECK_EQ(*current, ':');
    QUICHE_DCHECK_LE(current - stream_begin, line_end - stream_begin);
    QUICHE_DCHECK_LE(stream_begin - stream_begin, current - stream_begin);

    HeaderLineDescription& current_header_line = headers->header_lines_.back();
    current_header_line.key_end_idx = current - stream_begin;
    current_header_line.value_begin_idx = current_header_line.key_end_idx;
    if (current < line_end) {
      ++current_header_line.key_end_idx;

      CleanUpKeyValueWhitespace(stream_begin, line_begin, current, line_end,
                                &current_header_line);
    }
  }

  return true;
}

void BalsaFrame::HandleWarning(BalsaFrameEnums::ErrorCode error_code) {
  last_error_ = error_code;
  visitor_->HandleWarning(last_error_);
}

void BalsaFrame::HandleError(BalsaFrameEnums::ErrorCode error_code) {
  last_error_ = error_code;
  parse_state_ = BalsaFrameEnums::ERROR;
  visitor_->HandleError(last_error_);
}

BalsaHeadersEnums::ContentLengthStatus BalsaFrame::ProcessContentLengthLine(
    HeaderLines::size_type line_idx, size_t* length) {
  const HeaderLineDescription& header_line = headers_->header_lines_[line_idx];
  const char* stream_begin = headers_->OriginalHeaderStreamBegin();
  const char* line_end = stream_begin + header_line.last_char_idx;
  const char* value_begin = (stream_begin + header_line.value_begin_idx);

  if (value_begin >= line_end) {
    // There is no non-whitespace value data.
    QUICHE_DVLOG(1) << "invalid content-length -- no non-whitespace value data";
    return BalsaHeadersEnums::INVALID_CONTENT_LENGTH;
  }

  *length = 0;
  while (value_begin < line_end) {
    if (*value_begin < '0' || *value_begin > '9') {
      // bad! content-length found, and couldn't parse all of it!
      QUICHE_DVLOG(1)
          << "invalid content-length - non numeric character detected";
      return BalsaHeadersEnums::INVALID_CONTENT_LENGTH;
    }
    const size_t kMaxDiv10 = std::numeric_limits<size_t>::max() / 10;
    size_t length_x_10 = *length * 10;
    const size_t c = *value_begin - '0';
    if (*length > kMaxDiv10 ||
        (std::numeric_limits<size_t>::max() - length_x_10) < c) {
      QUICHE_DVLOG(1) << "content-length overflow";
      return BalsaHeadersEnums::CONTENT_LENGTH_OVERFLOW;
    }
    *length = length_x_10 + c;
    ++value_begin;
  }
  QUICHE_DVLOG(1) << "content_length parsed: " << *length;
  return BalsaHeadersEnums::VALID_CONTENT_LENGTH;
}

void BalsaFrame::ProcessTransferEncodingLine(HeaderLines::size_type line_idx) {
  const HeaderLineDescription& header_line = headers_->header_lines_[line_idx];
  const char* stream_begin = headers_->OriginalHeaderStreamBegin();
  const absl::string_view transfer_encoding(
      stream_begin + header_line.value_begin_idx,
      header_line.last_char_idx - header_line.value_begin_idx);

  if (absl::EqualsIgnoreCase(transfer_encoding, kChunked)) {
    headers_->transfer_encoding_is_chunked_ = true;
    return;
  }

  if (absl::EqualsIgnoreCase(transfer_encoding, kIdentity)) {
    headers_->transfer_encoding_is_chunked_ = false;
    return;
  }

  if (http_validation_policy().validate_transfer_encoding) {
    HandleError(BalsaFrameEnums::UNKNOWN_TRANSFER_ENCODING);
  }
}

bool BalsaFrame::CheckHeaderLinesForInvalidChars(const Lines& lines,
                                                 const BalsaHeaders* headers) {
  // Read from the beginning of the first line to the end of the last line.
  // Note we need to add the first line's offset as in the case of a trailer
  // it's non-zero.
  const char* stream_begin =
      headers->OriginalHeaderStreamBegin() + lines.front().first;
  const char* stream_end =
      headers->OriginalHeaderStreamBegin() + lines.back().second;
  bool found_invalid = false;

  for (const char* c = stream_begin; c < stream_end; c++) {
    if (header_properties::IsInvalidHeaderChar(*c)) {
      found_invalid = true;
      invalid_chars_[*c]++;
    }
  }

  return found_invalid;
}

void BalsaFrame::ProcessHeaderLines(const Lines& lines, bool is_trailer,
                                    BalsaHeaders* headers) {
  QUICHE_DCHECK(!lines.empty());
  QUICHE_DVLOG(1) << "******@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@**********\n";

  if ((is_request() || http_validation_policy()
                           .disallow_invalid_header_characters_in_response) &&
      track_invalid_chars()) {
    if (CheckHeaderLinesForInvalidChars(lines, headers)) {
      if (invalid_chars_error_enabled()) {
        HandleError(BalsaFrameEnums::INVALID_HEADER_CHARACTER);
        return;
      }

      HandleWarning(BalsaFrameEnums::INVALID_HEADER_CHARACTER);
    }
  }

  // There is no need to attempt to process headers (resp. trailers)
  // if no header (resp. trailer) lines exist.
  //
  // The last line of the message, which is an empty line, is never a header
  // (resp. trailer) line.  Furthermore, the first line of the message is not
  // a header line.  Therefore there are at least two (resp. one) lines in the
  // message which are not header (resp. trailer) lines.
  //
  // Thus, we test to see if we have more than two (resp. one) lines total
  // before attempting to parse any header (resp. trailer) lines.
  if (lines.size() <= (is_trailer ? 1 : 2)) {
    return;
  }

  HeaderLines::size_type content_length_idx = 0;
  HeaderLines::size_type transfer_encoding_idx = 0;
  const char* stream_begin = headers->OriginalHeaderStreamBegin();
  // Parse the rest of the header or trailer data into key-value pairs.
  if (!FindColonsAndParseIntoKeyValue(lines, is_trailer, headers)) {
    return;
  }
  // At this point, we've parsed all of the headers/trailers.  Time to look
  // for those headers which we require for framing or for format errors.
  const HeaderLines::size_type lines_size = headers->header_lines_.size();
  for (HeaderLines::size_type i = 0; i < lines_size; ++i) {
    const HeaderLineDescription& line = headers->header_lines_[i];
    const absl::string_view key(stream_begin + line.first_char_idx,
                                line.key_end_idx - line.first_char_idx);
    QUICHE_DVLOG(2) << "[" << i << "]: " << key << " key_len: " << key.length();

    // If a header begins with either lowercase or uppercase 'c' or 't', then
    // the header may be one of content-length, connection, content-encoding
    // or transfer-encoding. These headers are special, as they change the way
    // that the message is framed, and so the framer is required to search
    // for them.  However, first check for a formatting error, and skip
    // special header treatment on trailer lines (when is_trailer is true).
    if (key.empty() || key[0] == ' ') {
      parse_state_ = BalsaFrameEnums::ERROR;
      HandleError(is_trailer ? BalsaFrameEnums::INVALID_TRAILER_FORMAT
                             : BalsaFrameEnums::INVALID_HEADER_FORMAT);
      return;
    }
    if (is_trailer) {
      continue;
    }
    if (absl::EqualsIgnoreCase(key, kContentLength)) {
      size_t length = 0;
      BalsaHeadersEnums::ContentLengthStatus content_length_status =
          ProcessContentLengthLine(i, &length);
      if (content_length_idx == 0) {
        content_length_idx = i + 1;
        headers->content_length_status_ = content_length_status;
        headers->content_length_ = length;
        content_length_remaining_ = length;
        continue;
      }
      if ((headers->content_length_status_ != content_length_status) ||
          ((headers->content_length_status_ ==
            BalsaHeadersEnums::VALID_CONTENT_LENGTH) &&
           (http_validation_policy().disallow_multiple_content_length ||
            length != headers->content_length_))) {
        HandleError(BalsaFrameEnums::MULTIPLE_CONTENT_LENGTH_KEYS);
        return;
      }
      continue;
    }
    if (absl::EqualsIgnoreCase(key, kTransferEncoding)) {
      if (http_validation_policy().validate_transfer_encoding &&
          transfer_encoding_idx != 0) {
        HandleError(BalsaFrameEnums::MULTIPLE_TRANSFER_ENCODING_KEYS);
        return;
      }
      transfer_encoding_idx = i + 1;
    }
  }

  if (!is_trailer) {
    if (http_validation_policy().validate_transfer_encoding &&
        http_validation_policy()
            .disallow_transfer_encoding_with_content_length &&
        content_length_idx != 0 && transfer_encoding_idx != 0) {
      HandleError(BalsaFrameEnums::BOTH_TRANSFER_ENCODING_AND_CONTENT_LENGTH);
      return;
    }
    if (headers->transfer_encoding_is_chunked_) {
      headers->content_length_ = 0;
      headers->content_length_status_ = BalsaHeadersEnums::NO_CONTENT_LENGTH;
      content_length_remaining_ = 0;
    }
    if (transfer_encoding_idx != 0) {
      ProcessTransferEncodingLine(transfer_encoding_idx - 1);
    }
  }
}

void BalsaFrame::AssignParseStateAfterHeadersHaveBeenParsed() {
  // For responses, can't have a body if the request was a HEAD, or if it is
  // one of these response-codes.  rfc2616 section 4.3
  parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
  int response_code = headers_->parsed_response_code_;
  if (!is_request_ && (request_was_head_ ||
                       !BalsaHeaders::ResponseCanHaveBody(response_code))) {
    // There is no body.
    return;
  }

  if (headers_->transfer_encoding_is_chunked_) {
    // Note that
    // if ( Transfer-Encoding: chunked &&  Content-length: )
    // then Transfer-Encoding: chunked trumps.
    // This is as specified in the spec.
    // rfc2616 section 4.4.3
    parse_state_ = BalsaFrameEnums::READING_CHUNK_LENGTH;
    return;
  }

  // Errors parsing content-length definitely can cause
  // protocol errors/warnings
  switch (headers_->content_length_status_) {
    // If we have a content-length, and it is parsed
    // properly, there are two options.
    // 1) zero content, in which case the message is done, and
    // 2) nonzero content, in which case we have to
    //    consume the body.
    case BalsaHeadersEnums::VALID_CONTENT_LENGTH:
      if (headers_->content_length_ == 0) {
        parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
      } else {
        parse_state_ = BalsaFrameEnums::READING_CONTENT;
      }
      break;
    case BalsaHeadersEnums::CONTENT_LENGTH_OVERFLOW:
    case BalsaHeadersEnums::INVALID_CONTENT_LENGTH:
      // If there were characters left-over after parsing the
      // content length, we should flag an error and stop.
      HandleError(BalsaFrameEnums::UNPARSABLE_CONTENT_LENGTH);
      break;
      // We can have: no transfer-encoding, no content length, and no
      // connection: close...
      // Unfortunately, this case doesn't seem to be covered in the spec.
      // We'll assume that the safest thing to do here is what the google
      // binaries before 2008 already do, which is to assume that
      // everything until the connection is closed is body.
    case BalsaHeadersEnums::NO_CONTENT_LENGTH:
      if (is_request_) {
        const absl::string_view method = headers_->request_method();
        // POSTs and PUTs should have a detectable body length.  If they
        // do not we consider it an error.
        if ((method != "POST" && method != "PUT") ||
            !http_validation_policy().require_content_length_if_body_required) {
          parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
          break;
        } else if (!allow_reading_until_close_for_request_) {
          HandleError(BalsaFrameEnums::REQUIRED_BODY_BUT_NO_CONTENT_LENGTH);
          break;
        }
      }
      parse_state_ = BalsaFrameEnums::READING_UNTIL_CLOSE;
      HandleWarning(BalsaFrameEnums::MAYBE_BODY_BUT_NO_CONTENT_LENGTH);
      break;
      // The COV_NF_... statements here provide hints to the apparatus
      // which computes coverage reports/ratios that this code is never
      // intended to be executed, and should technically be impossible.
      // COV_NF_START
    default:
      QUICHE_LOG(FATAL) << "Saw a content_length_status: "
                        << headers_->content_length_status_
                        << " which is unknown.";
      // COV_NF_END
  }
}

size_t BalsaFrame::ProcessHeaders(const char* message_start,
                                  size_t message_length) {
  const char* const original_message_start = message_start;
  const char* const message_end = message_start + message_length;
  const char* message_current = message_start;
  const char* checkpoint = message_start;

  if (message_length == 0) {
    return message_current - original_message_start;
  }

  while (message_current < message_end) {
    size_t base_idx = headers_->GetReadableBytesFromHeaderStream();

    // Yes, we could use strchr (assuming null termination), or
    // memchr, but as it turns out that is slower than this tight loop
    // for the input that we see.
    if (!saw_non_newline_char_) {
      do {
        const char c = *message_current;
        if (c != '\r' && c != '\n') {
          if (CHAR_LE(c, ' ')) {
            HandleError(BalsaFrameEnums::NO_REQUEST_LINE_IN_REQUEST);
            return message_current - original_message_start;
          }
          break;
        }
        ++message_current;
        if (message_current == message_end) {
          return message_current - original_message_start;
        }
      } while (true);
      saw_non_newline_char_ = true;
      message_start = message_current;
      checkpoint = message_current;
    }
    while (message_current < message_end) {
      if (*message_current != '\n') {
        ++message_current;
        continue;
      }
      const size_t relative_idx = message_current - message_start;
      const size_t message_current_idx = 1 + base_idx + relative_idx;
      lines_.push_back(std::make_pair(last_slash_n_idx_, message_current_idx));
      if (lines_.size() == 1) {
        headers_->WriteFromFramer(checkpoint, 1 + message_current - checkpoint);
        checkpoint = message_current + 1;
        const char* begin = headers_->OriginalHeaderStreamBegin();

        QUICHE_DVLOG(1) << "First line "
                        << std::string(begin, lines_[0].second);
        QUICHE_DVLOG(1) << "is_request_: " << is_request_;
        ProcessFirstLine(begin, begin + lines_[0].second);
        if (parse_state_ == BalsaFrameEnums::MESSAGE_FULLY_READ) {
          break;
        }

        if (parse_state_ == BalsaFrameEnums::ERROR) {
          return message_current - original_message_start;
        }
      }
      const size_t chars_since_last_slash_n =
          (message_current_idx - last_slash_n_idx_);
      last_slash_n_idx_ = message_current_idx;
      if (chars_since_last_slash_n > 2) {
        // false positive.
        ++message_current;
        continue;
      }
      if ((chars_since_last_slash_n == 1) ||
          (((message_current > message_start) &&
            (*(message_current - 1) == '\r')) ||
           (last_char_was_slash_r_))) {
        break;
      }
      ++message_current;
    }

    if (message_current == message_end) {
      continue;
    }

    ++message_current;
    QUICHE_DCHECK(message_current >= message_start);
    if (message_current > message_start) {
      headers_->WriteFromFramer(checkpoint, message_current - checkpoint);
    }

    // Check if we have exceeded maximum headers length
    // Although we check for this limit before and after we call this function
    // we check it here as well to make sure that in case the visitor changed
    // the max_header_length_ (for example after processing the first line)
    // we handle it gracefully.
    if (headers_->GetReadableBytesFromHeaderStream() > max_header_length_) {
      HandleHeadersTooLongError();
      return message_current - original_message_start;
    }

    // Since we know that we won't be writing any more bytes of the header,
    // we tell that to the headers object. The headers object may make
    // more efficient allocation decisions when this is signaled.
    headers_->DoneWritingFromFramer();
    visitor_->OnHeaderInput(headers_->GetReadablePtrFromHeaderStream());

    // Ok, now that we've written everything into our header buffer, it is
    // time to process the header lines (extract proper values for headers
    // which are important for framing).
    ProcessHeaderLines(lines_, false /*is_trailer*/, headers_);
    if (parse_state_ == BalsaFrameEnums::ERROR) {
      return message_current - original_message_start;
    }

    if (use_interim_headers_callback_ &&
        IsInterimResponse(headers_->parsed_response_code()) &&
        headers_->parsed_response_code() != kSwitchingProtocolsStatusCode) {
      // Deliver headers from this interim response but reset everything else to
      // prepare for the next set of headers. Skip 101 Switching Protocols
      // because these are considered final headers for the current protocol.
      visitor_->OnInterimHeaders(
          std::make_unique<BalsaHeaders>(std::move(*headers_)));
      Reset();
      checkpoint = message_start = message_current;
      continue;
    }
    if (continue_headers_ != nullptr &&
        headers_->parsed_response_code_ == kContinueStatusCode) {
      // Save the headers from this 100 Continue response but reset everything
      // else to prepare for the next set of headers.
      BalsaHeaders saved_continue_headers = std::move(*headers_);
      Reset();
      *continue_headers_ = std::move(saved_continue_headers);
      visitor_->ContinueHeaderDone();
      checkpoint = message_start = message_current;
      continue;
    }
    AssignParseStateAfterHeadersHaveBeenParsed();
    if (parse_state_ == BalsaFrameEnums::ERROR) {
      return message_current - original_message_start;
    }
    visitor_->ProcessHeaders(*headers_);
    visitor_->HeaderDone();
    if (parse_state_ == BalsaFrameEnums::MESSAGE_FULLY_READ) {
      visitor_->MessageDone();
    }
    return message_current - original_message_start;
  }
  // If we've gotten to here, it means that we've consumed all of the
  // available input. We need to record whether or not the last character we
  // saw was a '\r' so that a subsequent call to ProcessInput correctly finds
  // a header framing that is split across the two calls.
  last_char_was_slash_r_ = (*(message_end - 1) == '\r');
  QUICHE_DCHECK(message_current >= message_start);
  if (message_current > message_start) {
    headers_->WriteFromFramer(checkpoint, message_current - checkpoint);
  }
  return message_current - original_message_start;
}

size_t BalsaFrame::BytesSafeToSplice() const {
  switch (parse_state_) {
    case BalsaFrameEnums::READING_CHUNK_DATA:
      return chunk_length_remaining_;
    case BalsaFrameEnums::READING_UNTIL_CLOSE:
      return std::numeric_limits<size_t>::max();
    case BalsaFrameEnums::READING_CONTENT:
      return content_length_remaining_;
    default:
      return 0;
  }
}

void BalsaFrame::BytesSpliced(size_t bytes_spliced) {
  switch (parse_state_) {
    case BalsaFrameEnums::READING_CHUNK_DATA:
      if (chunk_length_remaining_ < bytes_spliced) {
        HandleError(BalsaFrameEnums::
                        CALLED_BYTES_SPLICED_AND_EXCEEDED_SAFE_SPLICE_AMOUNT);
        return;
      }
      chunk_length_remaining_ -= bytes_spliced;
      if (chunk_length_remaining_ == 0) {
        parse_state_ = BalsaFrameEnums::READING_CHUNK_TERM;
      }
      return;

    case BalsaFrameEnums::READING_UNTIL_CLOSE:
      return;

    case BalsaFrameEnums::READING_CONTENT:
      if (content_length_remaining_ < bytes_spliced) {
        HandleError(BalsaFrameEnums::
                        CALLED_BYTES_SPLICED_AND_EXCEEDED_SAFE_SPLICE_AMOUNT);
        return;
      }
      content_length_remaining_ -= bytes_spliced;
      if (content_length_remaining_ == 0) {
        parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
        visitor_->MessageDone();
      }
      return;

    default:
      HandleError(BalsaFrameEnums::CALLED_BYTES_SPLICED_WHEN_UNSAFE_TO_DO_SO);
      return;
  }
}

size_t BalsaFrame::ProcessInput(const char* input, size_t size) {
  const char* current = input;
  const char* on_entry = current;
  const char* end = current + size;

  QUICHE_DCHECK(headers_ != nullptr);
  if (headers_ == nullptr) {
    return 0;
  }

  if (parse_state_ == BalsaFrameEnums::READING_HEADER_AND_FIRSTLINE) {
    const size_t header_length = headers_->GetReadableBytesFromHeaderStream();
    // Yes, we still have to check this here as the user can change the
    // max_header_length amount!
    // Also it is possible that we have reached the maximum allowed header size,
    // and we have more to consume (remember we are still inside
    // READING_HEADER_AND_FIRSTLINE) in which case we directly declare an error.
    if (header_length > max_header_length_ ||
        (header_length == max_header_length_ && size > 0)) {
      HandleHeadersTooLongError();
      return current - input;
    }
    const size_t bytes_to_process =
        std::min(max_header_length_ - header_length, size);
    current += ProcessHeaders(input, bytes_to_process);
    // If we are still reading headers check if we have crossed the headers
    // limit. Note that we check for >= as opposed to >. This is because if
    // header_length_after equals max_header_length_ and we are still in the
    // parse_state_  BalsaFrameEnums::READING_HEADER_AND_FIRSTLINE we know for
    // sure that the headers limit will be crossed later on
    if (parse_state_ == BalsaFrameEnums::READING_HEADER_AND_FIRSTLINE) {
      // Note that headers_ is valid only if we are still reading headers.
      const size_t header_length_after =
          headers_->GetReadableBytesFromHeaderStream();
      if (header_length_after >= max_header_length_) {
        HandleHeadersTooLongError();
      }
    }
    return current - input;
  }

  if (parse_state_ == BalsaFrameEnums::MESSAGE_FULLY_READ ||
      parse_state_ == BalsaFrameEnums::ERROR) {
    // Can do nothing more 'till we're reset.
    return current - input;
  }

  QUICHE_DCHECK_LE(current, end);
  if (current == end) {
    return current - input;
  }

  while (true) {
    switch (parse_state_) {
      case BalsaFrameEnums::READING_CHUNK_LENGTH:
        // In this state we read the chunk length.
        // Note that once we hit a character which is not in:
        // [0-9;A-Fa-f\n], we transition to a different state.
        //
        QUICHE_DCHECK_LE(current, end);
        while (true) {
          if (current == end) {
            visitor_->OnRawBodyInput(
                absl::string_view(on_entry, current - on_entry));
            return current - input;
          }

          const char c = *current;
          ++current;

          static const signed char kBad = -1;
          static const signed char kDelimiter = -2;

          // valid cases:
          //  "09123\n"                      // -> 09123
          //  "09123\r\n"                    // -> 09123
          //  "09123  \n"                    // -> 09123
          //  "09123  \r\n"                  // -> 09123
          //  "09123  12312\n"               // -> 09123
          //  "09123  12312\r\n"             // -> 09123
          //  "09123; foo=bar\n"             // -> 09123
          //  "09123; foo=bar\r\n"           // -> 09123
          //  "FFFFFFFFFFFFFFFF\r\n"         // -> FFFFFFFFFFFFFFFF
          //  "FFFFFFFFFFFFFFFF 22\r\n"      // -> FFFFFFFFFFFFFFFF
          // invalid cases:
          // "[ \t]+[^\n]*\n"
          // "FFFFFFFFFFFFFFFFF\r\n"  (would overflow)
          // "\r\n"
          // "\n"
          signed char addition = kBad;
          // clang-format off
          switch (c) {
            case '0': addition = 0; break;
            case '1': addition = 1; break;
            case '2': addition = 2; break;
            case '3': addition = 3; break;
            case '4': addition = 4; break;
            case '5': addition = 5; break;
            case '6': addition = 6; break;
            case '7': addition = 7; break;
            case '8': addition = 8; break;
            case '9': addition = 9; break;
            case 'a': addition = 0xA; break;
            case 'b': addition = 0xB; break;
            case 'c': addition = 0xC; break;
            case 'd': addition = 0xD; break;
            case 'e': addition = 0xE; break;
            case 'f': addition = 0xF; break;
            case 'A': addition = 0xA; break;
            case 'B': addition = 0xB; break;
            case 'C': addition = 0xC; break;
            case 'D': addition = 0xD; break;
            case 'E': addition = 0xE; break;
            case 'F': addition = 0xF; break;
            case '\t':
            case '\n':
            case '\r':
            case ' ':
            case ';':
              addition = kDelimiter;
              break;
            default:
              // Leave addition == kBad
              break;
          }
          // clang-format on
          if (addition >= 0) {
            chunk_length_character_extracted_ = true;
            size_t length_x_16 = chunk_length_remaining_ * 16;
            const size_t kMaxDiv16 = std::numeric_limits<size_t>::max() / 16;
            if ((chunk_length_remaining_ > kMaxDiv16) ||
                (std::numeric_limits<size_t>::max() - length_x_16) <
                    static_cast<size_t>(addition)) {
              // overflow -- asked for a chunk-length greater than 2^64 - 1!!
              visitor_->OnRawBodyInput(
                  absl::string_view(on_entry, current - on_entry));
              HandleError(BalsaFrameEnums::CHUNK_LENGTH_OVERFLOW);
              return current - input;
            }
            chunk_length_remaining_ = length_x_16 + addition;
            continue;
          }

          if (!chunk_length_character_extracted_ || addition == kBad) {
            // ^[0-9;A-Fa-f][ \t\n] -- was not matched, either because no
            // characters were converted, or an unexpected character was
            // seen.
            visitor_->OnRawBodyInput(
                absl::string_view(on_entry, current - on_entry));
            HandleError(BalsaFrameEnums::INVALID_CHUNK_LENGTH);
            return current - input;
          }

          break;
        }

        --current;
        parse_state_ = BalsaFrameEnums::READING_CHUNK_EXTENSION;
        visitor_->OnChunkLength(chunk_length_remaining_);
        continue;

      case BalsaFrameEnums::READING_CHUNK_EXTENSION: {
        // TODO(phython): Convert this scanning to be 16 bytes at a time if
        // there is data to be read.
        const char* extensions_start = current;
        size_t extensions_length = 0;
        QUICHE_DCHECK_LE(current, end);
        while (true) {
          if (current == end) {
            visitor_->OnChunkExtensionInput(
                absl::string_view(extensions_start, extensions_length));
            visitor_->OnRawBodyInput(
                absl::string_view(on_entry, current - on_entry));
            return current - input;
          }
          const char c = *current;
          if (c == '\r' || c == '\n') {
            extensions_length = (extensions_start == current)
                                    ? 0
                                    : current - extensions_start - 1;
          }

          ++current;
          if (c == '\n') {
            break;
          }
        }

        chunk_length_character_extracted_ = false;
        visitor_->OnChunkExtensionInput(
            absl::string_view(extensions_start, extensions_length));

        if (chunk_length_remaining_ != 0) {
          parse_state_ = BalsaFrameEnums::READING_CHUNK_DATA;
          continue;
        }

        HeaderFramingFound('\n');
        parse_state_ = BalsaFrameEnums::READING_LAST_CHUNK_TERM;
        continue;
      }

      case BalsaFrameEnums::READING_CHUNK_DATA:
        while (current < end) {
          if (chunk_length_remaining_ == 0) {
            break;
          }
          // read in the chunk
          size_t bytes_remaining = end - current;
          size_t consumed_bytes = (chunk_length_remaining_ < bytes_remaining)
                                      ? chunk_length_remaining_
                                      : bytes_remaining;
          const char* tmp_current = current + consumed_bytes;
          visitor_->OnRawBodyInput(
              absl::string_view(on_entry, tmp_current - on_entry));
          visitor_->OnBodyChunkInput(
              absl::string_view(current, consumed_bytes));
          on_entry = current = tmp_current;
          chunk_length_remaining_ -= consumed_bytes;
        }

        if (chunk_length_remaining_ == 0) {
          parse_state_ = BalsaFrameEnums::READING_CHUNK_TERM;
          continue;
        }

        visitor_->OnRawBodyInput(
            absl::string_view(on_entry, current - on_entry));
        return current - input;

      case BalsaFrameEnums::READING_CHUNK_TERM:
        QUICHE_DCHECK_LE(current, end);
        while (true) {
          if (current == end) {
            visitor_->OnRawBodyInput(
                absl::string_view(on_entry, current - on_entry));
            return current - input;
          }

          const char c = *current;
          ++current;

          if (c == '\n') {
            break;
          }
        }
        parse_state_ = BalsaFrameEnums::READING_CHUNK_LENGTH;
        continue;

      case BalsaFrameEnums::READING_LAST_CHUNK_TERM:
        QUICHE_DCHECK_LE(current, end);
        while (true) {
          if (current == end) {
            visitor_->OnRawBodyInput(
                absl::string_view(on_entry, current - on_entry));
            return current - input;
          }

          const char c = *current;
          if (HeaderFramingFound(c) != 0) {
            // If we've found a "\r\n\r\n", then the message
            // is done.
            ++current;
            parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
            visitor_->OnRawBodyInput(
                absl::string_view(on_entry, current - on_entry));
            visitor_->MessageDone();
            return current - input;
          }

          // If not, however, since the spec only suggests that the
          // client SHOULD indicate the presence of trailers, we get to
          // *test* that they did or didn't.
          // If all of the bytes we've seen since:
          //   OPTIONAL_WS 0 OPTIONAL_STUFF CRLF
          // are either '\r', or '\n', then we can assume that we don't yet
          // know if we need to parse headers, or if the next byte will make
          // the HeaderFramingFound condition (above) true.
          if (!HeaderFramingMayBeFound()) {
            break;
          }

          // If HeaderFramingMayBeFound(), then we have seen only characters
          // '\r' or '\n'.
          ++current;

          // Lets try again! There is no state change here.
        }

        // If (!HeaderFramingMayBeFound()), then we know that we must be
        // reading the first non CRLF character of a trailer.
        parse_state_ = BalsaFrameEnums::READING_TRAILER;
        visitor_->OnRawBodyInput(
            absl::string_view(on_entry, current - on_entry));
        on_entry = current;
        continue;

      // TODO(yongfa): No leading whitespace is allowed before field-name per
      // RFC2616. Leading whitespace will cause header parsing error too.
      case BalsaFrameEnums::READING_TRAILER:
        while (current < end) {
          const char c = *current;
          ++current;
          ++trailer_length_;
          if (trailers_ != nullptr) {
            // Reuse the header length limit for trailer, which is just a bunch
            // of headers.
            if (trailer_length_ > max_header_length_) {
              --current;
              HandleError(BalsaFrameEnums::TRAILER_TOO_LONG);
              return current - input;
            }
            if (LineFramingFound(c)) {
              trailer_lines_.push_back(
                  std::make_pair(start_of_trailer_line_, trailer_length_));
              start_of_trailer_line_ = trailer_length_;
            }
          }
          if (HeaderFramingFound(c) != 0) {
            parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
            if (trailers_ != nullptr) {
              trailers_->WriteFromFramer(on_entry, current - on_entry);
              trailers_->DoneWritingFromFramer();
              ProcessHeaderLines(trailer_lines_, true /*is_trailer*/,
                                 trailers_.get());
              if (parse_state_ == BalsaFrameEnums::ERROR) {
                return current - input;
              }
              visitor_->OnTrailers(std::move(trailers_));

              // Allows trailers to be delivered without another call to
              // EnableTrailers() in case the framer is Reset().
              trailers_ = std::make_unique<BalsaHeaders>();
            }
            visitor_->OnTrailerInput(
                absl::string_view(on_entry, current - on_entry));
            visitor_->MessageDone();
            return current - input;
          }
        }
        if (trailers_ != nullptr) {
          trailers_->WriteFromFramer(on_entry, current - on_entry);
        }
        visitor_->OnTrailerInput(
            absl::string_view(on_entry, current - on_entry));
        return current - input;

      case BalsaFrameEnums::READING_UNTIL_CLOSE: {
        const size_t bytes_remaining = end - current;
        if (bytes_remaining > 0) {
          visitor_->OnRawBodyInput(absl::string_view(current, bytes_remaining));
          visitor_->OnBodyChunkInput(
              absl::string_view(current, bytes_remaining));
          current += bytes_remaining;
        }
        return current - input;
      }

      case BalsaFrameEnums::READING_CONTENT:
        while ((content_length_remaining_ != 0u) && current < end) {
          // read in the content
          const size_t bytes_remaining = end - current;
          const size_t consumed_bytes =
              (content_length_remaining_ < bytes_remaining)
                  ? content_length_remaining_
                  : bytes_remaining;
          visitor_->OnRawBodyInput(absl::string_view(current, consumed_bytes));
          visitor_->OnBodyChunkInput(
              absl::string_view(current, consumed_bytes));
          current += consumed_bytes;
          content_length_remaining_ -= consumed_bytes;
        }
        if (content_length_remaining_ == 0) {
          parse_state_ = BalsaFrameEnums::MESSAGE_FULLY_READ;
          visitor_->MessageDone();
        }
        return current - input;

      default:
        // The state-machine should never be in a state that isn't handled
        // above.  This is a glaring logic error, and we should do something
        // drastic to ensure that this gets looked-at and fixed.
        QUICHE_LOG(FATAL) << "Unknown state: " << parse_state_  // COV_NF_LINE
                          << " memory corruption?!";            // COV_NF_LINE
    }
  }
}

void BalsaFrame::HandleHeadersTooLongError() {
  if (parse_truncated_headers_even_when_headers_too_long_) {
    const size_t len = headers_->GetReadableBytesFromHeaderStream();
    const char* stream_begin = headers_->OriginalHeaderStreamBegin();

    if (last_slash_n_idx_ < len && stream_begin[last_slash_n_idx_] != '\r') {
      // We write an end to the truncated line, and a blank line to end the
      // headers, to end up with something that will parse.
      static const absl::string_view kTwoLineEnds = "\r\n\r\n";
      headers_->WriteFromFramer(kTwoLineEnds.data(), kTwoLineEnds.size());

      // This is the last, truncated line.
      lines_.push_back(std::make_pair(last_slash_n_idx_, len + 2));
      // A blank line to end the headers.
      lines_.push_back(std::make_pair(len + 2, len + 4));
    }

    ProcessHeaderLines(lines_, /*is_trailer=*/false, headers_);
  }

  HandleError(BalsaFrameEnums::HEADERS_TOO_LONG);
}

const int32_t BalsaFrame::kValidTerm1;
const int32_t BalsaFrame::kValidTerm1Mask;
const int32_t BalsaFrame::kValidTerm2;
const int32_t BalsaFrame::kValidTerm2Mask;

}  // namespace quiche

#undef CHAR_LT
#undef CHAR_LE
#undef CHAR_GT
#undef CHAR_GE
#undef QUICHE_DCHECK_CHAR_GE
