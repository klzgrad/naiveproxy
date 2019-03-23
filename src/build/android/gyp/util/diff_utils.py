#!/usr/bin/env python
#
# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import difflib


def DiffFileContents(expected_path, actual_path):
  """Check file contents for equality and return the diff or None."""
  with open(expected_path) as f_expected, open(actual_path) as f_actual:
    expected_lines = f_expected.readlines()
    actual_lines = f_actual.readlines()

  if expected_lines == actual_lines:
    return None

  diff = difflib.unified_diff(
      expected_lines,
      actual_lines,
      fromfile=expected_path,
      tofile=actual_path,
      n=0)

  return '\n'.join(diff)
