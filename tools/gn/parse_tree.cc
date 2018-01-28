// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/parse_tree.h"

#include <stdint.h>

#include <string>
#include <tuple>

#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "tools/gn/functions.h"
#include "tools/gn/operators.h"
#include "tools/gn/scope.h"
#include "tools/gn/string_utils.h"

namespace {

enum DepsCategory {
  DEPS_CATEGORY_LOCAL,
  DEPS_CATEGORY_RELATIVE,
  DEPS_CATEGORY_ABSOLUTE,
  DEPS_CATEGORY_OTHER,
};

DepsCategory GetDepsCategory(base::StringPiece deps) {
  if (deps.length() < 2 || deps[0] != '"' || deps[deps.size() - 1] != '"')
    return DEPS_CATEGORY_OTHER;

  if (deps[1] == ':')
    return DEPS_CATEGORY_LOCAL;

  if (deps[1] == '/')
    return DEPS_CATEGORY_ABSOLUTE;

  return DEPS_CATEGORY_RELATIVE;
}

std::tuple<base::StringPiece, base::StringPiece> SplitAtFirst(
    base::StringPiece str,
    char c) {
  if (!str.starts_with("\"") || !str.ends_with("\""))
    return std::make_tuple(str, base::StringPiece());

  str = str.substr(1, str.length() - 2);
  size_t index_of_first = str.find(c);
  return std::make_tuple(str.substr(0, index_of_first),
                         index_of_first != base::StringPiece::npos
                             ? str.substr(index_of_first + 1)
                             : base::StringPiece());
}

std::string IndentFor(int value) {
  return std::string(value, ' ');
}

bool IsSortRangeSeparator(const ParseNode* node, const ParseNode* prev) {
  // If it's a block comment, or has an attached comment with a blank line
  // before it, then we break the range at this point.
  return node->AsBlockComment() != nullptr ||
         (prev && node->comments() && !node->comments()->before().empty() &&
          (node->GetRange().begin().line_number() >
           prev->GetRange().end().line_number() +
               static_cast<int>(node->comments()->before().size() + 1)));
}

base::StringPiece GetStringRepresentation(const ParseNode* node) {
  DCHECK(node->AsLiteral() || node->AsIdentifier() || node->AsAccessor());
  if (node->AsLiteral())
    return node->AsLiteral()->value().value();
  else if (node->AsIdentifier())
    return node->AsIdentifier()->value().value();
  else if (node->AsAccessor())
    return node->AsAccessor()->base().value();
  return base::StringPiece();
}

}  // namespace

Comments::Comments() {
}

Comments::~Comments() {
}

void Comments::ReverseSuffix() {
  for (int i = 0, j = static_cast<int>(suffix_.size() - 1); i < j; ++i, --j)
    std::swap(suffix_[i], suffix_[j]);
}

ParseNode::ParseNode() {
}

ParseNode::~ParseNode() {
}

const AccessorNode* ParseNode::AsAccessor() const { return nullptr; }
const BinaryOpNode* ParseNode::AsBinaryOp() const { return nullptr; }
const BlockCommentNode* ParseNode::AsBlockComment() const { return nullptr; }
const BlockNode* ParseNode::AsBlock() const { return nullptr; }
const ConditionNode* ParseNode::AsConditionNode() const { return nullptr; }
const EndNode* ParseNode::AsEnd() const { return nullptr; }
const FunctionCallNode* ParseNode::AsFunctionCall() const { return nullptr; }
const IdentifierNode* ParseNode::AsIdentifier() const { return nullptr; }
const ListNode* ParseNode::AsList() const { return nullptr; }
const LiteralNode* ParseNode::AsLiteral() const { return nullptr; }
const UnaryOpNode* ParseNode::AsUnaryOp() const { return nullptr; }

Comments* ParseNode::comments_mutable() {
  if (!comments_)
    comments_.reset(new Comments);
  return comments_.get();
}

