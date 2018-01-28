// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/item.h"

#include "base/logging.h"
#include "tools/gn/settings.h"

Item::Item(const Settings* settings, const Label& label)
    : settings_(settings), label_(label), defined_from_(nullptr) {}

Item::~Item() {
}

Config* Item::AsConfig() {
  return nullptr;
}
const Config* Item::AsConfig() const {
  return nullptr;
}
Pool* Item::AsPool() {
  return nullptr;
}
const Pool* Item::AsPool() const {
  return nullptr;
}
Target* Item::AsTarget() {
  return nullptr;
}
const Target* Item::AsTarget() const {
  return nullptr;
}
Toolchain* Item::AsToolchain() {
  return nullptr;
}
const Toolchain* Item::AsToolchain() const {
  return nullptr;
}

std::string Item::GetItemTypeName() const {
  if (AsConfig())
    return "config";
  if (AsTarget())
    return "target";
  if (AsToolchain())
    return "toolchain";
  if (AsPool())
    return "pool";
  NOTREACHED();
  return "this thing that I have no idea what it is";
}

bool Item::OnResolved(Err* err) {
  return true;
}
