// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_reader.h"
#include "base/values.h"

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2)
    return 0;

  // Create a copy of input buffer, as otherwise we don't catch
  // overflow that touches the last byte (which is used in options).
  std::unique_ptr<char[]> input(new char[size - 1]);
  memcpy(input.get(), data, size - 1);

  base::StringPiece input_string(input.get(), size - 1);

  const int options = data[size - 1];

  base::JSONReader::ReadAndReturnValueWithError(input_string, options);

  return 0;
}
