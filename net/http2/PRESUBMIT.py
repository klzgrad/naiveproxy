# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re

def CheckForbiddenRegex(change, forbidden_regex, message_type, message):
  problems = []
  for path, change_per_file in change:
    line_num = 1
    for line in change_per_file:
      if forbidden_regex.match(line):
        problems.extend(["  %s:%d" % (path, line_num)])
      line_num += 1
  if not problems:
    return []
  return [message_type(message + ":\n" + "\n".join(problems))]


def CheckChange(input_api, message_type):
  result = []
  shared_source_files = re.compile("^net/http2/(?!platform/impl/).*\.(h|cc)$")
  change = [(affected_file.LocalPath(), affected_file.NewContents())
            for affected_file in input_api.AffectedTestableFiles()
            if shared_source_files.match(affected_file.LocalPath())]
  forbidden_regex_list = [
      r"^#include \"net/base/net_export.h\"$",
      r"\bNET_EXPORT\b",
      r"\bNET_EXPORT_PRIVATE\b",
      "^#include <string>$",
      r"\bstd::string\b",
      r"^#include \"base/strings/string_piece.h\"$",
      r"\bbase::StringPiece\b",
      r"\bbase::StringPrintf\b",
      r"\bbase::StringAppendF\b",
      r"\bbase::HexDigitToInt\b",
      r"\bbase::HexEncode\b",
      r"\bHexDecode\b",
      r"\bHexDump\b",
  ]
  messages = [
      "Include \"http2/platform/api/http2_export.h\" "
          "instead of \"net/base/net_export.h\"",
      "Use HTTP2_EXPORT instead of NET_EXPORT",
      "Use HTTP2_EXPORT_PRIVATE instead of NET_EXPORT_PRIVATE",
      "Include \"http2/platform/api/http2_string.h\" instead of <string>",
      "Use Http2String instead of std::string",
      "Include \"http2/platform/api/http2_string_piece.h\" "
          "instead of \"base/strings/string_piece.h\"",
      "Use Http2StringPiece instead of base::StringPiece",
      "Use Http2StringPrintf instead of base::StringPrintf",
      "Use Http2HexEncode instead of base::HexEncode",
      "Use Http2HexDecode instead of HexDecode",
      "Use Http2HexDump instead of HexDump",
  ]
  for forbidden_regex, message in zip(forbidden_regex_list, messages):
    result.extend(CheckForbiddenRegex(
        change, re.compile(forbidden_regex), message_type, message))
  return result

# Warn before uploading but allow developer to skip warning
# so that CLs can be shared and reviewed before addressing all issues.
def CheckChangeOnUpload(input_api, output_api):
  return CheckChange(input_api, output_api.PresubmitPromptWarning)

# Do not allow code with forbidden patterns to be checked in.
def CheckChangeOnCommit(input_api, output_api):
  return CheckChange(input_api, output_api.PresubmitError)