void ParseNode::PrintComments(std::ostream& out, int indent) const {
  if (comments_) {
    std::string ind = IndentFor(indent + 1);
    for (const auto& token : comments_->before())
      out << ind << "+BEFORE_COMMENT(\"" << token.value() << "\")\n";
    for (const auto& token : comments_->suffix())
      out << ind << "+SUFFIX_COMMENT(\"" << token.value() << "\")\n";
    for (const auto& token : comments_->after())
      out << ind << "+AFTER_COMMENT(\"" << token.value() << "\")\n";
  }
}

// AccessorNode ---------------------------------------------------------------

AccessorNode::AccessorNode() {
}

AccessorNode::~AccessorNode() {
}

const AccessorNode* AccessorNode::AsAccessor() const {
  return this;
}

Value AccessorNode::Execute(Scope* scope, Err* err) const {
  if (index_)
    return ExecuteArrayAccess(scope, err);
  else if (member_)
    return ExecuteScopeAccess(scope, err);
  NOTREACHED();
  return Value();
}

LocationRange AccessorNode::GetRange() const {
  if (index_)
    return LocationRange(base_.location(), index_->GetRange().end());
  else if (member_)
    return LocationRange(base_.location(), member_->GetRange().end());
  NOTREACHED();
  return LocationRange();
}

Err AccessorNode::MakeErrorDescribing(const std::string& msg,
                                      const std::string& help) const {
  return Err(GetRange(), msg, help);
}

void AccessorNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "ACCESSOR\n";
  PrintComments(out, indent);
  out << IndentFor(indent + 1) << base_.value() << "\n";
  if (index_)
    index_->Print(out, indent + 1);
  else if (member_)
    member_->Print(out, indent + 1);
}

Value AccessorNode::ExecuteArrayAccess(Scope* scope, Err* err) const {
  const Value* base_value = scope->GetValue(base_.value(), true);
  if (!base_value) {
    *err = MakeErrorDescribing("Undefined identifier.");
    return Value();
  }
  if (!base_value->VerifyTypeIs(Value::LIST, err))
    return Value();

  size_t index = 0;
  if (!ComputeAndValidateListIndex(scope, base_value->list_value().size(),
                                   &index, err))
    return Value();
  return base_value->list_value()[index];
}

Value AccessorNode::ExecuteScopeAccess(Scope* scope, Err* err) const {
  // We jump through some hoops here since ideally a.b will count "b" as
  // accessed in the given scope. The value "a" might be in some normal nested
  // scope and we can modify it, but it might also be inherited from the
  // readonly root scope and we can't do used variable tracking on it. (It's
  // not legal to const cast it away since the root scope will be in readonly
  // mode and being accessed from multiple threads without locking.) So this
  // code handles both cases.
  const Value* result = nullptr;

  // Look up the value in the scope named by "base_".
  Value* mutable_base_value = scope->GetMutableValue(
      base_.value(), Scope::SEARCH_NESTED, true);
  if (mutable_base_value) {
    // Common case: base value is mutable so we can track variable accesses
    // for unused value warnings.
    if (!mutable_base_value->VerifyTypeIs(Value::SCOPE, err))
      return Value();
    result = mutable_base_value->scope_value()->GetValue(
        member_->value().value(), true);
  } else {
    // Fall back to see if the value is on a read-only scope.
    const Value* const_base_value = scope->GetValue(base_.value(), true);
    if (const_base_value) {
      // Read only value, don't try to mark the value access as a "used" one.
      if (!const_base_value->VerifyTypeIs(Value::SCOPE, err))
        return Value();
      result =
          const_base_value->scope_value()->GetValue(member_->value().value());
    } else {
      *err = Err(base_, "Undefined identifier.");
      return Value();
    }
  }

  if (!result) {
    *err = Err(member_.get(), "No value named \"" +
        member_->value().value() + "\" in scope \"" + base_.value() + "\"");
    return Value();
  }
  return *result;
}

