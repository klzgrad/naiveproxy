# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Chromium presubmit script for src/base.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details on the presubmit API built into depot_tools.
"""

def _CheckNoInterfacesInBase(input_api, output_api):
  """Checks to make sure no files in libbase.a have |@interface|."""
  pattern = input_api.re.compile(r'^\s*@interface', input_api.re.MULTILINE)
  files = []
  for f in input_api.AffectedSourceFiles(input_api.FilterSourceFile):
    if (f.LocalPath().startswith('base/') and
        not "/ios/" in f.LocalPath() and
        not "/test/" in f.LocalPath() and
        not f.LocalPath().endswith('.java') and
        not f.LocalPath().endswith('_unittest.mm') and
        not f.LocalPath().endswith('mac/sdk_forward_declarations.h')):
      contents = input_api.ReadFile(f)
      if pattern.search(contents):
        files.append(f)

  if len(files):
    return [ output_api.PresubmitError(
        'Objective-C interfaces or categories are forbidden in libbase. ' +
        'See http://groups.google.com/a/chromium.org/group/chromium-dev/' +
        'browse_thread/thread/efb28c10435987fd',
        files) ]
  return []


def _CheckNoTraceEventInclude(input_api, output_api):
  """Verify that //base includes base_tracing.h instead of trace event headers.

  Checks that files outside trace event implementation include the
  base_tracing.h header instead of specific trace event implementation headers
  to maintain compatibility with the gn flag "enable_base_tracing = false".
  """
  discouraged_includes = [
    r'^#include "base/trace_event/blame_context.h"$',
    r'^#include "base/trace_event/memory_allocator_dump_guid.h"$',
    r'^#include "base/trace_event/memory_dump_provider.h"$',
    r'^#include "base/trace_event/trace_event.h"$',
    r'^#include "base/trace_event/traced_value.h"$',
  ]

  white_list = [
    r".*\.(h|cc|mm)$",
  ]
  black_list = [
    r".*[\\/]trace_event[\\/].*",
    r".*[\\/]tracing[\\/].*",
  ]

  def FilterFile(affected_file):
    return input_api.FilterSourceFile(
      affected_file,
      white_list=white_list,
      black_list=black_list)

  locations = []
  for f in input_api.AffectedSourceFiles(FilterFile):
    for line_num, line in f.ChangedContents():
      for include in discouraged_includes:
        if input_api.re.search(include, line):
          locations.append("    %s:%d" % (f.LocalPath(), line_num))
          break

  if locations:
    return [ output_api.PresubmitPromptWarning(
        'Consider replacing includes to trace_event implementation headers\n' +
        'in //base with "base/trace_event/base_tracing.h" and/or verify\n' +
        'that base_unittests still passes with gn arg\n' +
        'enable_base_tracing = false.\n' + '\n'.join(locations)) ]
  return []


def _CommonChecks(input_api, output_api):
  """Checks common to both upload and commit."""
  results = []
  results.extend(_CheckNoInterfacesInBase(input_api, output_api))
  results.extend(_CheckNoTraceEventInclude(input_api, output_api))
  return results


def CheckChangeOnUpload(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  return results


def CheckChangeOnCommit(input_api, output_api):
  results = []
  results.extend(_CommonChecks(input_api, output_api))
  return results
