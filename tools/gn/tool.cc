// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/tool.h"

Tool::Tool()
    : defined_from_(nullptr),
      depsformat_(DEPS_GCC),
      precompiled_header_type_(PCH_NONE),
      restat_(false),
      complete_(false) {
}

Tool::~Tool() {
}

void Tool::SetComplete() {
  DCHECK(!complete_);
  complete_ = true;

  command_.FillRequiredTypes(&substitution_bits_);
  depfile_.FillRequiredTypes(&substitution_bits_);
  description_.FillRequiredTypes(&substitution_bits_);
  outputs_.FillRequiredTypes(&substitution_bits_);
  link_output_.FillRequiredTypes(&substitution_bits_);
  depend_output_.FillRequiredTypes(&substitution_bits_);
  rspfile_.FillRequiredTypes(&substitution_bits_);
  rspfile_content_.FillRequiredTypes(&substitution_bits_);
}
