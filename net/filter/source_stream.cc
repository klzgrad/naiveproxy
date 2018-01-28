// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/filter/source_stream.h"

namespace net {

SourceStream::SourceStream(SourceType type) : type_(type) {}

SourceStream::~SourceStream() {}

std::string SourceStream::Description() const {
  return "";
}

}  // namespace net
