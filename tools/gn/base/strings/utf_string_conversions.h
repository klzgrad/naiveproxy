// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_STRINGS_UTF_STRING_CONVERSIONS_H_
#define BASE_STRINGS_UTF_STRING_CONVERSIONS_H_

#include <stddef.h>

#include <string>

#include "base/strings/string16.h"
#include "base/strings/string_piece.h"

namespace base {

// These convert between UTF-8, -16, and -32 strings. They are potentially slow,
// so avoid unnecessary conversions. The low-level versions return a boolean
// indicating whether the conversion was 100% valid. In this case, it will still
// do the best it can and put the result in the output buffer. The versions that
// return strings ignore this error and just return the best conversion
// possible.
bool WideToUTF8(const wchar_t* src, size_t src_len, std::string* output);
std::string WideToUTF8(WStringPiece wide);
bool UTF8ToWide(const char* src, size_t src_len, std::wstring* output);
std::wstring UTF8ToWide(StringPiece utf8);

bool WideToUTF16(const wchar_t* src, size_t src_len, string16* output);
string16 WideToUTF16(WStringPiece wide);
bool UTF16ToWide(const char16* src, size_t src_len, std::wstring* output);
std::wstring UTF16ToWide(StringPiece16 utf16);

bool UTF8ToUTF16(const char* src, size_t src_len, string16* output);
string16 UTF8ToUTF16(StringPiece utf8);
bool UTF16ToUTF8(const char16* src, size_t src_len, std::string* output);
std::string UTF16ToUTF8(StringPiece16 utf16);

// This converts an ASCII string, typically a hardcoded constant, to a UTF16
// string.
string16 ASCIIToUTF16(StringPiece ascii);

// Converts to 7-bit ASCII by truncating. The result must be known to be ASCII
// beforehand.
std::string UTF16ToASCII(StringPiece16 utf16);

}  // namespace base

#endif  // BASE_STRINGS_UTF_STRING_CONVERSIONS_H_
