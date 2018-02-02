// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/operators.h"

#include <stddef.h>
#include <algorithm>

#include "base/strings/string_number_conversions.h"
#include "tools/gn/err.h"
#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"
#include "tools/gn/token.h"
#include "tools/gn/value.h"

namespace {

const char kSourcesName[] = "sources";

// Helper class used for assignment operations: =, +=, and -= to generalize
// writing to various types of destinations.
class ValueDestination {
 public:
  ValueDestination();

  bool Init(Scope* exec_scope,
            const ParseNode* dest,
            const BinaryOpNode* op_node,
            Err* err);

  // Returns the value in the destination scope if it already exists, or null
  // if it doesn't. This is for validation and does not count as a "use".
  // Other nested scopes will be searched.
  const Value* GetExistingValue() const;

  // Returns an existing version of the output if it can be modified. This will
  // not search nested scopes since writes only go into the current scope.
  // Returns null if the value does not exist, or is not in the current scope
  // (meaning assignments won't go to this value and it's not mutable). This
  // is for implementing += and -=.
  //
  // If it exists, this will mark the origin of the value to be the passed-in
  // node, and the value will be also marked unused (if possible) under the
  // assumption that it will be modified in-place.
  Value* GetExistingMutableValueIfExists(const ParseNode* origin);

  // Returns the sources assignment filter if it exists for the current
  // scope and it should be applied to this assignment. Otherwise returns null.
  const PatternList* GetAssignmentFilter(const Scope* exec_scope) const;

  // Returns a pointer to the value that was set.
  Value* SetValue(Value value, const ParseNode* set_node);

  // Fills the Err with an undefined value error appropriate for modification
  // operators: += and -= (where the source is also the dest).
  void MakeUndefinedIdentifierForModifyError(Err* err);

 private:
  enum Type { UNINITIALIZED, SCOPE, LIST };

  Type type_;

  // Valid when type_ == SCOPE.
  Scope* scope_;
  const Token* name_token_;

