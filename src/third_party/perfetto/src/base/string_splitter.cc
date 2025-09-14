/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "perfetto/ext/base/string_splitter.h"

#include <utility>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace base {

StringSplitter::StringSplitter(std::string str,
                               char delimiter,
                               EmptyTokenMode empty_token_mode)
    : str_(std::move(str)),
      delimiter_(delimiter),
      empty_token_mode_(empty_token_mode) {
  // It's legal to access str[str.size()] in C++11 (it always returns \0),
  // hence the +1 (which becomes just size() after the -1 in Initialize()).
  Initialize(&str_[0], str_.size() + 1);
}

StringSplitter::StringSplitter(char* str,
                               size_t size,
                               char delimiter,
                               EmptyTokenMode empty_token_mode)
    : delimiter_(delimiter), empty_token_mode_(empty_token_mode) {
  Initialize(str, size);
}

StringSplitter::StringSplitter(StringSplitter* outer,
                               char delimiter,
                               EmptyTokenMode empty_token_mode)
    : delimiter_(delimiter), empty_token_mode_(empty_token_mode) {
  Initialize(outer->cur_token(), outer->cur_token_size() + 1);
}

void StringSplitter::Initialize(char* str, size_t size) {
  PERFETTO_DCHECK(!size || str);
  next_ = str;
  end_ = str + size;
  cur_ = nullptr;
  cur_size_ = 0;
  if (size)
    next_[size - 1] = '\0';
}

bool StringSplitter::Next() {
  for (; next_ < end_; next_++) {
    if (*next_ == delimiter_ &&
        empty_token_mode_ == EmptyTokenMode::DISALLOW_EMPTY_TOKENS) {
      // If empty tokens are disallowed, find fist non-delimiter character.
      continue;
    }
    cur_ = next_;
    for (;; next_++) {
      if (*next_ == delimiter_) {
        cur_size_ = static_cast<size_t>(next_ - cur_);
        *(next_++) = '\0';
        break;
      }
      if (*next_ == '\0') {
        cur_size_ = static_cast<size_t>(next_ - cur_);
        next_ = end_;
        break;
      }
    }
    if (*cur_ || empty_token_mode_ == EmptyTokenMode::ALLOW_EMPTY_TOKENS)
      return true;
    break;
  }
  cur_ = nullptr;
  cur_size_ = 0;
  return false;
}

}  // namespace base
}  // namespace perfetto