void AccessorNode::SetNewLocation(int line_number) {
  Location old = base_.location();
  base_.set_location(
      Location(old.file(), line_number, old.column_number(), old.byte()));
}

bool AccessorNode::ComputeAndValidateListIndex(Scope* scope,
                                               size_t max_len,
                                               size_t* computed_index,
                                               Err* err) const {
  Value index_value = index_->Execute(scope, err);
  if (err->has_error())
    return false;
  if (!index_value.VerifyTypeIs(Value::INTEGER, err))
    return false;

  int64_t index_int = index_value.int_value();
  if (index_int < 0) {
    *err = Err(index_->GetRange(), "Negative array subscript.",
        "You gave me " + base::Int64ToString(index_int) + ".");
    return false;
  }
  size_t index_sizet = static_cast<size_t>(index_int);
  if (index_sizet >= max_len) {
    *err = Err(index_->GetRange(), "Array subscript out of range.",
        "You gave me " + base::Int64ToString(index_int) +
        " but I was expecting something from 0 to " +
        base::SizeTToString(max_len) + ", inclusive.");
    return false;
  }

  *computed_index = index_sizet;
  return true;
}

// BinaryOpNode ---------------------------------------------------------------

BinaryOpNode::BinaryOpNode() {
}

BinaryOpNode::~BinaryOpNode() {
}

const BinaryOpNode* BinaryOpNode::AsBinaryOp() const {
  return this;
}

Value BinaryOpNode::Execute(Scope* scope, Err* err) const {
  return ExecuteBinaryOperator(scope, this, left_.get(), right_.get(), err);
}

LocationRange BinaryOpNode::GetRange() const {
  return left_->GetRange().Union(right_->GetRange());
}

Err BinaryOpNode::MakeErrorDescribing(const std::string& msg,
                                      const std::string& help) const {
  return Err(op_, msg, help);
}

void BinaryOpNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BINARY(" << op_.value() << ")\n";
  PrintComments(out, indent);
  left_->Print(out, indent + 1);
  right_->Print(out, indent + 1);
}

// BlockNode ------------------------------------------------------------------

BlockNode::BlockNode(ResultMode result_mode) : result_mode_(result_mode) {
}

BlockNode::~BlockNode() {
}

const BlockNode* BlockNode::AsBlock() const {
  return this;
}

Value BlockNode::Execute(Scope* enclosing_scope, Err* err) const {
  std::unique_ptr<Scope> nested_scope;  // May be null.

  Scope* execution_scope;  // Either the enclosing_scope or nested_scope.
  if (result_mode_ == RETURNS_SCOPE) {
    // Create a nested scope to save the values for returning.
    nested_scope.reset(new Scope(enclosing_scope));
    execution_scope = nested_scope.get();
  } else {
    // Use the enclosing scope. Modifications will go into this also (for
    // example, if conditions and loops).
    execution_scope = enclosing_scope;
  }

  for (size_t i = 0; i < statements_.size() && !err->has_error(); i++) {
    // Check for trying to execute things with no side effects in a block.
    //
    // A BlockNode here means that somebody has a free-floating { }.
    // Technically this can have side effects since it could generated targets,
    // but we don't want to allow this since it creates ambiguity when
    // immediately following a function call that takes no block. By not
    // allowing free-floating blocks that aren't passed anywhere or assigned to
    // anything, this ambiguity is resolved.
    const ParseNode* cur = statements_[i].get();
    if (cur->AsList() || cur->AsLiteral() || cur->AsUnaryOp() ||
        cur->AsIdentifier() || cur->AsBlock()) {
      *err = cur->MakeErrorDescribing(
          "This statement has no effect.",
          "Either delete it or do something with the result.");
      return Value();
    }
    cur->Execute(execution_scope, err);
  }

  if (result_mode_ == RETURNS_SCOPE) {
    // Clear the reference to the containing scope. This will be passed in
    // a value whose lifetime will not be related to the enclosing_scope passed
    // to this function.
    nested_scope->DetachFromContaining();
    return Value(this, std::move(nested_scope));
  }
  return Value();
}

