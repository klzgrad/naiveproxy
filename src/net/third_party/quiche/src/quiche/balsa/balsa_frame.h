// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_BALSA_FRAME_H_
#define QUICHE_BALSA_BALSA_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "quiche/balsa/balsa_enums.h"
#include "quiche/balsa/balsa_headers.h"
#include "quiche/balsa/balsa_visitor_interface.h"
#include "quiche/balsa/framer_interface.h"
#include "quiche/balsa/http_validation_policy.h"
#include "quiche/balsa/noop_balsa_visitor.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"

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

  enum class InvalidCharsLevel { kOff, kWarning, kError };

  // TODO(fenix): get rid of the 'kValidTerm*' stuff by using the 'since last
  // index' strategy.  Note that this implies getting rid of the HeaderFramed()

  static constexpr int32_t kValidTerm1 = '\n' << 16 | '\r' << 8 | '\n';
  static constexpr int32_t kValidTerm1Mask = 0xFF << 16 | 0xFF << 8 | 0xFF;
  static constexpr int32_t kValidTerm2 = '\n' << 8 | '\n';
  static constexpr int32_t kValidTerm2Mask = 0xFF << 8 | 0xFF;
  BalsaFrame()
      : last_char_was_slash_r_(false),
        saw_non_newline_char_(false),
        start_was_space_(true),
        chunk_length_character_extracted_(false),
        is_request_(true),
        allow_reading_until_close_for_request_(false),
        request_was_head_(false),
        max_header_length_(16 * 1024),
        visitor_(&do_nothing_visitor_),
        chunk_length_remaining_(0),
        content_length_remaining_(0),
        last_slash_n_loc_(nullptr),
        last_recorded_slash_n_loc_(nullptr),
        last_slash_n_idx_(0),
        term_chars_(0),
        parse_state_(BalsaFrameEnums::READING_HEADER_AND_FIRSTLINE),
        last_error_(BalsaFrameEnums::BALSA_NO_ERROR),
        continue_headers_(nullptr),
        headers_(nullptr),
        start_of_trailer_line_(0),
        trailer_length_(0),
        trailer_(nullptr),
        invalid_chars_level_(InvalidCharsLevel::kOff) {}

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

  // The method set_balsa_trailer() clears `trailer` and attaches it to the
  // framer.  This is a required step before the framer will process any input
  // message data.  To detach the trailer object from the framer, use
  // set_balsa_trailer(nullptr).
  void set_balsa_trailer(BalsaHeaders* trailer) {
    if (trailer != nullptr && is_request()) {
      QUICHE_CODE_COUNT(balsa_trailer_in_request);
    }

    if (trailer_ != trailer) {
      trailer_ = trailer;
    }
    if (trailer_ != nullptr) {
      // Clear the trailer if it is non-null, even if the new trailer is
      // the same as the old.
      trailer_->Clear();
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

  bool track_invalid_chars() {
    return invalid_chars_level_ != InvalidCharsLevel::kOff;
  }

  bool invalid_chars_error_enabled() {
    return invalid_chars_level_ == InvalidCharsLevel::kError;
  }

  void set_http_validation_policy(const quiche::HttpValidationPolicy& policy) {
    http_validation_policy_ = policy;
  }
  const quiche::HttpValidationPolicy& http_validation_policy() const {
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

  const absl::flat_hash_map<char, int>& get_invalid_chars() const {
    return invalid_chars_;
  }

  const BalsaHeaders* headers() const { return headers_; }
  BalsaHeaders* mutable_headers() { return headers_; }

  const BalsaHeaders* trailer() const { return trailer_; }
  BalsaHeaders* mutable_trailer() { return trailer_; }

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

 protected:
  inline BalsaHeadersEnums::ContentLengthStatus ProcessContentLengthLine(
      size_t line_idx, size_t* length);

  inline void ProcessTransferEncodingLine(size_t line_idx);

  void ProcessFirstLine(const char* begin, const char* end);

  void CleanUpKeyValueWhitespace(const char* stream_begin,
                                 const char* line_begin, const char* current,
                                 const char* line_end,
                                 HeaderLineDescription* current_header_line);

  void ProcessHeaderLines(const Lines& lines, bool is_trailer,
                          BalsaHeaders* headers);

  // Returns true if there are invalid characters, false otherwise.
  // Will also update counts per invalid character in invalid_chars_.
  bool CheckHeaderLinesForInvalidChars(const Lines& lines,
                                       const BalsaHeaders* headers);

  inline size_t ProcessHeaders(const char* message_start,
                               size_t message_length);

  void AssignParseStateAfterHeadersHaveBeenParsed();

  inline bool LineFramingFound(char current_char) {
    return current_char == '\n';
  }

  // TODO(fenix): get rid of the following function and its uses (and
  // replace with something more efficient).
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

  bool last_char_was_slash_r_;
  bool saw_non_newline_char_;
  bool start_was_space_;
  bool chunk_length_character_extracted_;
  bool is_request_;  // This is not reset in Reset()
  // Generally, requests are not allowed to frame with connection: close.  For
  // protocols which do their own protocol-specific chunking, such as streamed
  // stubby, we allow connection close semantics for requests.
  bool allow_reading_until_close_for_request_;
  bool request_was_head_;     // This is not reset in Reset()
  size_t max_header_length_;  // This is not reset in Reset()
  BalsaVisitorInterface* visitor_;
  size_t chunk_length_remaining_;
  size_t content_length_remaining_;
  const char* last_slash_n_loc_;
  const char* last_recorded_slash_n_loc_;
  size_t last_slash_n_idx_;
  uint32_t term_chars_;
  BalsaFrameEnums::ParseState parse_state_;
  BalsaFrameEnums::ErrorCode last_error_;
  absl::flat_hash_map<char, int> invalid_chars_;

  Lines lines_;

  BalsaHeaders* continue_headers_;  // This is not reset to nullptr in Reset().
  BalsaHeaders* headers_;           // This is not reset to nullptr in Reset().
  NoOpBalsaVisitor do_nothing_visitor_;

  Lines trailer_lines_;
  size_t start_of_trailer_line_;
  size_t trailer_length_;
  BalsaHeaders* trailer_;  // Does not own and is not reset to nullptr
                           // in Reset().
  InvalidCharsLevel invalid_chars_level_;  // This is not reset in Reset()

  quiche::HttpValidationPolicy http_validation_policy_;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_BALSA_FRAME_H_
