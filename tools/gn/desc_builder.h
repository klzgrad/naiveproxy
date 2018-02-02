// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_DESC_BUILDER_H_
#define TOOLS_GN_DESC_BUILDER_H_

#include "base/values.h"
#include "tools/gn/target.h"

class DescBuilder {
 public:
  // Creates Dictionary representation for given target
  static std::unique_ptr<base::DictionaryValue> DescriptionForTarget(
      const Target* target,
      const std::string& what,
      bool all,
      bool tree,
      bool blame);

  // Creates Dictionary representation for given config
  static std::unique_ptr<base::DictionaryValue> DescriptionForConfig(
      const Config* config,
      const std::string& what);
};

#endif
