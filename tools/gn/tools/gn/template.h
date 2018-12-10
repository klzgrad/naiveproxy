// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_TEMPLATE_H_
#define TOOLS_GN_TEMPLATE_H_

#include <memory>
#include <vector>

#include "base/memory/ref_counted.h"

class BlockNode;
class Err;
class FunctionCallNode;
class LocationRange;
class Scope;
class Value;

// Represents the information associated with a template() call in GN, which
// includes a closure and the code to run when the template is invoked.
//
// This class is immutable so we can reference it from multiple threads without
// locking. Normally, this will be associated with a .gni file and then a
// reference will be taken by each .gn file that imports it. These files might
// execute the template in parallel.
class Template : public base::RefCountedThreadSafe<Template> {
 public:
  // Makes a new closure based on the given scope.
  Template(const Scope* scope, const FunctionCallNode* def);

  // Takes ownership of a previously-constructed closure.
  Template(std::unique_ptr<Scope> closure, const FunctionCallNode* def);

  // Invoke the template. The values correspond to the state of the code
  // invoking the template. The template name needs to be supplied since the
  // template object itself doesn't know what name the calling code is using
  // to refer to it (this is used to set defaults).
  Value Invoke(Scope* scope,
               const FunctionCallNode* invocation,
               const std::string& template_name,
               const std::vector<Value>& args,
               BlockNode* block,
               Err* err) const;

  // Returns the location range where this template was defined.
  LocationRange GetDefinitionRange() const;

 private:
  friend class base::RefCountedThreadSafe<Template>;

  Template();
  ~Template();

  // It's important that this Scope is const. A template can be referenced by
  // the root BUILDCONFIG file and then duplicated to all threads. Therefore,
  // this scope must be usable from multiple threads at the same time.
  //
  // When executing a template, a new scope will be created as a child of this
  // one, which will reference it as mutable or not according to the mutability
  // of this value.
  std::unique_ptr<const Scope> closure_;

  const FunctionCallNode* definition_;
};

#endif  // TOOLS_GN_TEMPLATE_H_
