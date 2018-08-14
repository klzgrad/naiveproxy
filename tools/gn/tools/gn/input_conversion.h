// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_INPUT_CONVERSION_H_
#define TOOLS_GN_INPUT_CONVERSION_H_

#include <string>

class Err;
class ParseNode;
class Settings;
class Value;

extern const char kInputOutputConversion_Help[];

// Converts the given input string (is read from a file or output from a
// script) to a Value. Conversions as specified in the input_conversion string
// will be performed. The given origin will be used for constructing the
// resulting Value.
//
// If the conversion string is invalid, the error will be set and an empty
// value will be returned.
Value ConvertInputToValue(const Settings* settings,
                          const std::string& input,
                          const ParseNode* origin,
                          const Value& input_conversion_value,
                          Err* err);

#endif  // TOOLS_GN_INPUT_CONVERSION_H_
