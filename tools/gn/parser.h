// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_PARSER_H_
#define TOOLS_GN_PARSER_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "tools/gn/err.h"
#include "tools/gn/parse_tree.h"

class Parser;
typedef std::unique_ptr<ParseNode> (Parser::*PrefixFunc)(const Token& token);
typedef std::unique_ptr<ParseNode> (
    Parser::*InfixFunc)(std::unique_ptr<ParseNode> left, const Token& token);

extern const char kGrammar_Help[];

struct ParserHelper {
  PrefixFunc prefix;
  InfixFunc infix;
  int precedence;
};

// Parses a series of tokens. The resulting AST will refer to the tokens passed
// to the input, so the tokens an the file data they refer to must outlive your
// use of the ParseNode.
class Parser {
 public:
  // Will return a null pointer and set the err on error.
  static std::unique_ptr<ParseNode> Parse(const std::vector<Token>& tokens,
                                          Err* err);

  // Alternative to parsing that assumes the input is an expression.
  static std::unique_ptr<ParseNode> ParseExpression(
      const std::vector<Token>& tokens,
      Err* err);

  // Alternative to parsing that assumes the input is a literal value.
  static std::unique_ptr<ParseNode> ParseValue(const std::vector<Token>& tokens,
                                               Err* err);

 private:
  // Vector must be valid for lifetime of call.
  Parser(const std::vector<Token>& tokens, Err* err);
  ~Parser();

  std::unique_ptr<ParseNode> ParseExpression();

  // Parses an expression with the given precedence or higher.
  std::unique_ptr<ParseNode> ParseExpression(int precedence);

  // |PrefixFunc|s used in parsing expressions.
  std::unique_ptr<ParseNode> Block(const Token& token);
  std::unique_ptr<ParseNode> Literal(const Token& token);
  std::unique_ptr<ParseNode> Name(const Token& token);
  std::unique_ptr<ParseNode> Group(const Token& token);
  std::unique_ptr<ParseNode> Not(const Token& token);
  std::unique_ptr<ParseNode> List(const Token& token);
  std::unique_ptr<ParseNode> BlockComment(const Token& token);

  // |InfixFunc|s used in parsing expressions.
  std::unique_ptr<ParseNode> BinaryOperator(std::unique_ptr<ParseNode> left,
                                            const Token& token);
  std::unique_ptr<ParseNode> IdentifierOrCall(std::unique_ptr<ParseNode> left,
                                              const Token& token);
  std::unique_ptr<ParseNode> Assignment(std::unique_ptr<ParseNode> left,
                                        const Token& token);
  std::unique_ptr<ParseNode> Subscript(std::unique_ptr<ParseNode> left,
                                       const Token& token);
  std::unique_ptr<ParseNode> DotOperator(std::unique_ptr<ParseNode> left,
                                         const Token& token);

  // Helper to parse a comma separated list, optionally allowing trailing
  // commas (allowed in [] lists, not in function calls).
  std::unique_ptr<ListNode> ParseList(const Token& start_token,
                                      Token::Type stop_before,
                                      bool allow_trailing_comma);

  std::unique_ptr<ParseNode> ParseFile();
  std::unique_ptr<ParseNode> ParseStatement();
  // Expects to be passed the token corresponding to the '{' and that the
  // current token is the one following the '{'.
  std::unique_ptr<BlockNode> ParseBlock(const Token& begin_brace,
                                        BlockNode::ResultMode result_mode);
  std::unique_ptr<ParseNode> ParseCondition();

  // Generates a pre- and post-order traversal of the tree.
  void TraverseOrder(const ParseNode* root,
                     std::vector<const ParseNode*>* pre,
                     std::vector<const ParseNode*>* post);

  // Attach comments to nearby syntax.
  void AssignComments(ParseNode* file);

  bool IsAssignment(const ParseNode* node) const;
  bool IsStatementBreak(Token::Type token_type) const;

  bool LookAhead(Token::Type type);
  bool Match(Token::Type type);
  const Token& Consume(Token::Type type, const char* error_message);
  const Token& Consume(Token::Type* types,
                       size_t num_types,
                       const char* error_message);
  const Token& Consume();

  // Call this only if !at_end().
  const Token& cur_token() const { return tokens_[cur_]; }

  const Token& cur_or_last_token() const {
    return at_end() ? tokens_[tokens_.size() - 1] : cur_token();
  }

  bool done() const { return at_end() || has_error(); }
  bool at_end() const { return cur_ >= tokens_.size(); }
  bool has_error() const { return err_->has_error(); }

  std::vector<Token> tokens_;
  std::vector<Token> line_comment_tokens_;
  std::vector<Token> suffix_comment_tokens_;

  static ParserHelper expressions_[Token::NUM_TYPES];

  Token invalid_token_;
  Err* err_;

  // Current index into the tokens.
  size_t cur_;

  FRIEND_TEST_ALL_PREFIXES(Parser, BinaryOp);
  FRIEND_TEST_ALL_PREFIXES(Parser, Block);
  FRIEND_TEST_ALL_PREFIXES(Parser, Condition);
  FRIEND_TEST_ALL_PREFIXES(Parser, Expression);
  FRIEND_TEST_ALL_PREFIXES(Parser, FunctionCall);
  FRIEND_TEST_ALL_PREFIXES(Parser, List);
  FRIEND_TEST_ALL_PREFIXES(Parser, ParenExpression);
  FRIEND_TEST_ALL_PREFIXES(Parser, UnaryOp);

  DISALLOW_COPY_AND_ASSIGN(Parser);
};

#endif  // TOOLS_GN_PARSER_H_
