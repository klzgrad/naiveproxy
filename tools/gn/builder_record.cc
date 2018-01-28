// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/builder_record.h"

#include "tools/gn/item.h"

BuilderRecord::BuilderRecord(ItemType type, const Label& label)
    : type_(type),
      label_(label),
      originally_referenced_from_(nullptr),
      should_generate_(false),
      resolved_(false) {
}

BuilderRecord::~BuilderRecord() {
}

// static
const char* BuilderRecord::GetNameForType(ItemType type) {
  switch (type) {
    case ITEM_TARGET:
      return "target";
    case ITEM_CONFIG:
      return "config";
    case ITEM_TOOLCHAIN:
      return "toolchain";
    case ITEM_POOL:
      return "pool";
    case ITEM_UNKNOWN:
    default:
      return "unknown";
  }
}

// static
bool BuilderRecord::IsItemOfType(const Item* item, ItemType type) {
  switch (type) {
    case ITEM_TARGET:
      return !!item->AsTarget();
    case ITEM_CONFIG:
      return !!item->AsConfig();
    case ITEM_TOOLCHAIN:
      return !!item->AsToolchain();
    case ITEM_POOL:
      return !!item->AsPool();
    case ITEM_UNKNOWN:
    default:
      return false;
  }
}

// static
BuilderRecord::ItemType BuilderRecord::TypeOfItem(const Item* item) {
  if (item->AsTarget())
    return ITEM_TARGET;
  if (item->AsConfig())
    return ITEM_CONFIG;
  if (item->AsToolchain())
    return ITEM_TOOLCHAIN;
  if (item->AsPool())
    return ITEM_POOL;

  NOTREACHED();
  return ITEM_UNKNOWN;
}

void BuilderRecord::AddDep(BuilderRecord* record) {
  all_deps_.insert(record);
  if (!record->resolved()) {
    unresolved_deps_.insert(record);
    record->waiting_on_resolution_.insert(this);
  }
}
