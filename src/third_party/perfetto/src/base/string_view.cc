/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/string_view.h"

namespace perfetto {
namespace base {

// Without ignoring this warning we get the message:
//   error: out-of-line definition of constexpr static data member is redundant
//   in C++17 and is deprecated
// when using clang-cl in Windows.
#if defined(__GNUC__)  // GCC & clang
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated"
#endif  // __GNUC__

// static
constexpr size_t StringView::npos;

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

}  // namespace base
}  // namespace perfetto
