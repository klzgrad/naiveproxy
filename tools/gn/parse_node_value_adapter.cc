// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/parse_node_value_adapter.h"

#include "tools/gn/parse_tree.h"
#include "tools/gn/scope.h"

ParseNodeValueAdapter::ParseNodeValueAdapter() : ref_(nullptr) {
}

ParseNodeValueAdapter::~ParseNodeValueAdapter() {
}


bool ParseNodeValueAdapter::Init(Scope* scope,
                                 const ParseNode* node,
                                 Err* err) {
  const IdentifierNode* identifier = node->AsIdentifier();
  if (identifier) {
    ref_ = scope->GetValue(identifier->value().value(), true);
    if (!ref_) {
      identifier->MakeErrorDescribing("Undefined identifier");
      return false;
    }
    return true;
  }

  temporary_ = node->Execute(scope, err);
  return !err->has_error();
}

bool ParseNodeValueAdapter::InitForType(Scope* scope,
                                        const ParseNode* node,
                                        Value::Type type,
                                        Err* err) {
  if (!Init(scope, node, err))
    return false;
  if (get().VerifyTypeIs(type, err))
    return true;

  // Fix up the error range (see class comment in the header file) to be the
  // identifier node rather than the original value.
  *err = Err(node, err->message(), err->help_text());
  return false;
}