LocationRange BlockNode::GetRange() const {
  if (begin_token_.type() != Token::INVALID &&
      end_->value().type() != Token::INVALID) {
    return begin_token_.range().Union(end_->value().range());
  } else if (!statements_.empty()) {
    return statements_[0]->GetRange().Union(
        statements_[statements_.size() - 1]->GetRange());
  }
  return LocationRange();
}

Err BlockNode::MakeErrorDescribing(const std::string& msg,
                                   const std::string& help) const {
  return Err(GetRange(), msg, help);
}

void BlockNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BLOCK\n";
  PrintComments(out, indent);
  for (const auto& statement : statements_)
    statement->Print(out, indent + 1);
  if (end_ && end_->comments())
    end_->Print(out, indent + 1);
}

// ConditionNode --------------------------------------------------------------

ConditionNode::ConditionNode() {
}

ConditionNode::~ConditionNode() {
}

const ConditionNode* ConditionNode::AsConditionNode() const {
  return this;
}

Value ConditionNode::Execute(Scope* scope, Err* err) const {
  Value condition_result = condition_->Execute(scope, err);
  if (err->has_error())
    return Value();
  if (condition_result.type() != Value::BOOLEAN) {
    *err = condition_->MakeErrorDescribing(
        "Condition does not evaluate to a boolean value.",
        std::string("This is a value of type \"") +
            Value::DescribeType(condition_result.type()) +
            "\" instead.");
    err->AppendRange(if_token_.range());
    return Value();
  }

  if (condition_result.boolean_value()) {
    if_true_->Execute(scope, err);
  } else if (if_false_) {
    // The else block is optional.
    if_false_->Execute(scope, err);
  }

  return Value();
}

LocationRange ConditionNode::GetRange() const {
  if (if_false_)
    return if_token_.range().Union(if_false_->GetRange());
  return if_token_.range().Union(if_true_->GetRange());
}

Err ConditionNode::MakeErrorDescribing(const std::string& msg,
                                       const std::string& help) const {
  return Err(if_token_, msg, help);
}

void ConditionNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "CONDITION\n";
  PrintComments(out, indent);
  condition_->Print(out, indent + 1);
  if_true_->Print(out, indent + 1);
  if (if_false_)
    if_false_->Print(out, indent + 1);
}

// FunctionCallNode -----------------------------------------------------------

FunctionCallNode::FunctionCallNode() {
}

FunctionCallNode::~FunctionCallNode() {
}

const FunctionCallNode* FunctionCallNode::AsFunctionCall() const {
  return this;
}

Value FunctionCallNode::Execute(Scope* scope, Err* err) const {
  return functions::RunFunction(scope, this, args_.get(), block_.get(), err);
}

LocationRange FunctionCallNode::GetRange() const {
  if (function_.type() == Token::INVALID)
    return LocationRange();  // This will be null in some tests.
  if (block_)
    return function_.range().Union(block_->GetRange());
  return function_.range().Union(args_->GetRange());
}

Err FunctionCallNode::MakeErrorDescribing(const std::string& msg,
                                          const std::string& help) const {
  return Err(function_, msg, help);
}

void FunctionCallNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "FUNCTION(" << function_.value() << ")\n";
  PrintComments(out, indent);
  args_->Print(out, indent + 1);
  if (block_)
    block_->Print(out, indent + 1);
}

// IdentifierNode --------------------------------------------------------------

IdentifierNode::IdentifierNode() {
}

IdentifierNode::IdentifierNode(const Token& token) : value_(token) {
}

IdentifierNode::~IdentifierNode() {
}

const IdentifierNode* IdentifierNode::AsIdentifier() const {
  return this;
}

