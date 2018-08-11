# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

DIR_SOURCE_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
SDK_ROOT = os.path.join(DIR_SOURCE_ROOT, 'third_party', 'fuchsia-sdk', 'sdk')

def EnsurePathExists(path):
  """Checks that the file |path| exists on the filesystem and returns the path
  if it does, raising an exception otherwise."""

  if not os.path.exists(path):
    raise IOError('Missing file: ' + path)

  return path
