//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// <utility>

// template<class T, T N>
//   using make_integer_sequence = integer_sequence<T, 0, 1, ..., N-1>;

// UNSUPPORTED: c++03, c++11

// This test hangs during recursive template instantiation with libstdc++
// UNSUPPORTED: stdlib=libstdc++

// ADDITIONAL_COMPILE_FLAGS: -D_LIBCPP_TESTING_FALLBACK_MAKE_INTEGER_SEQUENCE

#include <utility>

typedef std::make_integer_sequence<int, -3> MakeSeqT;
MakeSeqT i; // expected-error-re@*:* {{static assertion failed{{.*}}std::make_integer_sequence must have a non-negative sequence length}}