Value IdentifierNode::Execute(Scope* scope, Err* err) const {
  const Scope* found_in_scope = nullptr;
  const Value* value = scope->GetValueWithScope(value_.value(), true,
                                                &found_in_scope);
  Value result;
  if (!value) {
    *err = MakeErrorDescribing("Undefined identifier");
    return result;
  }

  if (!EnsureNotReadingFromSameDeclareArgs(this, scope, found_in_scope, err))
    return result;

  result = *value;
  result.set_origin(this);
  return result;
}

LocationRange IdentifierNode::GetRange() const {
  return value_.range();
}

Err IdentifierNode::MakeErrorDescribing(const std::string& msg,
                                        const std::string& help) const {
  return Err(value_, msg, help);
}

void IdentifierNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "IDENTIFIER(" << value_.value() << ")\n";
  PrintComments(out, indent);
}

void IdentifierNode::SetNewLocation(int line_number) {
  Location old = value_.location();
  value_.set_location(
      Location(old.file(), line_number, old.column_number(), old.byte()));
}

// ListNode -------------------------------------------------------------------

ListNode::ListNode() : prefer_multiline_(false) {
}

ListNode::~ListNode() {
}

const ListNode* ListNode::AsList() const {
  return this;
}

Value ListNode::Execute(Scope* scope, Err* err) const {
  Value result_value(this, Value::LIST);
  std::vector<Value>& results = result_value.list_value();
  results.reserve(contents_.size());

  for (const auto& cur : contents_) {
    if (cur->AsBlockComment())
      continue;
    results.push_back(cur->Execute(scope, err));
    if (err->has_error())
      return Value();
    if (results.back().type() == Value::NONE) {
      *err = cur->MakeErrorDescribing(
          "This does not evaluate to a value.",
          "I can't do something with nothing.");
      return Value();
    }
  }
  return result_value;
}

LocationRange ListNode::GetRange() const {
  return LocationRange(begin_token_.location(),
                       end_->value().location());
}

Err ListNode::MakeErrorDescribing(const std::string& msg,
                                  const std::string& help) const {
  return Err(begin_token_, msg, help);
}

void ListNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LIST" << (prefer_multiline_ ? " multiline" : "")
      << "\n";
  PrintComments(out, indent);
  for (const auto& cur : contents_)
    cur->Print(out, indent + 1);
  if (end_ && end_->comments())
    end_->Print(out, indent + 1);
}

template <typename Comparator>
void ListNode::SortList(Comparator comparator) {
  // Partitions first on BlockCommentNodes and sorts each partition separately.
  for (auto sr : GetSortRanges()) {
    bool skip = false;
    for (size_t i = sr.begin; i != sr.end; ++i) {
      // Bails out if any of the nodes are unsupported.
      const ParseNode* node = contents_[i].get();
      if (!node->AsLiteral() && !node->AsIdentifier() && !node->AsAccessor()) {
        skip = true;
        continue;
      }
    }
    if (skip)
      continue;
    // Save the original line number so that we can re-assign ranges. We assume
    // they're contiguous lines because GetSortRanges() does so above. We need
    // to re-assign these line numbers primiarily because `gn format` uses them
    // to determine whether two nodes were initially separated by a blank line
    // or not.
    int start_line = contents_[sr.begin]->GetRange().begin().line_number();
    const ParseNode* original_first = contents_[sr.begin].get();
    std::sort(contents_.begin() + sr.begin, contents_.begin() + sr.end,
              [&comparator](const std::unique_ptr<const ParseNode>& a,
                            const std::unique_ptr<const ParseNode>& b) {
                return comparator(a.get(), b.get());
              });
    // If the beginning of the range had before comments, and the first node
    // moved during the sort, then move its comments to the new head of the
    // range.
    if (original_first->comments() &&
        contents_[sr.begin].get() != original_first) {
      for (const auto& hc : original_first->comments()->before()) {
        const_cast<ParseNode*>(contents_[sr.begin].get())
            ->comments_mutable()
            ->append_before(hc);
      }
      const_cast<ParseNode*>(original_first)
          ->comments_mutable()
          ->clear_before();
    }
    const ParseNode* prev = nullptr;
    for (size_t i = sr.begin; i != sr.end; ++i) {
      const ParseNode* node = contents_[i].get();
      DCHECK(node->AsLiteral() || node->AsIdentifier() || node->AsAccessor());
      int line_number =
          prev ? prev->GetRange().end().line_number() + 1 : start_line;
      if (node->AsLiteral()) {
        const_cast<LiteralNode*>(node->AsLiteral())
            ->SetNewLocation(line_number);
      } else if (node->AsIdentifier()) {
        const_cast<IdentifierNode*>(node->AsIdentifier())
            ->SetNewLocation(line_number);
      } else if (node->AsAccessor()) {
        const_cast<AccessorNode*>(node->AsAccessor())
            ->SetNewLocation(line_number);
      }
      prev = node;
    }
  }
}

