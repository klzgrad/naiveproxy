/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "perfetto/ext/base/string_view_splitter.h"

#include <utility>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace base {

StringViewSplitter::StringViewSplitter(base::StringView str,
                                       char delimiter,
                                       EmptyTokenMode empty_token_mode)
    : str_(std::move(str)),
      delimiter_(delimiter),
      empty_token_mode_(empty_token_mode) {
  Initialize(str);
}

StringViewSplitter::StringViewSplitter(StringViewSplitter* outer,
                                       char delimiter,
                                       EmptyTokenMode empty_token_mode)
    : delimiter_(delimiter), empty_token_mode_(empty_token_mode) {
  Initialize(outer->cur_token());
}

void StringViewSplitter::Initialize(base::StringView str) {
  next_ = str;
  cur_ = "";
  end_of_input_ = false;
}

bool StringViewSplitter::Next() {
  if (end_of_input_) {
    cur_ = next_ = "";
    return false;
  }

  size_t substr_start = 0;
  if (empty_token_mode_ == EmptyTokenMode::DISALLOW_EMPTY_TOKENS) {
    while (substr_start < next_.size() &&
           next_.at(substr_start) == delimiter_) {
      substr_start++;
    }
  }

  if (substr_start >= next_.size()) {
    end_of_input_ = true;
    cur_ = next_ = "";
    return !cur_.empty() ||
           empty_token_mode_ == EmptyTokenMode::ALLOW_EMPTY_TOKENS;
  }

  size_t delimiter_start = next_.find(delimiter_, substr_start);
  if (delimiter_start == base::StringView::npos) {
    cur_ = next_.substr(substr_start);
    next_ = "";
    end_of_input_ = true;
    return !cur_.empty() ||
           empty_token_mode_ == EmptyTokenMode::ALLOW_EMPTY_TOKENS;
  }

  size_t delimiter_end = delimiter_start + 1;

  if (empty_token_mode_ == EmptyTokenMode::DISALLOW_EMPTY_TOKENS) {
    while (delimiter_end < next_.size() &&
           next_.at(delimiter_end) == delimiter_) {
      delimiter_end++;
    }
    if (delimiter_end >= next_.size()) {
      end_of_input_ = true;
    }
  }

  cur_ = next_.substr(substr_start, delimiter_start - substr_start);
  next_ = next_.substr(delimiter_end);

  return !cur_.empty() ||
         empty_token_mode_ == EmptyTokenMode::ALLOW_EMPTY_TOKENS;
}

}  // namespace base
}  // namespace perfetto
