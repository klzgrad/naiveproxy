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
    r'^#include "base/trace_event/(?!base_tracing\.h)',
  ]

  files_to_check = [
    r".*\.(h|cc|mm)$",
  ]
  files_to_skip = [
    r".*[\\/]test[\\/].*",
    r".*[\\/]trace_event[\\/].*",
    r".*[\\/]tracing[\\/].*",
  ]
  no_presubmit = r"// no-presubmit-check"

  def FilterFile(affected_file):
    return input_api.FilterSourceFile(
      affected_file,
      files_to_check=files_to_check,
      files_to_skip=files_to_skip)

  locations = []
  for f in input_api.AffectedSourceFiles(FilterFile):
    for line_num, line in f.ChangedContents():
      for include in discouraged_includes:
        if (input_api.re.search(include, line) and
            not input_api.re.search(no_presubmit, line)):
          locations.append("    %s:%d" % (f.LocalPath(), line_num))
          break

  if locations:
    return [ output_api.PresubmitError(
        'Base code should include "base/trace_event/base_tracing.h" instead\n' +
        'of trace_event implementation headers. If you need to include an\n' +
        'implementation header, verify that base_unittests still passes\n' +
        'with gn arg "enable_base_tracing = false" and add\n' +
        '"// no-presubmit-check" after the include. \n' +
        '\n'.join(locations)) ]
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
