// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_TOKENIZER_H_
#define TOOLS_GN_TOKENIZER_H_

#include <stddef.h>

#include <vector>

#include "base/macros.h"
#include "base/strings/string_piece.h"
#include "tools/gn/err.h"
#include "tools/gn/token.h"

class InputFile;

class Tokenizer {
 public:
  static std::vector<Token> Tokenize(const InputFile* input_file, Err* err);

  // Counts lines in the given buffer (the first line is "1") and returns
  // the byte offset of the beginning of that line, or (size_t)-1 if there
  // aren't that many lines in the file. Note that this will return the byte
  // one past the end of the input if the last character is a newline.
  //
  // This is a helper function for error output so that the tokenizer's
  // notion of lines can be used elsewhere.
  static size_t ByteOffsetOfNthLine(const base::StringPiece& buf, int n);

  // Returns true if the given offset of the string piece counts as a newline.
  // The offset must be in the buffer.
  static bool IsNewline(const base::StringPiece& buffer, size_t offset);

  static bool IsIdentifierFirstChar(char c);

  static bool IsIdentifierContinuingChar(char c);

 private:
  // InputFile must outlive the tokenizer and all generated tokens.
  Tokenizer(const InputFile* input_file, Err* err);
  ~Tokenizer();

  std::vector<Token> Run();

  void AdvanceToNextToken();
  Token::Type ClassifyCurrent() const;
  void AdvanceToEndOfToken(const Location& location, Token::Type type);

  // Whether from this location back to the beginning of the line is only
  // whitespace. |location| should be the first character of the token to be
  // checked.
  bool AtStartOfLine(size_t location) const;

  bool IsCurrentWhitespace() const;
  bool IsCurrentNewline() const;
  bool IsCurrentStringTerminator(char quote_char) const;

  bool CanIncrement() const { return cur_ < input_.size(); }

  // Increments the current location by one.
  void Advance();

  // Returns the current character in the file as a location.
  Location GetCurrentLocation() const;

  Err GetErrorForInvalidToken(const Location& location) const;

  bool done() const { return at_end() || has_error(); }

  bool at_end() const { return cur_ == input_.size(); }
  char cur_char() const { return input_[cur_]; }

  bool has_error() const { return err_->has_error(); }

  std::vector<Token> tokens_;

  const InputFile* input_file_;
  const base::StringPiece input_;
  Err* err_;
  size_t cur_;  // Byte offset into input buffer.

  int line_number_;
  int column_number_;

  DISALLOW_COPY_AND_ASSIGN(Tokenizer);
};

#endif  // TOOLS_GN_TOKENIZER_H_
