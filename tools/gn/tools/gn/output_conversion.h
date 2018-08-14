// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_OUTPUT_CONVERSION_H_
#define TOOLS_GN_OUTPUT_CONVERSION_H_

#include <iostream>
#include <string>

class Err;
class Settings;
class Value;

// Converts the given input Value to an output string (to be written to a file).
// Conversions as specified in the output_conversion string will be performed.
// The given ostream will be used for writing the resulting string.
//
// If the conversion string is invalid, the error will be set.
void ConvertValueToOutput(const Settings* settings,
                          const Value& output,
                          const Value& output_conversion_value,
                          std::ostream& out,
                          Err* err);

#endif  // TOOLS_GN_OUTPUT_CONVERSION_H_