  // Valid when type_ == LIST.
  Value* list_;
  size_t index_;  // Guaranteed in-range when Init() succeeds.
};

ValueDestination::ValueDestination()
    : type_(UNINITIALIZED),
      scope_(nullptr),
      name_token_(nullptr),
      list_(nullptr),
      index_(0) {
}

bool ValueDestination::Init(Scope* exec_scope,
                            const ParseNode* dest,
                            const BinaryOpNode* op_node,
                            Err* err) {
  // Check for standard variable set.
  const IdentifierNode* dest_identifier = dest->AsIdentifier();
  if (dest_identifier) {
    type_ = SCOPE;
    scope_ = exec_scope;
    name_token_ = &dest_identifier->value();
    return true;
  }

  // Check for array and scope accesses. The base (array or scope variable
  // name) must always be defined ahead of time.
  const AccessorNode* dest_accessor = dest->AsAccessor();
  if (!dest_accessor) {
    *err = Err(op_node, "Assignment requires a lvalue.",
               "This thing on the left is not an identifier or accessor.");
    err->AppendRange(dest->GetRange());
    return false;
  }

  // Known to be an accessor.
  base::StringPiece base_str = dest_accessor->base().value();
  Value* base = exec_scope->GetMutableValue(
      base_str, Scope::SEARCH_CURRENT, false);
  if (!base) {
    // Base is either undefined or it's defined but not in the current scope.
    // Make a good error message.
    if (exec_scope->GetValue(base_str, false)) {
      *err = Err(dest_accessor->base(), "Suspicious in-place modification.",
          "This variable exists in a containing scope. Normally, writing to it "
          "would\nmake a copy of it into the current scope with the modified "
          "version. But\nhere you're modifying only an element of a scope or "
          "list object. It's unlikely\nyou meant to copy the entire thing just "
          "to modify this part of it.\n"
          "\n"
          "If you really wanted to do this, do:\n"
          "  " + base_str.as_string() + " = " + base_str.as_string() + "\n"
          "to copy it into the current scope before doing this operation.");
    } else {
      *err = Err(dest_accessor->base(), "Undefined identifier.");
    }
    return false;
  }

  if (dest_accessor->index()) {
    // List access with an index.
    if (!base->VerifyTypeIs(Value::LIST, err)) {
      // Errors here will confusingly refer to the variable declaration (since
      // that's all Value knows) rather than the list access. So rewrite the
      // error location to refer to the base value's location.
      *err = Err(dest_accessor->base(), err->message(), err->help_text());
      return false;
    }

    type_ = LIST;
    list_ = base;
    return dest_accessor->ComputeAndValidateListIndex(
        exec_scope, base->list_value().size(), &index_, err);
  }

  // Scope access with a dot.
  if (!base->VerifyTypeIs(Value::SCOPE, err)) {
    // As the for the list index case above, rewrite the error location.
    *err = Err(dest_accessor->base(), err->message(), err->help_text());
    return false;
  }
  type_ = SCOPE;
  scope_ = base->scope_value();
  name_token_ = &dest_accessor->member()->value();
  return true;
}

const Value* ValueDestination::GetExistingValue() const {
  if (type_ == SCOPE)
    return scope_->GetValue(name_token_->value(), true);
  else if (type_ == LIST)
    return &list_->list_value()[index_];
  return nullptr;
}

Value* ValueDestination::GetExistingMutableValueIfExists(
    const ParseNode* origin) {
  if (type_ == SCOPE) {
    Value* value = scope_->GetMutableValue(
        name_token_->value(), Scope::SEARCH_CURRENT, false);
    if (value) {
      // The value will be written to, reset its tracking information.
      value->set_origin(origin);
      scope_->MarkUnused(name_token_->value());
    }
  }
  if (type_ == LIST)
    return &list_->list_value()[index_];
  return nullptr;
}

const PatternList* ValueDestination::GetAssignmentFilter(
    const Scope* exec_scope) const {
  if (type_ != SCOPE)
    return nullptr;  // Destination can't be named, so no sources filtering.
  if (name_token_->value() != kSourcesName)
    return nullptr;  // Destination not named "sources".

  const PatternList* filter = exec_scope->GetSourcesAssignmentFilter();
  if (!filter || filter->is_empty())
    return nullptr;  // No filter or empty filter, don't need to do anything.
  return filter;
}

Value* ValueDestination::SetValue(Value value, const ParseNode* set_node) {
  if (type_ == SCOPE) {
    return scope_->SetValue(name_token_->value(), std::move(value), set_node);
  } else if (type_ == LIST) {
    Value* dest = &list_->list_value()[index_];
    *dest = std::move(value);
    return dest;
  }
  return nullptr;
}

void ValueDestination::MakeUndefinedIdentifierForModifyError(Err* err) {
  // When Init() succeeds, the base of any accessor has already been resolved
  // and that list indices are in-range. This means any undefined identifiers
  // are for scope accesses.
  DCHECK(type_ == SCOPE);
  *err = Err(*name_token_, "Undefined identifier.");
}

// Computes an error message for overwriting a nonempty list/scope with another.
Err MakeOverwriteError(const BinaryOpNode* op_node,
                       const Value& old_value) {
  std::string type_name;
  std::string empty_def;

  if (old_value.type() == Value::LIST) {
    type_name = "list";
    empty_def = "[]";
  } else if (old_value.type() == Value::SCOPE) {
    type_name = "scope";
    empty_def = "{}";
  } else {
    NOTREACHED();
  }

  Err result(op_node->left()->GetRange(),
      "Replacing nonempty " + type_name + ".",
      "This overwrites a previously-defined nonempty " + type_name +
      " with another nonempty " + type_name + ".");
  result.AppendSubErr(Err(old_value, "for previous definition",
      "Did you mean to append/modify instead? If you really want to overwrite, "
      "do:\n"
      "  foo = " + empty_def + "\nbefore reassigning."));
  return result;
}

// -----------------------------------------------------------------------------

Err MakeIncompatibleTypeError(const BinaryOpNode* op_node,
                              const Value& left,
                              const Value& right) {
  std::string msg =
      std::string("You can't do <") + Value::DescribeType(left.type()) + "> " +
      op_node->op().value().as_string() +
      " <" + Value::DescribeType(right.type()) + ">.";
  if (left.type() == Value::LIST) {
    // Append extra hint for list stuff.
    msg += "\n\nHint: If you're attempting to add or remove a single item from "
        " a list, use \"foo + [ bar ]\".";
  }
  return Err(op_node, "Incompatible types for binary operator.", msg);
}

Value GetValueOrFillError(const BinaryOpNode* op_node,
                          const ParseNode* node,
                          const char* name,
                          Scope* scope,
                          Err* err) {
  Value value = node->Execute(scope, err);
  if (err->has_error())
    return Value();
  if (value.type() == Value::NONE) {
    *err = Err(op_node->op(),
               "Operator requires a value.",
               "This thing on the " + std::string(name) +
                   " does not evaluate to a value.");
    err->AppendRange(node->GetRange());
    return Value();
  }
  return value;
}

void RemoveMatchesFromList(const BinaryOpNode* op_node,
                           Value* list,
                           const Value& to_remove,
                           Err* err) {
  std::vector<Value>& v = list->list_value();
  switch (to_remove.type()) {
    case Value::BOOLEAN:
    case Value::INTEGER:  // Filter out the individual int/string.
    case Value::STRING: {
      bool found_match = false;
      for (size_t i = 0; i < v.size(); /* nothing */) {
        if (v[i] == to_remove) {
          found_match = true;
          v.erase(v.begin() + i);
        } else {
          i++;
        }
      }
      if (!found_match) {
        *err = Err(to_remove.origin()->GetRange(), "Item not found",
            "You were trying to remove " + to_remove.ToString(true) +
            "\nfrom the list but it wasn't there.");
      }
      break;
    }

    case Value::LIST:  // Filter out each individual thing.
      for (const auto& elem : to_remove.list_value()) {
        // TODO(brettw) if the nested item is a list, we may want to search
        // for the literal list rather than remote the items in it.
        RemoveMatchesFromList(op_node, list, elem, err);
        if (err->has_error())
          return;
      }
      break;

    default:
      break;
  }
}

// Assignment -----------------------------------------------------------------

// We return a null value from this rather than the result of doing the append.
// See ValuePlusEquals for rationale.
Value ExecuteEquals(Scope* exec_scope,
                    const BinaryOpNode* op_node,
                    ValueDestination* dest,
                    Value right,
                    Err* err) {
  const Value* old_value = dest->GetExistingValue();
  if (old_value) {
    // Check for overwriting nonempty scopes or lists with other nonempty
    // scopes or lists. This prevents mistakes that clobber a value rather than
    // appending to it. For cases where a user meant to clear a value, allow
    // overwriting a nonempty list/scope with an empty one, which can then be
    // modified.
    if (old_value->type() == Value::LIST && right.type() == Value::LIST &&
        !old_value->list_value().empty() && !right.list_value().empty()) {
      *err = MakeOverwriteError(op_node, *old_value);
      return Value();
    } else if (old_value->type() == Value::SCOPE &&
               right.type() == Value::SCOPE &&
               old_value->scope_value()->HasValues(Scope::SEARCH_CURRENT) &&
               right.scope_value()->HasValues(Scope::SEARCH_CURRENT)) {
      *err = MakeOverwriteError(op_node, *old_value);
      return Value();
    }
  }

  Value* written_value = dest->SetValue(std::move(right), op_node->right());

  // Optionally apply the assignment filter in-place.
  const PatternList* filter = dest->GetAssignmentFilter(exec_scope);
  if (filter) {
    std::vector<Value>& list_value = written_value->list_value();
    auto first_deleted = std::remove_if(
        list_value.begin(), list_value.end(),
        [filter](const Value& v) {
          return filter->MatchesValue(v);
        });
    list_value.erase(first_deleted, list_value.end());
  }
  return Value();
}

// Plus/minus ------------------------------------------------------------------

// allow_left_type_conversion indicates if we're allowed to change the type of
// the left value. This is set to true when doing +, and false when doing +=.
Value ExecutePlus(const BinaryOpNode* op_node,
                  Value left,
                  Value right,
                  bool allow_left_type_conversion,
                  Err* err) {
  // Left-hand-side integer.
  if (left.type() == Value::INTEGER) {
    if (right.type() == Value::INTEGER) {
      // Int + int -> addition.
      return Value(op_node, left.int_value() + right.int_value());
    } else if (right.type() == Value::STRING && allow_left_type_conversion) {
      // Int + string -> string concat.
      return Value(
          op_node,
          base::Int64ToString(left.int_value()) + right.string_value());
    }
    *err = MakeIncompatibleTypeError(op_node, left, right);
    return Value();
  }

  // Left-hand-side string.
  if (left.type() == Value::STRING) {
    if (right.type() == Value::INTEGER) {
      // String + int -> string concat.
      return Value(op_node,
          left.string_value() + base::Int64ToString(right.int_value()));
    } else if (right.type() == Value::STRING) {
      // String + string -> string concat. Since the left is passed by copy
      // we can avoid realloc if there is enough buffer by appending to left
      // and assigning.
      left.string_value().append(right.string_value());
      return left;  // FIXME(brettw) des this copy?
    }
    *err = MakeIncompatibleTypeError(op_node, left, right);
    return Value();
  }

  // Left-hand-side list. The only valid thing is to add another list.
  if (left.type() == Value::LIST && right.type() == Value::LIST) {
    // Since left was passed by copy, avoid realloc by destructively appending
    // to it and using that as the result.
    for (Value& value : right.list_value())
      left.list_value().push_back(std::move(value));
    return left;  // FIXME(brettw) does this copy?
  }

  *err = MakeIncompatibleTypeError(op_node, left, right);
  return Value();
}

// Left is passed by value because it will be modified in-place and returned
// for the list case.
Value ExecuteMinus(const BinaryOpNode* op_node,
                   Value left,
                   const Value& right,
                   Err* err) {
  // Left-hand-side int. The only thing to do is subtract another int.
  if (left.type() == Value::INTEGER && right.type() == Value::INTEGER) {
    // Int - int -> subtraction.
    return Value(op_node, left.int_value() - right.int_value());
  }

  // Left-hand-side list. The only thing to do is subtract another list.
  if (left.type() == Value::LIST && right.type() == Value::LIST) {
    // In-place modify left and return it.
    RemoveMatchesFromList(op_node, &left, right, err);
    return left;
  }

  *err = MakeIncompatibleTypeError(op_node, left, right);
  return Value();
}

// In-place plus/minus ---------------------------------------------------------

void ExecutePlusEquals(Scope* exec_scope,
                       const BinaryOpNode* op_node,
                       ValueDestination* dest,
                       Value right,
                       Err* err) {
  // There are several cases. Some things we can convert "foo += bar" to
  // "foo = foo + bar". Some cases we can't (the 'sources' variable won't
  // get the right filtering on the list). Some cases we don't want to (lists
  // and strings will get unnecessary copying so we can to optimize these).
  //
  //  - Value is already mutable in the current scope:
  //     1. List/string append: use it.
  //     2. Other types: fall back to "foo = foo + bar"
  //
  //  - Value is not mutable in the current scope:
  //     3. List/string append: copy into current scope and append to that.
  //     4. Other types: fall back to "foo = foo + bar"
  //
  // The common case is to use += for list and string appends in the local
  // scope, so this is written to avoid multiple variable lookups in that case.
  Value* mutable_dest = dest->GetExistingMutableValueIfExists(op_node);
  if (!mutable_dest) {
    const Value* existing_value = dest->GetExistingValue();
    if (!existing_value) {
      // Undefined left-hand-size for +=.
      dest->MakeUndefinedIdentifierForModifyError(err);
      return;
    }

    if (existing_value->type() != Value::STRING &&
        existing_value->type() != Value::LIST) {
      // Case #4 above.
      dest->SetValue(ExecutePlus(op_node, *existing_value,
                                 std::move(right), false, err), op_node);
      return;
    }

    // Case #3 above, copy to current scope and fall-through to appending.
    mutable_dest = dest->SetValue(*existing_value, op_node);
  } else if (mutable_dest->type() != Value::STRING &&
             mutable_dest->type() != Value::LIST) {
    // Case #2 above.
    dest->SetValue(ExecutePlus(op_node, *mutable_dest,
                               std::move(right), false, err), op_node);
    return;
  }  // "else" is case #1 above.

  if (mutable_dest->type() == Value::STRING) {
    if (right.type() == Value::INTEGER) {
      // String + int -> string concat.
      mutable_dest->string_value().append(
          base::Int64ToString(right.int_value()));
    } else if (right.type() == Value::STRING) {
      // String + string -> string concat.
      mutable_dest->string_value().append(right.string_value());
    } else {
      *err = MakeIncompatibleTypeError(op_node, *mutable_dest, right);
    }
  } else if (mutable_dest->type() == Value::LIST) {
    // List concat.
    if (right.type() == Value::LIST) {
      // Note: don't reserve() the dest vector here since that actually hurts
      // the allocation pattern when the build script is doing multiple small
      // additions.
      const PatternList* filter = dest->GetAssignmentFilter(exec_scope);
      if (filter) {
        // Filtered list concat.
        for (Value& value : right.list_value()) {
          if (!filter->MatchesValue(value))
            mutable_dest->list_value().push_back(std::move(value));
        }
      } else {
        // Normal list concat. This is a destructive move.
        for (Value& value : right.list_value())
          mutable_dest->list_value().push_back(std::move(value));
      }
    } else {
      *err = Err(op_node->op(), "Incompatible types to add.",
          "To append a single item to a list do \"foo += [ bar ]\".");
    }
  }
}

void ExecuteMinusEquals(const BinaryOpNode* op_node,
                        ValueDestination* dest,
                        const Value& right,
                        Err* err) {
  // Like the += case, we can convert "foo -= bar" to "foo = foo - bar". Since
  // there is no sources filtering, this is always semantically valid. The
  // only case we don't do it is for lists in the current scope which is the
  // most common case, and also the one that can be optimized the most by
  // doing it in-place.
  Value* mutable_dest = dest->GetExistingMutableValueIfExists(op_node);
  if (!mutable_dest ||
      (mutable_dest->type() != Value::LIST || right.type() != Value::LIST)) {
    const Value* existing_value = dest->GetExistingValue();
    if (!existing_value) {
      // Undefined left-hand-size for -=.
      dest->MakeUndefinedIdentifierForModifyError(err);
      return;
    }
    dest->SetValue(ExecuteMinus(op_node, *existing_value, right, err), op_node);
    return;
  }

  // In-place removal of items from "right".
  RemoveMatchesFromList(op_node, mutable_dest, right, err);
}

// Comparison -----------------------------------------------------------------

Value ExecuteEqualsEquals(Scope* scope,
                          const BinaryOpNode* op_node,
                          const Value& left,
                          const Value& right,
                          Err* err) {
  if (left == right)
    return Value(op_node, true);
  return Value(op_node, false);
}

Value ExecuteNotEquals(Scope* scope,
                       const BinaryOpNode* op_node,
                       const Value& left,
                       const Value& right,
                       Err* err) {
  // Evaluate in terms of ==.
  Value result = ExecuteEqualsEquals(scope, op_node, left, right, err);
  result.boolean_value() = !result.boolean_value();
  return result;
}

Value FillNeedsTwoIntegersError(const BinaryOpNode* op_node,
                                const Value& left,
                                const Value& right,
                                Err* err) {
  *err = Err(op_node, "Comparison requires two integers.",
             "This operator can only compare two integers.");
  err->AppendRange(left.origin()->GetRange());
  err->AppendRange(right.origin()->GetRange());
  return Value();
}

Value ExecuteLessEquals(Scope* scope,
                        const BinaryOpNode* op_node,
                        const Value& left,
                        const Value& right,
                        Err* err) {
  if (left.type() != Value::INTEGER || right.type() != Value::INTEGER)
    return FillNeedsTwoIntegersError(op_node, left, right, err);
  return Value(op_node, left.int_value() <= right.int_value());
}

Value ExecuteGreaterEquals(Scope* scope,
                           const BinaryOpNode* op_node,
                           const Value& left,
                           const Value& right,
                           Err* err) {
  if (left.type() != Value::INTEGER || right.type() != Value::INTEGER)
    return FillNeedsTwoIntegersError(op_node, left, right, err);
  return Value(op_node, left.int_value() >= right.int_value());
}

Value ExecuteGreater(Scope* scope,
                     const BinaryOpNode* op_node,
                     const Value& left,
                     const Value& right,
                     Err* err) {
  if (left.type() != Value::INTEGER || right.type() != Value::INTEGER)
    return FillNeedsTwoIntegersError(op_node, left, right, err);
  return Value(op_node, left.int_value() > right.int_value());
}

Value ExecuteLess(Scope* scope,
                  const BinaryOpNode* op_node,
                  const Value& left,
                  const Value& right,
                  Err* err) {
  if (left.type() != Value::INTEGER || right.type() != Value::INTEGER)
    return FillNeedsTwoIntegersError(op_node, left, right, err);
  return Value(op_node, left.int_value() < right.int_value());
}

// Binary ----------------------------------------------------------------------

Value ExecuteOr(Scope* scope,
                const BinaryOpNode* op_node,
                const ParseNode* left_node,
                const ParseNode* right_node,
                Err* err) {
  Value left = GetValueOrFillError(op_node, left_node, "left", scope, err);
  if (err->has_error())
    return Value();
  if (left.type() != Value::BOOLEAN) {
    *err = Err(op_node->left(), "Left side of || operator is not a boolean.",
        "Type is \"" + std::string(Value::DescribeType(left.type())) +
        "\" instead.");
    return Value();
  }
  if (left.boolean_value())
    return Value(op_node, left.boolean_value());

  Value right = GetValueOrFillError(op_node, right_node, "right", scope, err);
  if (err->has_error())
    return Value();
  if (right.type() != Value::BOOLEAN) {
    *err = Err(op_node->right(), "Right side of || operator is not a boolean.",
        "Type is \"" + std::string(Value::DescribeType(right.type())) +
        "\" instead.");
    return Value();
  }

  return Value(op_node, left.boolean_value() || right.boolean_value());
}

Value ExecuteAnd(Scope* scope,
                 const BinaryOpNode* op_node,
                 const ParseNode* left_node,
                 const ParseNode* right_node,
                 Err* err) {
  Value left = GetValueOrFillError(op_node, left_node, "left", scope, err);
  if (err->has_error())
    return Value();
  if (left.type() != Value::BOOLEAN) {
    *err = Err(op_node->left(), "Left side of && operator is not a boolean.",
        "Type is \"" + std::string(Value::DescribeType(left.type())) +
        "\" instead.");
    return Value();
  }
  if (!left.boolean_value())
    return Value(op_node, left.boolean_value());

  Value right = GetValueOrFillError(op_node, right_node, "right", scope, err);
  if (err->has_error())
    return Value();
  if (right.type() != Value::BOOLEAN) {
    *err = Err(op_node->right(), "Right side of && operator is not a boolean.",
        "Type is \"" + std::string(Value::DescribeType(right.type())) +
        "\" instead.");
    return Value();
  }
  return Value(op_node, left.boolean_value() && right.boolean_value());
}

}  // namespace

