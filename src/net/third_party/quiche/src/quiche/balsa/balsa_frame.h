// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_BALSA_FRAME_H_
#define QUICHE_BALSA_BALSA_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "quiche/balsa/balsa_enums.h"
#include "quiche/balsa/balsa_headers.h"
#include "quiche/balsa/balsa_visitor_interface.h"
#include "quiche/balsa/framer_interface.h"
#include "quiche/balsa/http_validation_policy.h"
#include "quiche/balsa/noop_balsa_visitor.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

namespace test {
class BalsaFrameTestPeer;
}  // namespace test

// BalsaFrame is a lightweight HTTP framer.
class QUICHE_EXPORT BalsaFrame : public FramerInterface {
 public:
  typedef std::vector<std::pair<size_t, size_t> > Lines;

  typedef BalsaHeaders::HeaderLineDescription HeaderLineDescription;
  typedef BalsaHeaders::HeaderLines HeaderLines;
  typedef BalsaHeaders::HeaderTokenList HeaderTokenList;

  enum class InvalidCharsLevel : uint8_t { kOff, kError };

  static constexpr int32_t kValidTerm1 = '\n' << 16 | '\r' << 8 | '\n';
  static constexpr int32_t kValidTerm1Mask = 0xFF << 16 | 0xFF << 8 | 0xFF;
  static constexpr int32_t kValidTerm2 = '\n' << 8 | '\n';
  static constexpr int32_t kValidTerm2Mask = 0xFF << 8 | 0xFF;
  BalsaFrame()
      : visitor_(&do_nothing_visitor_),
        continue_headers_(nullptr),
        headers_(nullptr),
        max_header_length_(16 * 1024),
        start_of_trailer_line_(0),
        trailer_length_(0),
        chunk_length_remaining_(0),
        content_length_remaining_(0),
        last_slash_n_idx_(0),
        term_chars_(0),
        parse_state_(BalsaFrameEnums::READING_HEADER_AND_FIRSTLINE),
        last_error_(BalsaFrameEnums::BALSA_NO_ERROR),
        invalid_chars_level_(InvalidCharsLevel::kOff),
        last_char_was_slash_r_(false),
        saw_non_newline_char_(false),
        start_was_space_(true),
        chunk_length_character_extracted_(false),
        is_request_(true),
        allow_reading_until_close_for_request_(false),
        request_was_head_(false),
        is_valid_target_uri_(true),
        use_interim_headers_callback_(false),
        parse_truncated_headers_even_when_headers_too_long_(false) {}

  ~BalsaFrame() override {}

  // Reset reinitializes all the member variables of the framer and clears the
  // attached header object (but doesn't change the pointer value headers_).
  void Reset();

  // The method set_balsa_headers clears the headers provided and attaches them
  // to the framer.  This is a required step before the framer will process any
  // input message data.
  // To detach the header object from the framer, use
  // set_balsa_headers(nullptr).
  void set_balsa_headers(BalsaHeaders* headers) {
    if (headers_ != headers) {
      headers_ = headers;
    }
    if (headers_ != nullptr) {
      // Clear the headers if they are non-null, even if the new headers are
      // the same as the old.
      headers_->Clear();
    }
  }

  // If set to non-null, allow 100 Continue headers before the main headers.
  // This method is a no-op if set_use_interim_headers_callback(true) is called.
  void set_continue_headers(BalsaHeaders* continue_headers) {
    if (continue_headers_ != continue_headers) {
      continue_headers_ = continue_headers;
    }
    if (continue_headers_ != nullptr) {
      // Clear the headers if they are non-null, even if the new headers are
      // the same as the old.
      continue_headers_->Clear();
    }
  }

  // Enables the framer to process trailers and deliver them in
  // `BalsaVisitorInterface::OnTrailers()`. If this method is not called and
  // trailers are received, only minimal trailers parsing will be performed
  // (just enough to advance past trailers).
  void EnableTrailers() {
    if (is_request()) {
      QUICHE_CODE_COUNT(balsa_trailer_in_request);
    }
    if (trailers_ == nullptr) {
      trailers_ = std::make_unique<BalsaHeaders>();
    }
  }

  void set_balsa_visitor(BalsaVisitorInterface* visitor) {
    visitor_ = visitor;
    if (visitor_ == nullptr) {
      visitor_ = &do_nothing_visitor_;
    }
  }

  void set_invalid_chars_level(InvalidCharsLevel v) {
    invalid_chars_level_ = v;
  }

  bool invalid_chars_error_enabled() {
    return invalid_chars_level_ == InvalidCharsLevel::kError;
  }

  void set_http_validation_policy(const HttpValidationPolicy& policy) {
    http_validation_policy_ = policy;
  }
  const HttpValidationPolicy& http_validation_policy() const {
    return http_validation_policy_;
  }

  void set_is_request(bool is_request) { is_request_ = is_request; }

  bool is_request() const { return is_request_; }

  void set_request_was_head(bool request_was_head) {
    request_was_head_ = request_was_head;
  }

  void set_max_header_length(size_t max_header_length) {
    max_header_length_ = max_header_length;
  }

  size_t max_header_length() const { return max_header_length_; }

  bool MessageFullyRead() const {
    return parse_state_ == BalsaFrameEnums::MESSAGE_FULLY_READ;
  }

  BalsaFrameEnums::ParseState ParseState() const { return parse_state_; }

  bool Error() const { return parse_state_ == BalsaFrameEnums::ERROR; }

  BalsaFrameEnums::ErrorCode ErrorCode() const { return last_error_; }

  const BalsaHeaders* headers() const { return headers_; }
  BalsaHeaders* mutable_headers() { return headers_; }

  size_t BytesSafeToSplice() const;
  void BytesSpliced(size_t bytes_spliced);

