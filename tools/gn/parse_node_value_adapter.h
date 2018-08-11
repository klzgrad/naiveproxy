// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_PARSE_NODE_VALUE_ADAPTER_H_
#define TOOLS_GN_PARSE_NODE_VALUE_ADAPTER_H_

#include "base/macros.h"
#include "tools/gn/value.h"

class ParseNode;

// Provides a means to convert a parse node to a value without causing a copy
// in the common case of an "Identifier" node. Normally to get a value from a
// parse node you have to call Execute(), and when an identifier is executed
// it just returns the current value of itself as a copy. But some variables
// are very large (lists of many strings for example).
//
// The reason you might not want to do this is that in the case of an
// identifier where the copy is optimized away, the origin will still be the
// original value. The result can be confusing because it will reference the
// original value rather than the place where the value was dereferenced, e.g.
// for a function call. The InitForType() function will verify type information
// and will fix up the origin so it's not confusing.
class ParseNodeValueAdapter {
 public:
  ParseNodeValueAdapter();
  ~ParseNodeValueAdapter();

  const Value& get() {
    if (ref_)
      return *ref_;
    return temporary_;
  }

  // Initializes the adapter for the result of the given expression. Returns
  // truen on success.
  bool Init(Scope* scope, const ParseNode* node, Err* err);

  // Like Init() but additionally verifies that the type of the result matches.
  bool InitForType(Scope* scope,
                   const ParseNode* node,
                   Value::Type type,
                   Err* err);

 private:
  // Holds either a reference to an existing item, or a temporary as a copy.
  // If ref is non-null, it's valid, otherwise the temporary is used.
  const Value* ref_;
  Value temporary_;

  DISALLOW_COPY_AND_ASSIGN(ParseNodeValueAdapter);
};

#endif  // TOOLS_GN_PARSE_NODE_VALUE_ADAPTER_H_
