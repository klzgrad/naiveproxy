// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_OPERATORS_H_
#define TOOLS_GN_OPERATORS_H_

class BinaryOpNode;
class Err;
class ParseNode;
class Scope;
class UnaryOpNode;
class Value;

Value ExecuteUnaryOperator(Scope* scope,
                           const UnaryOpNode* op_node,
                           const Value& value,
                           Err* err);
Value ExecuteBinaryOperator(Scope* scope,
                            const BinaryOpNode* op_node,
                            const ParseNode* left,
                            const ParseNode* right,
                            Err* err);

#endif  // TOOLS_GN_OPERATORS_H_