// ----------------------------------------------------------------------------

Value ExecuteUnaryOperator(Scope* scope,
                           const UnaryOpNode* op_node,
                           const Value& expr,
                           Err* err) {
  DCHECK(op_node->op().type() == Token::BANG);

  if (expr.type() != Value::BOOLEAN) {
    *err = Err(op_node, "Operand of ! operator is not a boolean.",
        "Type is \"" + std::string(Value::DescribeType(expr.type())) +
        "\" instead.");
    return Value();
  }
  // TODO(scottmg): Why no unary minus?
  return Value(op_node, !expr.boolean_value());
}

Value ExecuteBinaryOperator(Scope* scope,
                            const BinaryOpNode* op_node,
                            const ParseNode* left,
                            const ParseNode* right,
                            Err* err) {
  const Token& op = op_node->op();

  // First handle the ones that take an lvalue.
  if (op.type() == Token::EQUAL ||
      op.type() == Token::PLUS_EQUALS ||
      op.type() == Token::MINUS_EQUALS) {
    // Compute the left side.
    ValueDestination dest;
    if (!dest.Init(scope, left, op_node, err))
      return Value();

    // Compute the right side.
    Value right_value = right->Execute(scope, err);
    if (err->has_error())
      return Value();
    if (right_value.type() == Value::NONE) {
      *err = Err(op, "Operator requires a rvalue.",
                 "This thing on the right does not evaluate to a value.");
      err->AppendRange(right->GetRange());
      return Value();
    }

    // "foo += bar" (same for "-=") is converted to "foo = foo + bar" here, but
    // we pass the original value of "foo" by pointer to avoid a copy.
    if (op.type() == Token::EQUAL) {
      ExecuteEquals(scope, op_node, &dest, std::move(right_value), err);
    } else if (op.type() == Token::PLUS_EQUALS) {
      ExecutePlusEquals(scope, op_node, &dest, std::move(right_value), err);
    } else if (op.type() == Token::MINUS_EQUALS) {
      ExecuteMinusEquals(op_node, &dest, right_value, err);
    } else {
      NOTREACHED();
    }
    return Value();
  }

  // ||, &&. Passed the node instead of the value so that they can avoid
  // evaluating the RHS on early-out.
  if (op.type() == Token::BOOLEAN_OR)
    return ExecuteOr(scope, op_node, left, right, err);
  if (op.type() == Token::BOOLEAN_AND)
    return ExecuteAnd(scope, op_node, left, right, err);

  // Everything else works on the evaluated left and right values.
  Value left_value = GetValueOrFillError(op_node, left, "left", scope, err);
  if (err->has_error())
    return Value();
  Value right_value = GetValueOrFillError(op_node, right, "right", scope, err);
  if (err->has_error())
    return Value();

  // +, -.
  if (op.type() == Token::MINUS)
    return ExecuteMinus(op_node, std::move(left_value), right_value, err);
  if (op.type() == Token::PLUS) {
    return ExecutePlus(op_node, std::move(left_value), std::move(right_value),
                       true, err);
  }

  // Comparisons.
  if (op.type() == Token::EQUAL_EQUAL)
    return ExecuteEqualsEquals(scope, op_node, left_value, right_value, err);
  if (op.type() == Token::NOT_EQUAL)
    return ExecuteNotEquals(scope, op_node, left_value, right_value, err);
  if (op.type() == Token::GREATER_EQUAL)
    return ExecuteGreaterEquals(scope, op_node, left_value, right_value, err);
  if (op.type() == Token::LESS_EQUAL)
    return ExecuteLessEquals(scope, op_node, left_value, right_value, err);
  if (op.type() == Token::GREATER_THAN)
    return ExecuteGreater(scope, op_node, left_value, right_value, err);
  if (op.type() == Token::LESS_THAN)
    return ExecuteLess(scope, op_node, left_value, right_value, err);

  return Value();
}
