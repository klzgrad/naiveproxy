#!/usr/bin/env python
#
# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

import difflib
from util import build_utils


def _SkipOmitted(line):
  """
  Skip lines that are to be intentionally omitted from the expectations file.

  This is required when the file to be compared against expectations contains
  a line that changes from build to build because - for instance - it contains
  version information.
  """
  if line.endswith('# OMIT FROM EXPECTATIONS\n'):
    return '# THIS LINE WAS OMITTED\n'
  return line


def GenerateDiffWithOnlyAdditons(expected_path, actual_path):
  """Generate a diff that only contains additions"""
  with open(expected_path) as expected, open(actual_path) as actual:
    expected_lines = expected.readlines()
    actual_lines = actual.readlines()

    diff = difflib.ndiff(expected_lines, actual_lines)
    filtered_diff = (line for line in diff if line.startswith('+'))
    return ''.join(filtered_diff)


def DiffFileContents(expected_path, actual_path, show_files_compared=True):
  """Check file contents for equality and return the diff or None."""
  with open(expected_path) as f_expected, open(actual_path) as f_actual:
    expected_lines = f_expected.readlines()
    actual_lines = [_SkipOmitted(line) for line in f_actual]

  if expected_lines == actual_lines:
    return None

  expected_path = os.path.relpath(expected_path, build_utils.DIR_SOURCE_ROOT)
  actual_path = os.path.relpath(actual_path, build_utils.DIR_SOURCE_ROOT)

  diff = difflib.unified_diff(
      expected_lines,
      actual_lines,
      fromfile=os.path.join('before', expected_path),
      tofile=os.path.join('after', expected_path),
      n=0)

  files_compared_msg = """\
Files Compared:
  * {}
  * {}
  """.format(expected_path, actual_path)

  patch_msg = """\
If you are looking at this through LogDog, click "Raw log" before copying.
See https://bugs.chromium.org/p/chromium/issues/detail?id=984616.

To update the file, run:
########### START ###########
 patch -p1 <<'END_DIFF'
{}
END_DIFF
############ END ############
""".format(''.join(diff).rstrip())

  return files_compared_msg + patch_msg if show_files_compared else patch_msg