void ListNode::SortAsStringsList() {
  // Sorts alphabetically.
  SortList([](const ParseNode* a, const ParseNode* b) {
    base::StringPiece astr = GetStringRepresentation(a);
    base::StringPiece bstr = GetStringRepresentation(b);
    return astr < bstr;
  });
}

void ListNode::SortAsDepsList() {
  // Sorts first relative targets, then absolute, each group is sorted
  // alphabetically.
  SortList([](const ParseNode* a, const ParseNode* b) {
    base::StringPiece astr = GetStringRepresentation(a);
    base::StringPiece bstr = GetStringRepresentation(b);
    return std::make_pair(GetDepsCategory(astr), SplitAtFirst(astr, ':')) <
           std::make_pair(GetDepsCategory(bstr), SplitAtFirst(bstr, ':'));
  });
}

// Breaks the ParseNodes of |contents| up by ranges that should be separately
// sorted. In particular, we break at a block comment, or an item that has an
// attached "before" comment and is separated by a blank line from the item
// before it. The assumption is that both of these indicate a separate 'section'
// of a sources block across which items should not be inter-sorted.
std::vector<ListNode::SortRange> ListNode::GetSortRanges() const {
  std::vector<SortRange> ranges;
  const ParseNode* prev = nullptr;
  size_t begin = 0;
  for (size_t i = begin; i < contents_.size(); prev = contents_[i++].get()) {
    if (IsSortRangeSeparator(contents_[i].get(), prev)) {
      if (i > begin) {
        ranges.push_back(SortRange(begin, i));
        // If |i| is an item with an attached comment, then we start the next
        // range at that point, because we want to include it in the sort.
        // Otherwise, it's a block comment which we skip over entirely because
        // we don't want to move or include it in the sort. The two cases are:
        //
        // sources = [
        //   "a",
        //   "b",
        //
        //   #
        //   # This is a block comment.
        //   #
        //
        //   "c",
        //   "d",
        // ]
        //
        // which contains 5 elements, and for which the ranges would be { [0,
        // 2), [3, 5) } (notably excluding 2, the block comment), and:
        //
        // sources = [
        //   "a",
        //   "b",
        //
        //   # This is a header comment.
        //   "c",
        //   "d",
        // ]
        //
        // which contains 4 elements, index 2 containing an attached 'before'
        // comments, and the ranges should be { [0, 2), [2, 4) }.
        if (!contents_[i]->AsBlockComment())
          begin = i;
        else
          begin = i + 1;
      } else {
        // If it was a one item range, just skip over it.
        begin = i + 1;
      }
    }
  }
  if (begin != contents_.size())
    ranges.push_back(SortRange(begin, contents_.size()));
  return ranges;
}

// LiteralNode -----------------------------------------------------------------

LiteralNode::LiteralNode() {
}

LiteralNode::LiteralNode(const Token& token) : value_(token) {
}

LiteralNode::~LiteralNode() {
}

