// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a "No Compile Test" suite.
// http://dev.chromium.org/developers/testing/no-compile-tests

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"

namespace base {

#if defined(NCTEST_DIFFERENT_ENUM)  // [r"\|sample\| and \|boundary\| shouldn't be of different enums"]

void WontCompile() {
  enum TypeA { A };
  enum TypeB { B };
  UMA_HISTOGRAM_ENUMERATION("", A, B);
}

#elif defined(NCTEST_DIFFERENT_ENUM_CLASS)  // [r"\|sample\| and \|boundary\| shouldn't be of different enums"]

void WontCompile() {
  enum class TypeA { A };
  enum class TypeB { B };
  UMA_HISTOGRAM_ENUMERATION("", TypeA::A, TypeB::B);
}

#elif defined(NCTEST_DIFFERENT_ENUM_MIXED)  // [r"\|sample\| and \|boundary\| shouldn't be of different enums"]

void WontCompile() {
  enum class TypeA { A };
  enum TypeB { B };
  UMA_HISTOGRAM_ENUMERATION("", TypeA::A, B);
}

#elif defined(NCTEST_NEGATIVE_ENUM_MAX)  // [r'static_assert failed "\|boundary\| is out of range of HistogramBase::Sample"']

void WontCompile() {
  // Buckets for enumeration start from 0, so a boundary < 0 is illegal.
  enum class TypeA { A = -1 };
  UMA_HISTOGRAM_ENUMERATION("", TypeA::A, TypeA::A);
}

#elif defined(NCTEST_ENUM_MAX_OUT_OF_RANGE)  // [r'static_assert failed "\|boundary\| is out of range of HistogramBase::Sample"']

void WontCompile() {
  // HistogramBase::Sample is an int and can't hold larger values.
  enum class TypeA : uint32_t { A = 0xffffffff };
  UMA_HISTOGRAM_ENUMERATION("", TypeA::A, TypeA::A);
}

#elif defined(NCTEST_SAMPLE_NOT_ENUM)  // [r'static_assert failed "Unexpected: \|boundary\| is enum, but \|sample\| is not."']

void WontCompile() {
  enum TypeA { A };
  UMA_HISTOGRAM_ENUMERATION("", 0, TypeA::A);
}

#elif defined(NCTEST_FUNCTION_INT)  // [r"Non enum passed to UmaHistogramEnumeration"]

void WontCompile() {
  UmaHistogramEnumeration("", 1, 2);
}

#elif defined(NCTEST_FUNCTION_DIFFERENT_ENUM)  // [r"no matching function for call to 'UmaHistogramEnumeration'"]

void WontCompile() {
  enum TypeA { A };
  enum TypeB { B };
  UmaHistogramEnumeration("", A, B);
}

#elif defined(NCTEST_FUNCTION_FIRST_NOT_ENUM)  // [r"no matching function for call to 'UmaHistogramEnumeration'"]

void WontCompile() {
  enum TypeB { B };
  UmaHistogramEnumeration("", 1, B);
}

#elif defined(NCTEST_FUNCTION_SECOND_NOT_ENUM)  // [r"no matching function for call to 'UmaHistogramEnumeration'"]

void WontCompile() {
  enum TypeA { A };
  UmaHistogramEnumeration("", A, 2);
}

#endif

}  // namespace base
