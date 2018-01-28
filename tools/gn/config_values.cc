// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/config_values.h"

namespace {

template<typename T>
void VectorAppend(std::vector<T>* append_to,
                  const std::vector<T>& append_this) {
  if (append_this.empty())
    return;
  append_to->insert(append_to->end(),append_this.begin(), append_this.end());
}

}  // namespace

ConfigValues::ConfigValues() {
}

ConfigValues::~ConfigValues() {
}

void ConfigValues::AppendValues(const ConfigValues& append) {
  VectorAppend(&asmflags_,     append.asmflags_);
  VectorAppend(&arflags_,      append.arflags_);
  VectorAppend(&cflags_,       append.cflags_);
  VectorAppend(&cflags_c_,     append.cflags_c_);
  VectorAppend(&cflags_cc_,    append.cflags_cc_);
  VectorAppend(&cflags_objc_,  append.cflags_objc_);
  VectorAppend(&cflags_objcc_, append.cflags_objcc_);
  VectorAppend(&defines_,      append.defines_);
  VectorAppend(&include_dirs_, append.include_dirs_);
  VectorAppend(&ldflags_,      append.ldflags_);
  VectorAppend(&lib_dirs_,     append.lib_dirs_);
  VectorAppend(&libs_,         append.libs_);

  // Only append precompiled header if there isn't one. It might be nice to
  // throw an error if there are conflicting precompiled headers, but that
  // requires piping through some context of the actual configs involved, and
  // conflicts here should be very unusual. Instead, use the first value.
  if (!append.precompiled_header_.empty() && !precompiled_header_.empty())
    precompiled_header_ = append.precompiled_header_;
  if (!append.precompiled_source_.is_null() && !precompiled_source_.is_null())
    precompiled_source_ = append.precompiled_source_;
}