  size_t ProcessInput(const char* input, size_t size) override;

  void set_allow_reading_until_close_for_request(bool set) {
    allow_reading_until_close_for_request_ = set;
  }

  // For websockets and possibly other uses, we suspend the usual expectations
  // about when a message has a body and how long it should be.
  void AllowArbitraryBody() {
    parse_state_ = BalsaFrameEnums::READING_UNTIL_CLOSE;
  }

  // If enabled, calls BalsaVisitorInterface::OnInterimHeaders() when parsing
  // interim headers. For 100 Continue, this callback will be invoked instead of
  // ContinueHeaderDone(), even when set_continue_headers() is called.
  void set_use_interim_headers_callback(bool set) {
    use_interim_headers_callback_ = set;
  }

  // If enabled, parse the available portion of headers even on a
  // HEADERS_TOO_LONG error, so that that portion of headers is available to the
  // error handler. Generally results in the last header being truncated.
  void set_parse_truncated_headers_even_when_headers_too_long(bool set) {
    parse_truncated_headers_even_when_headers_too_long_ = set;
  }

  bool is_valid_target_uri() const { return is_valid_target_uri_; }

 protected:
  inline BalsaHeadersEnums::ContentLengthStatus ProcessContentLengthLine(
      size_t line_idx, size_t* length);

  inline void ProcessTransferEncodingLine(size_t line_idx);

  void ProcessFirstLine(char* begin, char* end);

  void CleanUpKeyValueWhitespace(const char* stream_begin,
                                 const char* line_begin, const char* current,
                                 const char* line_end,
                                 HeaderLineDescription* current_header_line);

  void ProcessHeaderLines(const Lines& lines, bool is_trailer,
                          BalsaHeaders* headers);

  // Returns true if there are invalid characters, false otherwise.
  bool CheckHeaderLinesForInvalidChars(const Lines& lines,
                                       const BalsaHeaders* headers);

  inline size_t ProcessHeaders(const char* message_start,
                               size_t message_length);

  void AssignParseStateAfterHeadersHaveBeenParsed();

  inline bool LineFramingFound(char current_char) {
    return current_char == '\n';
  }

  // Return header framing pattern. Non-zero return value indicates found,
  // which has two possible outcomes: kValidTerm1, which means \n\r\n
  // or kValidTerm2, which means \n\n. Zero return value means not found.
  inline int32_t HeaderFramingFound(char current_char) {
    // Note that the 'if (current_char == '\n' ...)' test exists to ensure that
    // the HeaderFramingMayBeFound test works properly. In benchmarking done on
    // 2/13/2008, the 'if' actually speeds up performance of the function
    // anyway..
    if (current_char == '\n' || current_char == '\r') {
      term_chars_ <<= 8;
      // This is necessary IFF architecture has > 8 bit char.  Alas, I'm
      // paranoid.
      term_chars_ |= current_char & 0xFF;

      if ((term_chars_ & kValidTerm1Mask) == kValidTerm1) {
        term_chars_ = 0;
        return kValidTerm1;
      }
      if ((term_chars_ & kValidTerm2Mask) == kValidTerm2) {
        term_chars_ = 0;
        return kValidTerm2;
      }
    } else {
      term_chars_ = 0;
    }
    return 0;
  }

  inline bool HeaderFramingMayBeFound() const { return term_chars_ != 0; }

 private:
  friend class test::BalsaFrameTestPeer;

  // Calls HandleError() and returns false on error.
  bool FindColonsAndParseIntoKeyValue(const Lines& lines, bool is_trailer,
                                      BalsaHeaders* headers);

  void HandleError(BalsaFrameEnums::ErrorCode error_code);
  void HandleWarning(BalsaFrameEnums::ErrorCode error_code);

  void HandleHeadersTooLongError();

  BalsaVisitorInterface* visitor_;
  BalsaHeaders* continue_headers_;  // This is not reset to nullptr in Reset().
  BalsaHeaders* headers_;           // This is not reset to nullptr in Reset().
  NoOpBalsaVisitor do_nothing_visitor_;
  // Cleared but not reset to nullptr in Reset().
  std::unique_ptr<BalsaHeaders> trailers_;

  Lines lines_;
  Lines trailer_lines_;

  size_t max_header_length_;  // This is not reset in Reset()

  size_t start_of_trailer_line_;
  size_t trailer_length_;

  size_t chunk_length_remaining_;
  size_t content_length_remaining_;
  size_t last_slash_n_idx_;
  uint32_t term_chars_;
  BalsaFrameEnums::ParseState parse_state_;
  BalsaFrameEnums::ErrorCode last_error_;

  InvalidCharsLevel invalid_chars_level_;  // This is not reset in Reset().

  HttpValidationPolicy http_validation_policy_;

  bool last_char_was_slash_r_ : 1;
  bool saw_non_newline_char_ : 1;
  bool start_was_space_ : 1;
  bool chunk_length_character_extracted_ : 1;
  bool is_request_ : 1;  // This is not reset in Reset()
  // Generally, requests are not allowed to frame with connection: close.  For
  // protocols which do their own protocol-specific chunking, such as streamed
  // stubby, we allow connection close semantics for requests.
  bool allow_reading_until_close_for_request_ : 1;
  bool request_was_head_ : 1;     // This is not reset in Reset()
  bool is_valid_target_uri_ : 1;  // False if the target URI was invalid.
  // This is not reset in Reset().
  // TODO(b/68801833): Default-enable and then deprecate this field, along with
  // set_continue_headers().
  bool use_interim_headers_callback_ : 1;

  // This is not reset in Reset().
  bool parse_truncated_headers_even_when_headers_too_long_ : 1;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_BALSA_FRAME_H_
