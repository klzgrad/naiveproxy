// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_INHERITED_LIBRARIES_H_
#define TOOLS_GN_INHERITED_LIBRARIES_H_

#include <stddef.h>

#include <map>
#include <utility>
#include <vector>

#include "base/macros.h"

class Target;

// Represents an ordered uniquified set of all shared/static libraries for
// a given target. These are pushed up the dependency tree.
//
// Maintaining the order is important so GN links all libraries in the same
// order specified in the build files.
//
// Since this list is uniquified, appending to the list will not actually
// append a new item if the target already exists. However, the existing one
// may have its is_public flag updated. "Public" always wins, so is_public will
// be true if any dependency with that name has been set to public.
class InheritedLibraries {
 public:
  InheritedLibraries();
  ~InheritedLibraries();

  // Returns the list of dependencies in order, optionally with the flag
  // indicating whether the dependency is public.
  std::vector<const Target*> GetOrdered() const;
  std::vector<std::pair<const Target*, bool>> GetOrderedAndPublicFlag() const;

  // Adds a single dependency to the end of the list. See note on adding above.
  void Append(const Target* target, bool is_public);

  // Appends all items from the "other" list to the current one. The is_public
  // parameter indicates how the current target depends on the items in
  // "other". If is_public is true, the existing public flags of the appended
  // items will be preserved (propogating the public-ness up the dependency
  // chain). If is_public is false, all deps will be added as private since
  // the current target isn't forwarding them.
  void AppendInherited(const InheritedLibraries& other, bool is_public);

  // Like AppendInherited but only appends the items in "other" that are of
  // type SHARED_LIBRARY and only when they're marked public. This is used
  // to push shared libraries up the dependency chain, following only public
  // deps, to dependent targets that need to use them.
  void AppendPublicSharedLibraries(const InheritedLibraries& other,
                                   bool is_public);

 private:
  struct Node {
    Node() : index(static_cast<size_t>(-1)), is_public(false) {}
    Node(size_t i, bool p) : index(i), is_public(p) {}

    size_t index;
    bool is_public;
  };

  typedef std::map<const Target*, Node> LibraryMap;
  LibraryMap map_;

  DISALLOW_COPY_AND_ASSIGN(InheritedLibraries);
};

#endif  // TOOLS_GN_INHERITED_LIBRARIES_H_
