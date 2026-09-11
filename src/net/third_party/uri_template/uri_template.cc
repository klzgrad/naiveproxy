/*
 * \copyright Copyright 2013 Google Inc. All Rights Reserved.
 * \license @{
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @}
 */

// Implementation of RFC 6570 based on (open source implementation) at
//   java/com/google/api/client/http/UriTemplate.java
// The URI Template spec is at http://tools.ietf.org/html/rfc6570
// Templates up to level 3 are supported.

#include "net/third_party/uri_template/uri_template.h"

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "third_party/abseil-cpp/absl/container/flat_hash_map.h"

using std::string;

namespace uri_template {

namespace {

// The UriTemplateConfig is used to represent variable sections and to construct
// the expanded url.
struct UriTemplateConfig {
 public:
  UriTemplateConfig(std::string_view prefix,
                    std::string_view joiner,
                    bool requires_variable_assignment,
                    bool allow_reserved_expansion,
                    bool no_variable_assignment_if_empty = false)
      : prefix_(prefix),
        joiner_(joiner),
        requires_variable_assignment_(requires_variable_assignment),
        no_variable_assignment_if_empty_(no_variable_assignment_if_empty),
        allow_reserved_expansion_(allow_reserved_expansion) {}

  void AppendValue(std::string_view variable,
                   std::string_view value,
                   bool use_prefix,
                   string* target) const {
    const std::string& joiner = use_prefix ? prefix_ : joiner_;
    if (requires_variable_assignment_) {
      if (value.empty() && no_variable_assignment_if_empty_) {
        base::StrAppend(target, {joiner, EscapedValue(variable)});
      } else {
        base::StrAppend(
            target, {joiner, EscapedValue(variable), "=", EscapedValue(value)});
      }
    } else {
      base::StrAppend(target, {joiner, EscapedValue(value)});
    }
  }

 private:
  string EscapedValue(std::string_view value) const {
    if (allow_reserved_expansion_) {
      // Reserved expansion passes through reserved and pct-encoded characters.
      return base::EscapeExternalHandlerValue(value);
    }

    return base::EscapeAllExceptUnreserved(value);
  }

  std::string prefix_;
  std::string joiner_;
  bool requires_variable_assignment_;
  bool no_variable_assignment_if_empty_;
  bool allow_reserved_expansion_;
};

// variable is an in-out argument. On input it is the content between the
// '{}' in the source. On result the control parameters are stripped off
// leaving just the comma-separated variable name(s) that we should try to
// resolve.
UriTemplateConfig MakeConfig(std::string_view* variable) {
  if (variable->empty()) {
    return UriTemplateConfig("", ",", false, false);
  }
  switch (variable->front()) {
    // Reserved expansion.
    case '+':
      variable->remove_prefix(1);
      return UriTemplateConfig("", ",", false, true);

    // Fragment expansion.
    case '#':
      variable->remove_prefix(1);
      return UriTemplateConfig("#", ",", false, true);

    // Label with dot-prefix.
    case '.':
      variable->remove_prefix(1);
      return UriTemplateConfig(".", ".", false, false);

    // Path segment expansion.
    case '/':
      variable->remove_prefix(1);
      return UriTemplateConfig("/", "/", false, false);

    // Path segment parameter expansion.
    case ';':
      variable->remove_prefix(1);
      return UriTemplateConfig(";", ";", true, false, true);

    // Form-style query expansion.
    case '?':
      variable->remove_prefix(1);
      return UriTemplateConfig("?", "&", true, false);

    // Form-style query continuation.
    case '&':
      variable->remove_prefix(1);
      return UriTemplateConfig("&", "&", true, false);

    // Simple expansion.
    default:
      return UriTemplateConfig("", ",", false, false);
  }
}

void ProcessVariableSection(
    std::string_view variable_section,
    const absl::flat_hash_map<string, string>& parameters,
    string* target,
    std::set<string>* vars_found) {
  // Note that this function will modify the variable_section string_view to
  // remove the decorators, leaving just comma-separated variable name(s).
  UriTemplateConfig config = MakeConfig(&variable_section);
  std::vector<std::string_view> variables = base::SplitStringPiece(
      variable_section, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  bool first_var = true;
  for (std::string_view variable : variables) {
    auto found = parameters.find(variable);
    if (found != parameters.end()) {
      config.AppendValue(variable, found->second, first_var, target);
      first_var = false;
      if (vars_found) {
        vars_found->emplace(variable);
      }
    }
  }
}

}  // namespace

bool Expand(const string& path_uri,
            const absl::flat_hash_map<string, string>& parameters,
            string* target,
            std::set<string>* vars_found) {
  std::string_view path(path_uri);
  while (!path.empty()) {
    const size_t open = path.find('{');
    const size_t close = path.find('}');
    if (open == std::string_view::npos) {
      if (close == std::string_view::npos) {
        // No more variables to process.
        target->append(path);
        return true;
      } else {
        // Template was malformed. Unexpected closing brace.
        target->clear();
        return false;
      }
    }

    if (close == std::string_view::npos || close < open) {
      // Template was malformed. No closing brace, or closing brace before
      // opening brace.
      target->clear();
      return false;
    }

    const size_t length_of_section = close - open - 1;
    target->append(path.substr(0, open));
    path.remove_prefix(open + 1);

    const std::string_view variable_section = path.substr(0, length_of_section);
    if (variable_section.contains('{')) {
      // Template was malformed.
      target->clear();
      return false;
    }

    path.remove_prefix(length_of_section + 1);

    ProcessVariableSection(variable_section, parameters, target, vars_found);
  }
  return true;
}

}  // namespace uri_template
