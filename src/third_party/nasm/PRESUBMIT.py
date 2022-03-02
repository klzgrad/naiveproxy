# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Presubmit script for nasm repository."""


def _WarnIfGitIgnoreHasSources(input_api, output_api):
  """Warn if .gitignore has source files in it."""
  for f in input_api.AffectedFiles():
    if f.LocalPath().endswith('.gitignore'):
      with open(f.LocalPath(), 'r') as f:
        lines = f.readlines()

      bad_lines = [l.strip() for l in lines if l.strip().endswith(('.c', '.h'))]
      if not bad_lines:
        break

      return [
          output_api.PresubmitError('\n'.join([
              '.gitignore contains source files which may be needed for building, ',
              'please remove the .gitignore entries for the following lines:',
              '\n    ' + '    \n'.join(bad_lines)
          ]))
      ]
  return []


def CheckChangeOnUpload(input_api, output_api):
  results = []
  results.extend(_WarnIfGitIgnoreHasSources(input_api, output_api))
  return results
