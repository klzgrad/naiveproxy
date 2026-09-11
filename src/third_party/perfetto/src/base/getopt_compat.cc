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

// Returns true if |token| is an option-bearing argv element whose argument
// is supplied by the next argv element (rather than embedded or absent).
// Used by the GNU-style permutation logic in getopt_long().
bool TokenConsumesNextArg(const char* token, const std::vector<option>& opts) {
  if (token[0] != '-' || token[1] == '\0')
    return false;
  if (token[1] == '-') {
    // Long option: "--name" needs a separate arg if it's required_argument
    // and there is no embedded "=value".
    if (token[2] == '\0')
      return false;  // "--" alone.
    if (strchr(token + 2, '=') != nullptr)
      return false;
    size_t len = strlen(token + 2);
    const option* opt = LookupLongOpt(opts, token + 2, len);
    return opt && opt->has_arg == required_argument;
  }
  // Short option chain: "-abc" / "-aXYZ". Walk until we find an option that
  // requires an argument; if it is the last character of the token, the next
  // argv element is the argument. Anything after it would be the embedded arg.
  for (size_t i = 1; token[i] != '\0'; ++i) {
    const option* opt = LookupShortOpt(opts, token[i]);
    if (!opt)
      return false;
    if (opt->has_arg == required_argument)
      return token[i + 1] == '\0';
  }
  return false;
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

  // GNU-style permutation: if we're at a non-option, move the next option
  // (and its separate argument, if any) into position |optind|, shifting any
  // intervening non-options to the right. This matches the default behavior
  // of GNU getopt(3) so positional args can be freely interleaved with flags.
  // We only permute when |nextchar| is null (i.e. we're not in the middle of
  // a "-abc" short-option chain).
  if (!nextchar) {
    int scan = optind;
    while (scan < argc) {
      const char* s = argv[scan];
      // Stop at the next option-bearing token ("-x", "--long", or "--").
      if (s[0] == '-' && s[1] != '\0')
        break;
      ++scan;
    }
    // No permutation if there's nothing before the next option, or if the
    // next token is "--" (the explicit end-of-options marker).
    if (scan < argc && scan > optind && strcmp(argv[scan], "--") != 0) {
      bool takes_next = TokenConsumesNextArg(argv[scan], opts);
      int count = (takes_next && scan + 1 < argc) ? 2 : 1;
      char* opt_token = argv[scan];
      char* opt_arg = count == 2 ? argv[scan + 1] : nullptr;
      // Shift argv[optind..scan) right by |count| to make room.
      for (int k = scan - 1; k >= optind; --k)
        argv[k + count] = argv[k];
      argv[optind] = opt_token;
      if (count == 2)
        argv[optind + 1] = opt_arg;
    }
  }

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
