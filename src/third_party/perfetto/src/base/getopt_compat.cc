/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/ext/base/getopt_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace base {
namespace getopt_compat {

char* optarg = nullptr;
int optind = 0;
int optopt = 0;
int opterr = 1;

namespace {

char* nextchar = nullptr;

const option* LookupLongOpt(const std::vector<option>& opts,
                            const char* name,
                            size_t len) {
  for (const option& opt : opts) {
    if (strncmp(opt.name, name, len) == 0 && strlen(opt.name) == len)
      return &opt;
  }
  return nullptr;
}

const option* LookupShortOpt(const std::vector<option>& opts, char c) {
  for (const option& opt : opts) {
    if (!*opt.name && opt.val == c)
      return &opt;
  }
  return nullptr;
}

bool ParseOpts(const char* shortopts,
               const option* longopts,
               std::vector<option>* res) {
  // Parse long options first.
  for (const option* lopt = longopts; lopt && lopt->name; lopt++) {
    PERFETTO_CHECK(lopt->flag == nullptr);
    PERFETTO_CHECK(lopt->has_arg == no_argument ||
                   lopt->has_arg == required_argument);
    res->emplace_back(*lopt);
  }

  // Merge short options.
  for (const char* sopt = shortopts; sopt && *sopt;) {
    const size_t idx = static_cast<size_t>(sopt - shortopts);
    char c = *sopt++;
    bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9');
    if (!valid) {
      fprintf(stderr,
              "Error parsing shortopts. Unexpected char '%c' at offset %zu\n",
              c, idx);
      return false;
    }
    res->emplace_back();
    option& opt = res->back();
    opt.name = "";
    opt.val = c;
    opt.has_arg = no_argument;
    if (*sopt == ':') {
      opt.has_arg = required_argument;
      ++sopt;
    }
  }
  return true;
}

}  // namespace

int getopt_long(int argc,
                char** argv,
                const char* shortopts,
                const option* longopts,
                std::nullptr_t /*longind*/) {
  std::vector<option> opts;
  optarg = nullptr;

  if (optind == 0)
    optind = 1;

  if (optind >= argc)
    return -1;

  if (!ParseOpts(shortopts, longopts, &opts))
    return '?';

  char* arg = argv[optind];
  optopt = 0;

  if (!nextchar) {
    // If |nextchar| is null we are NOT in the middle of a short option and we
    // should parse the next argv.
    if (strncmp(arg, "--", 2) == 0 && strlen(arg) > 2) {
      // A --long option.
      arg += 2;
      char* sep = strchr(arg, '=');
      optind++;

      size_t len = sep ? static_cast<size_t>(sep - arg) : strlen(arg);
      const option* opt = LookupLongOpt(opts, arg, len);

      if (!opt) {
        if (opterr)
          fprintf(stderr, "unrecognized option '--%s'\n", arg);
        return '?';
      }

      optopt = opt->val;
      if (opt->has_arg == no_argument) {
        if (sep) {
          fprintf(stderr, "option '--%s' doesn't allow an argument\n", arg);
          return '?';
        } else {
          return opt->val;
        }
      } else if (opt->has_arg == required_argument) {
        if (sep) {
          optarg = sep + 1;
          return opt->val;
        } else if (optind >= argc) {
          if (opterr)
            fprintf(stderr, "option '--%s' requires an argument\n", arg);
          return '?';
        } else {
          optarg = argv[optind++];
          return opt->val;
        }
      }
      // has_arg must be either |no_argument| or |required_argument|. We
      // shoulnd't get here unless the check in ParseOpts() has a bug.
      PERFETTO_CHECK(false);
    }  // if (arg ~= "--*").

    if (strlen(arg) > 1 && arg[0] == '-' && arg[1] != '-') {
      // A sequence of short options. Parsing logic continues below.
      nextchar = &arg[1];
    }
  }  // if(!nextchar)

  if (nextchar) {
    // At this point either:
    // 1. This is the first char of a sequence of short options, and we fell
    //    through here from the lines above.
    // 2. This is the N (>1) char of a sequence of short options, and we got
    //    here from a new getopt() call to getopt().
    const char cur_char = *nextchar;
    PERFETTO_CHECK(cur_char != '\0');

    // Advance the option char in any case, before we start reasoning on them.
    // if we got to the end of the "-abc" sequence, increment optind so the next
    // getopt() call resumes from the next argv argument.
    if (*(++nextchar) == '\0') {
      nextchar = nullptr;
      ++optind;
    }

    const option* opt = LookupShortOpt(opts, cur_char);
    optopt = cur_char;
    if (!opt) {
      if (opterr)
        fprintf(stderr, "invalid option -- '%c'\n", cur_char);
      return '?';
    }
    if (opt->has_arg == no_argument) {
      return cur_char;
    } else if (opt->has_arg == required_argument) {
      // This is a subtle getopt behavior. Say you call `tar -fx`, there are
      // two cases:
      // 1. If 'f' is no_argument then 'x' (and anything else after) is
      //    interpreted as an independent argument (like `tar -f -x`).
      // 2. If 'f' is required_argument, than everything else after the 'f'
      //    is interpreted as the option argument (like `tar -f x`)
      if (!nextchar) {
        // Case 1.
        if (optind >= argc) {
          if (opterr)
            fprintf(stderr, "option requires an argument -- '%c'\n", cur_char);
          return '?';
        } else {
          optarg = argv[optind++];
          return cur_char;
        }
      } else {
        // Case 2.
        optarg = nextchar;
        nextchar = nullptr;
        optind++;
        return cur_char;
      }
    }
    PERFETTO_CHECK(false);
  }  // if (nextchar)

  // If we get here, we found the first non-option argument. Stop here.

  if (strcmp(arg, "--") == 0)
    optind++;

  return -1;
}

int getopt(int argc, char** argv, const char* shortopts) {
  return getopt_long(argc, argv, shortopts, nullptr, nullptr);
}

}  // namespace getopt_compat
}  // namespace base
}  // namespace perfetto
