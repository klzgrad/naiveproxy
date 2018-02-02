// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_BUILDER_RECORD_H_
#define TOOLS_GN_BUILDER_RECORD_H_

#include <memory>
#include <set>
#include <utility>

#include "base/macros.h"
#include "tools/gn/item.h"
#include "tools/gn/location.h"

class ParseNode;

// This class is used by the builder to manage the loading of the dependency
// tree. It holds a reference to an item and links to other records that the
// item depends on, both resolved ones, and unresolved ones.
//
// If a target depends on another one that hasn't been defined yet, we'll make
// a placeholder BuilderRecord with no item, and try to load the buildfile
// associated with the new item. The item will get filled in when we encounter
// the declaration for the item (or when we're done and realize there are
// undefined items).
//
// You can also have null item pointers when the target is not required for
// the current build (should_generate is false).
class BuilderRecord {
 public:
  typedef std::set<BuilderRecord*> BuilderRecordSet;

  enum ItemType {
    ITEM_UNKNOWN,
    ITEM_TARGET,
    ITEM_CONFIG,
    ITEM_TOOLCHAIN,
    ITEM_POOL
  };

  BuilderRecord(ItemType type, const Label& label);
  ~BuilderRecord();

  ItemType type() const { return type_; }
  const Label& label() const { return label_; }

  // Returns a user-ready name for the given type. e.g. "target".
  static const char* GetNameForType(ItemType type);

  // Returns true if the given item is of the given type.
  static bool IsItemOfType(const Item* item, ItemType type);

  // Returns the type enum for the given item.
  static ItemType TypeOfItem(const Item* item);

  Item* item() { return item_.get(); }
  const Item* item() const { return item_.get(); }
  void set_item(std::unique_ptr<Item> item) { item_ = std::move(item); }

  // Indicates from where this item was originally referenced from that caused
  // it to be loaded. For targets for which we encountered the declaration
  // before a reference, this will be the empty range.
  const ParseNode* originally_referenced_from() const {
    return originally_referenced_from_;
  }
  void set_originally_referenced_from(const ParseNode* pn) {
    originally_referenced_from_ = pn;
  }

  bool should_generate() const { return should_generate_; }
  void set_should_generate(bool sg) { should_generate_ = sg; }

  bool resolved() const { return resolved_; }
  void set_resolved(bool r) { resolved_ = r; }

  bool can_resolve() const {
    return item_ && unresolved_deps_.empty();
  }

  // All records this one is depending on.
  BuilderRecordSet& all_deps() { return all_deps_; }
  const BuilderRecordSet& all_deps() const { return all_deps_; }

  // Unresolved records this one is depending on. A subset of all... above.
  BuilderRecordSet& unresolved_deps() { return unresolved_deps_; }
  const BuilderRecordSet& unresolved_deps() const { return unresolved_deps_; }

  // Records that are waiting on this one to be resolved. This is the other
  // end of the "unresolved deps" arrow.
  BuilderRecordSet& waiting_on_resolution() { return waiting_on_resolution_; }
  const BuilderRecordSet& waiting_on_resolution() const {
    return waiting_on_resolution_;
  }

  void AddDep(BuilderRecord* record);

 private:
  ItemType type_;
  Label label_;
  std::unique_ptr<Item> item_;
  const ParseNode* originally_referenced_from_;
  bool should_generate_;
  bool resolved_;

  BuilderRecordSet all_deps_;
  BuilderRecordSet unresolved_deps_;
  BuilderRecordSet waiting_on_resolution_;

  DISALLOW_COPY_AND_ASSIGN(BuilderRecord);
};

#endif  // TOOLS_GN_BUILDER_RECORD_H_