const LiteralNode* LiteralNode::AsLiteral() const {
  return this;
}

Value LiteralNode::Execute(Scope* scope, Err* err) const {
  switch (value_.type()) {
    case Token::TRUE_TOKEN:
      return Value(this, true);
    case Token::FALSE_TOKEN:
      return Value(this, false);
    case Token::INTEGER: {
      base::StringPiece s = value_.value();
      if ((s.starts_with("0") && s.size() > 1) || s.starts_with("-0")) {
        if (s == "-0")
          *err = MakeErrorDescribing("Negative zero doesn't make sense");
        else
          *err = MakeErrorDescribing("Leading zeros not allowed");
        return Value();
      }
      int64_t result_int;
      if (!base::StringToInt64(s, &result_int)) {
        *err = MakeErrorDescribing("This does not look like an integer");
        return Value();
      }
      return Value(this, result_int);
    }
    case Token::STRING: {
      Value v(this, Value::STRING);
      ExpandStringLiteral(scope, value_, &v, err);
      return v;
    }
    default:
      NOTREACHED();
      return Value();
  }
}

LocationRange LiteralNode::GetRange() const {
  return value_.range();
}

Err LiteralNode::MakeErrorDescribing(const std::string& msg,
                                     const std::string& help) const {
  return Err(value_, msg, help);
}

void LiteralNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "LITERAL(" << value_.value() << ")\n";
  PrintComments(out, indent);
}

void LiteralNode::SetNewLocation(int line_number) {
  Location old = value_.location();
  value_.set_location(
      Location(old.file(), line_number, old.column_number(), old.byte()));
}

// UnaryOpNode ----------------------------------------------------------------

UnaryOpNode::UnaryOpNode() {
}

UnaryOpNode::~UnaryOpNode() {
}

const UnaryOpNode* UnaryOpNode::AsUnaryOp() const {
  return this;
}

Value UnaryOpNode::Execute(Scope* scope, Err* err) const {
  Value operand_value = operand_->Execute(scope, err);
  if (err->has_error())
    return Value();
  return ExecuteUnaryOperator(scope, this, operand_value, err);
}

LocationRange UnaryOpNode::GetRange() const {
  return op_.range().Union(operand_->GetRange());
}

Err UnaryOpNode::MakeErrorDescribing(const std::string& msg,
                                     const std::string& help) const {
  return Err(op_, msg, help);
}

void UnaryOpNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "UNARY(" << op_.value() << ")\n";
  PrintComments(out, indent);
  operand_->Print(out, indent + 1);
}

// BlockCommentNode ------------------------------------------------------------

BlockCommentNode::BlockCommentNode() {
}

BlockCommentNode::~BlockCommentNode() {
}

const BlockCommentNode* BlockCommentNode::AsBlockComment() const {
  return this;
}

Value BlockCommentNode::Execute(Scope* scope, Err* err) const {
  return Value();
}

LocationRange BlockCommentNode::GetRange() const {
  return comment_.range();
}

Err BlockCommentNode::MakeErrorDescribing(const std::string& msg,
                                          const std::string& help) const {
  return Err(comment_, msg, help);
}

void BlockCommentNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "BLOCK_COMMENT(" << comment_.value() << ")\n";
  PrintComments(out, indent);
}

// EndNode ---------------------------------------------------------------------

EndNode::EndNode(const Token& token) : value_(token) {
}

EndNode::~EndNode() {
}

const EndNode* EndNode::AsEnd() const {
  return this;
}

Value EndNode::Execute(Scope* scope, Err* err) const {
  return Value();
}

LocationRange EndNode::GetRange() const {
  return value_.range();
}

Err EndNode::MakeErrorDescribing(const std::string& msg,
                                        const std::string& help) const {
  return Err(value_, msg, help);
}

void EndNode::Print(std::ostream& out, int indent) const {
  out << IndentFor(indent) << "END(" << value_.value() << ")\n";
  PrintComments(out, indent);
}
